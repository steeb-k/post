/* stamp-calendar-view.c
 *
 * The calendar view of Post, built from the vendored GNOME Calendar
 * widgets (src/gcal). Re-implements the glue that GNOME Calendar keeps
 * in GcalWindow: context startup, timeline subscriptions, active-date
 * propagation, per-calendar colors, and event creation/editing.
 */

#include "stamp-calendar-view.h"

#include "gcal-agenda-view.h"
#include "gcal-calendar-list.h"
#include "gcal-calendar-management-dialog.h"
#include "gcal-calendar-navigation-button.h"
#include "gcal-context.h"
#include "gcal-date-chooser.h"
#include "gcal-date-time-utils.h"
#include "gcal-enum-types.h"
#include "gcal-event-editor-dialog.h"
#include "gcal-event-widget.h"
#include "gcal-global.h"
#include "gcal-manager.h"
#include "gcal-month-view.h"
#include "gcal-quick-add-popover.h"
#include "gcal-sync-indicator.h"
#include "gcal-timeline.h"
#include "gcal-timeline-subscriber.h"
#include "gcal-utils.h"
#include "gcal-view.h"
#include "gcal-week-view.h"

#include <glib/gi18n.h>
#include <libecal/libecal.h>

typedef struct {
  gint x;
  gint y;
  GcalRange *range;
} NewEventData;

struct _StampCalendarView {
  AdwBreakpointBin parent_instance;

  GtkWidget *sidebar_pane;

  AdwMultiLayoutView *calendar_layout;
  AdwOverlaySplitView *outer_osv;
  AdwOverlaySplitView *tablet_osv;
  AdwOverlaySplitView *mobile_osv;
  AdwToastOverlay *toast_overlay;
  GtkToggleButton *sidebar_button;
  GcalCalendarNavigationButton *navigation_button;

  AdwViewStack *views_stack;
  AdwViewStackPage *agenda_page;
  GtkWidget *month_view;
  GtkWidget *week_view;
  GtkWidget *agenda_view;
  GtkWidget *date_chooser;

  GcalEventEditorDialog *event_editor;
  GcalCalendarManagementDialog *calendar_management;
  GtkWidget *quick_add_popover;

  GSimpleActionGroup *actions;

  GtkWidget *views[GCAL_WINDOW_VIEW_N_VIEWS];
  gboolean subscribed;
  GcalWindowView active_view;
  GDateTime *active_date;

  gboolean new_event_mode;
  NewEventData *event_creation_data;
  GtkWidget *last_focused_widget;

  AdwToast *delete_event_toast;

  GtkCssProvider *colors_provider;
};

enum {
  PROP_0,
  PROP_ACTIVE_DATE,
  PROP_ACTIVE_VIEW,
  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static void event_activated (GcalView        *view,
                             GcalEventWidget *event_widget,
                             gpointer         user_data);

G_DEFINE_FINAL_TYPE (StampCalendarView, stamp_calendar_view, ADW_TYPE_BREAKPOINT_BIN);

/*
 * One-time gcal setup: default context, icon resources and stylesheets.
 * The vendored widgets expect all of these to be prepared by
 * GcalApplication; in Post the calendar view is the only consumer.
 */
static void
ensure_gcal (void)
{
  static gboolean done = FALSE;
  GtkCssProvider *theme_provider;
  GcalContext *context;

  if (done)
    return;
  done = TRUE;

  context = gcal_context_new ();
  gcal_set_default_context (context);
  gcal_context_startup (context);

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_for_display (gdk_display_get_default ()),
                                    "/org/gnome/calendar/icons");

  theme_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (theme_provider, "/org/gnome/calendar/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (theme_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (theme_provider);
}

/*
 * Auxiliary methods
 */

static void
update_today_action_enabled (StampCalendarView *self)
{
  g_autoptr (GDateTime) now = NULL;
  GSimpleAction *action;
  gboolean enabled;

  now = g_date_time_new_now_local ();

  switch (self->active_view) {
    case GCAL_WINDOW_VIEW_WEEK:
    case GCAL_WINDOW_VIEW_MONTH:
    case GCAL_WINDOW_VIEW_AGENDA:
      enabled = g_date_time_get_year (self->active_date) != g_date_time_get_year (now) ||
                g_date_time_get_week_of_year (self->active_date) != g_date_time_get_week_of_year (now);
      break;

    default:
      enabled = TRUE;
      break;
  }

  action = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self->actions), "today"));
  g_simple_action_set_enabled (action, enabled);
}

static void
focus_last_focused_widget (StampCalendarView *self)
{
  if (self->last_focused_widget && gtk_widget_grab_focus (self->last_focused_widget))
    g_clear_weak_pointer (&self->last_focused_widget);
}

static GtkWidget *
find_first_focusable_widget (GtkWidget *widget)
{
  GtkWidget *aux = widget;

  while (aux && !gtk_widget_get_focusable (aux))
    aux = gtk_widget_get_parent (aux);

  g_assert (GTK_IS_WIDGET (aux));
  return aux;
}

static void
maybe_add_subscribers_to_timeline (StampCalendarView *self)
{
  GcalContext *context = gcal_get_default_context ();
  GcalTimeline *timeline;

  if (self->subscribed)
    return;

  timeline = gcal_manager_get_timeline (gcal_context_get_manager (context));
  gcal_timeline_add_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->week_view));
  gcal_timeline_add_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->month_view));
  gcal_timeline_add_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->date_chooser));

  self->subscribed = TRUE;
}

static void
update_active_date (StampCalendarView *self,
                    GDateTime         *date)
{
  g_autoptr (GDateTime) new_date = NULL;
  GcalWindowView i;

  new_date = g_date_time_new (g_date_time_get_timezone (date),
                              g_date_time_get_year (date),
                              g_date_time_get_month (date),
                              g_date_time_get_day_of_month (date),
                              0, 0, 0);

  gcal_set_date_time (&self->active_date, new_date);

  for (i = 0; i < GCAL_WINDOW_VIEW_N_VIEWS; i++)
    gcal_view_set_date (GCAL_VIEW (self->views[i]), new_date);
  gcal_view_set_date (GCAL_VIEW (self->date_chooser), new_date);

  update_today_action_enabled (self);

  maybe_add_subscribers_to_timeline (self);
}

static void
recalculate_calendar_colors_css (StampCalendarView *self)
{
  GcalContext *context = gcal_get_default_context ();
  g_autoptr (GString) css_colors = NULL;
  g_autoptr (GList) calendars = NULL;
  GcalManager *manager;

  css_colors = g_string_new (NULL);
  manager = gcal_context_get_manager (context);
  calendars = gcal_manager_get_calendars (manager);
  for (GList *l = calendars; l; l = l->next) {
    g_autofree gchar *color_str = NULL;
    const GdkRGBA *color;
    GcalCalendar *calendar;
    GQuark color_id;

    calendar = GCAL_CALENDAR (l->data);

    color = gcal_calendar_get_color (calendar);
    color_str = gdk_rgba_to_string (color);
    color_id = g_quark_from_string (color_str);

    g_string_append_printf (css_colors, ".color-%u { --event-bg-color: %s; }\n", color_id, color_str);
  }

  gtk_css_provider_load_from_string (self->colors_provider, css_colors->str);
}

static void
load_css_providers (StampCalendarView *self)
{
  GdkDisplay *display;

  display = gtk_widget_get_display (GTK_WIDGET (self));

  /* Per-calendar colors */
  self->colors_provider = gtk_css_provider_new ();
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (self->colors_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
}

static void
show_widget (GtkWidget *widget)
{
  gtk_widget_set_visible (widget, TRUE);
}

static void
hide_widget (GtkWidget *widget)
{
  gtk_widget_set_visible (widget, FALSE);
}

static void
switch_next_view (StampCalendarView *self)
{
  g_autoptr (GDateTime) next_date = NULL;

  next_date = gcal_view_get_next_date (GCAL_VIEW (self->views[self->active_view]));
  update_active_date (self, next_date);
}

static void
switch_prev_view (StampCalendarView *self)
{
  g_autoptr (GDateTime) previous_date = NULL;

  previous_date = gcal_view_get_previous_date (GCAL_VIEW (self->views[self->active_view]));
  update_active_date (self, previous_date);
}

static void
add_or_remove_agenda_view_to_timeline (StampCalendarView *self)
{
  GcalContext *context = gcal_get_default_context ();
  GcalTimeline *timeline = gcal_manager_get_timeline (gcal_context_get_manager (context));

  if (adw_view_stack_page_get_visible (self->agenda_page))
    gcal_timeline_add_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->agenda_view));
  else
    gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->agenda_view));
}

static AdwOverlaySplitView *
get_current_osv (StampCalendarView *self)
{
  const gchar *layout = adw_multi_layout_view_get_layout_name (self->calendar_layout);

  if (g_strcmp0 (layout, "tablet") == 0)
    return self->tablet_osv;
  if (g_strcmp0 (layout, "mobile") == 0)
    return self->mobile_osv;
  return self->outer_osv;
}

/*
 * Callbacks
 */

static void
on_agenda_page_visible_changed_cb (AdwViewStackPage *agenda_page G_GNUC_UNUSED,
                                   GParamSpec *pspec             G_GNUC_UNUSED,
                                   StampCalendarView            *self)
{
  add_or_remove_agenda_view_to_timeline (self);
}

static void
event_editor_closed_cb (GcalEventEditorDialog *dialog G_GNUC_UNUSED,
                        StampCalendarView             *self)
{
  focus_last_focused_widget (self);
}

static gchar *
get_previous_date_icon (StampCalendarView *self G_GNUC_UNUSED,
                        GcalView                *view)
{
  switch (gcal_view_get_time_direction (view)) {
    case GTK_ORIENTATION_VERTICAL:
      return g_strdup ("go-up-symbolic");

    case GTK_ORIENTATION_HORIZONTAL:
    default:
      return g_strdup ("go-previous-symbolic");
  }
}

static gchar *
get_previous_date_tooltip (StampCalendarView *self G_GNUC_UNUSED,
                           const gchar             *view_name)
{
  if (g_strcmp0 (view_name, "week") == 0)
    return g_strdup (_("Previous Week"));
  else if (g_strcmp0 (view_name, "month") == 0)
    return g_strdup (_("Previous Month"));
  else
    return g_strdup (_("Yesterday"));
}

static gchar *
get_next_date_icon (StampCalendarView *self G_GNUC_UNUSED,
                    GcalView                *view)
{
  switch (gcal_view_get_time_direction (view)) {
    case GTK_ORIENTATION_VERTICAL:
      return g_strdup ("go-down-symbolic");

    case GTK_ORIENTATION_HORIZONTAL:
    default:
      return g_strdup ("go-next-symbolic");
  }
}

static gchar *
get_next_date_tooltip (StampCalendarView *self G_GNUC_UNUSED,
                       const gchar             *view_name)
{
  if (g_strcmp0 (view_name, "week") == 0)
    return g_strdup (_("Next Week"));
  else if (g_strcmp0 (view_name, "month") == 0)
    return g_strdup (_("Next Month"));
  else
    return g_strdup (_("Tomorrow"));
}

static gchar *
get_view_menu_icon (StampCalendarView *self G_GNUC_UNUSED,
                    const gchar             *view_name)
{
  if (g_strcmp0 (view_name, "week") == 0)
    return g_strdup ("calendar-week-symbolic");
  else if (g_strcmp0 (view_name, "agenda") == 0)
    return g_strdup ("calendar-agenda-symbolic");
  else
    return g_strdup ("calendar-month-symbolic");
}

static gchar *
get_view_menu_label (StampCalendarView *self G_GNUC_UNUSED,
                     const gchar             *view_name)
{
  if (g_strcmp0 (view_name, "week") == 0)
    return g_strdup (_("Week"));
  else if (g_strcmp0 (view_name, "agenda") == 0)
    return g_strdup (_("Agenda"));
  else
    return g_strdup (_("Month"));
}

static void
on_day_selected (StampCalendarView *self)
{
  update_active_date (self, gcal_view_get_date (GCAL_VIEW (self->date_chooser)));
}

static void
on_view_changed (GObject *object       G_GNUC_UNUSED,
                 GParamSpec *pspec     G_GNUC_UNUSED,
                 gpointer               user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);
  GEnumClass *eklass;
  GEnumValue *eval;

  /* XXX: this is the destruction process */
  if (!gtk_widget_get_visible (GTK_WIDGET (self->views_stack)))
    return;

  /*
   * As there are no guarantees for the order of AdwBreakpoint setters,
   * it is possible that the agenda view disappears before the month and
   * week views reappears, and vice versa.
   */
  if (!adw_view_stack_get_visible_child_name (self->views_stack))
    return;

  eklass = g_type_class_ref (GCAL_TYPE_WINDOW_VIEW);
  eval = g_enum_get_value_by_nick (eklass, adw_view_stack_get_visible_child_name (self->views_stack));

  self->active_view = eval->value;

  g_type_class_unref (eklass);

  /* Keep the view dropdown's radio state in sync */
  g_simple_action_set_state (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self->actions), "view")),
                             g_variant_new_string (adw_view_stack_get_visible_child_name (self->views_stack)));

  update_today_action_enabled (self);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_VIEW]);
}

static void
set_new_event_mode (StampCalendarView *self,
                    gboolean           enabled)
{
  self->new_event_mode = enabled;

  if (!enabled && self->views[self->active_view]) {
    gcal_view_clear_marks (GCAL_VIEW (self->views[self->active_view]));
    focus_last_focused_widget (self);
  }

  if (!enabled && gtk_widget_is_visible (self->quick_add_popover))
    gtk_widget_set_visible (self->quick_add_popover, FALSE);
}

static void
show_new_event_widget (GcalView          *view G_GNUC_UNUSED,
                       GcalRange         *range,
                       gdouble            x,
                       gdouble            y,
                       StampCalendarView *self)
{
  graphene_point_t p;
  GdkRectangle rect;
  GtkRoot *root;

  g_assert (range != NULL);

  root = gtk_widget_get_root (GTK_WIDGET (self));
  g_set_weak_pointer (&self->last_focused_widget, gtk_root_get_focus (root));

  set_new_event_mode (self, TRUE);

  if (self->event_creation_data != NULL) {
    g_clear_pointer (&self->event_creation_data->range, gcal_range_unref);
    g_clear_pointer (&self->event_creation_data, g_free);
  }

  self->event_creation_data = g_new0 (NewEventData, 1);
  self->event_creation_data->x = x;
  self->event_creation_data->y = y;
  self->event_creation_data->range = gcal_range_ref (range);

  gcal_quick_add_popover_set_range (GCAL_QUICK_ADD_POPOVER (self->quick_add_popover),
                                    self->event_creation_data->range);

  if (!gtk_widget_compute_point (self->views[self->active_view],
                                 GTK_WIDGET (self),
                                 &GRAPHENE_POINT_INIT (x, y),
                                 &p))
    g_assert_not_reached ();

  rect.x = p.x;
  rect.y = p.y;
  rect.width = 1;
  rect.height = 1;

  gtk_popover_set_pointing_to (GTK_POPOVER (self->quick_add_popover), &rect);
  gtk_popover_popup (GTK_POPOVER (self->quick_add_popover));
}

static void
close_new_event_widget (GtkButton *button G_GNUC_UNUSED,
                        gpointer           user_data)
{
  set_new_event_mode (STAMP_CALENDAR_VIEW (user_data), FALSE);
}

static void
edit_event (GcalQuickAddPopover *popover G_GNUC_UNUSED,
            GcalEvent                   *event,
            StampCalendarView           *self)
{
  gcal_event_editor_dialog_present_event (self->event_editor, GTK_WIDGET (self), event, TRUE);
}

static void
create_event_detailed_cb (GcalView          *view G_GNUC_UNUSED,
                          GcalRange         *range,
                          StampCalendarView *self)
{
  GcalContext *context = gcal_get_default_context ();
  g_autoptr (GDateTime) range_start = NULL;
  g_autoptr (GDateTime) range_end = NULL;
  GcalCalendar *default_calendar;
  GcalManager *manager;
  ECalComponent *comp;
  GcalEvent *event;

  manager = gcal_context_get_manager (context);
  range_start = gcal_range_get_start (range);
  range_end = gcal_range_get_end (range);
  comp = build_component_from_details ("", range_start, range_end);
  default_calendar = gcal_manager_get_default_calendar (manager);
  event = gcal_event_new (default_calendar, comp, NULL);

  gcal_event_editor_dialog_present_event (self->event_editor, GTK_WIDGET (self), event, TRUE);

  g_clear_object (&comp);
}

static void
event_preview_cb (GcalEventWidget        *event_widget,
                  GcalEventPreviewAction  action,
                  gpointer                user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);
  GcalEvent *event;

  switch (action) {
    case GCAL_EVENT_PREVIEW_ACTION_EDIT:
      event = gcal_event_widget_get_event (event_widget);
      gcal_event_editor_dialog_present_event (self->event_editor, GTK_WIDGET (self), event, FALSE);
      break;

    case GCAL_EVENT_PREVIEW_ACTION_NONE:
    default:
      focus_last_focused_widget (self);
      break;
  }
}

static void
event_activated (GcalView *view        G_GNUC_UNUSED,
                 GcalEventWidget       *event_widget,
                 gpointer               user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);

  g_set_weak_pointer (&self->last_focused_widget, find_first_focusable_widget (GTK_WIDGET (event_widget)));

  gcal_event_widget_show_preview (event_widget, event_preview_cb, user_data);
}

static void
on_toast_dismissed_cb (AdwToast          *toast,
                       StampCalendarView *self)
{
  GcalContext *context = gcal_get_default_context ();
  GcalRecurrenceModType modifier;
  GcalManager *manager;
  GcalEvent *event;

  /* If we undid the removal, the stored toast is gone at this point */
  if (!self->delete_event_toast)
    return;

  manager = gcal_context_get_manager (context);
  event = g_object_get_data (G_OBJECT (toast), "event");
  modifier = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (toast), "modifier"));

  g_assert (event != NULL);

  gcal_manager_remove_event (manager, event, modifier);
  g_clear_object (&self->delete_event_toast);
}

static void
on_event_editor_dialog_remove_event_cb (GcalEventEditorDialog *edit_dialog G_GNUC_UNUSED,
                                        GcalEvent                          *event,
                                        GcalRecurrenceModType               modifier,
                                        StampCalendarView                  *self)
{
  g_autoptr (AdwToast) toast = NULL;
  g_autoptr (GList) widgets = NULL;
  g_autoptr (GList) agenda_view_widgets = NULL;
  g_autoptr (GList) date_chooser_widgets = NULL;
  GcalView *view;
  gboolean has_deleted_event;

  has_deleted_event = self->delete_event_toast != NULL;
  if (self->delete_event_toast)
    adw_toast_dismiss (self->delete_event_toast);
  self->delete_event_toast = NULL;

  toast = adw_toast_new (has_deleted_event ? _("Another event deleted") : _("Event deleted"));
  adw_toast_set_timeout (toast, 5);
  adw_toast_set_button_label (toast, _("_Undo"));
  adw_toast_set_action_name (toast, "calendar.undo-delete-event");
  g_object_set_data_full (G_OBJECT (toast), "event", g_object_ref (event), g_object_unref);
  g_object_set_data (G_OBJECT (toast), "modifier", GINT_TO_POINTER (modifier));
  g_signal_connect (toast, "dismissed", G_CALLBACK (on_toast_dismissed_cb), self);

  adw_toast_overlay_add_toast (self->toast_overlay, g_object_ref (toast));
  self->delete_event_toast = g_steal_pointer (&toast);

  /* hide widget of the event */
  view = GCAL_VIEW (self->views[self->active_view]);
  widgets = gcal_view_get_children_by_uuid (view, modifier, gcal_event_get_uid (event));
  agenda_view_widgets = gcal_view_get_children_by_uuid (GCAL_VIEW (self->agenda_view), modifier, gcal_event_get_uid (event));
  date_chooser_widgets = gcal_view_get_children_by_uuid (GCAL_VIEW (self->date_chooser), modifier, gcal_event_get_uid (event));

  g_list_foreach (widgets, (GFunc) hide_widget, NULL);
  g_list_foreach (agenda_view_widgets, (GFunc) hide_widget, NULL);
  g_list_foreach (date_chooser_widgets, (GFunc) hide_widget, NULL);
}

/*
 * Actions
 */

static void
on_change_view_activated (GSimpleAction *action G_GNUC_UNUSED,
                          GVariant              *param,
                          gpointer               user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);
  gint32 view;

  view = g_variant_get_int32 (param);

  /* -1 means next view, -2 means previous view */
  if (view == -1)
    view = self->active_view + 1;
  else if (view == -2)
    view = self->active_view - 1;

  view = CLAMP (view, 0, GCAL_WINDOW_VIEW_N_VIEWS - 1);

  if (adw_view_stack_page_get_visible (adw_view_stack_get_page (self->views_stack, self->views[view]))) {
    self->active_view = view;
    adw_view_stack_set_visible_child (self->views_stack, self->views[self->active_view]);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_VIEW]);
  }
}

static void
on_next_date_activated (GSimpleAction *action G_GNUC_UNUSED,
                        GVariant *param       G_GNUC_UNUSED,
                        gpointer               user_data)
{
  switch_next_view (STAMP_CALENDAR_VIEW (user_data));
}

static void
on_previous_date_activated (GSimpleAction *action G_GNUC_UNUSED,
                            GVariant *param       G_GNUC_UNUSED,
                            gpointer               user_data)
{
  switch_prev_view (STAMP_CALENDAR_VIEW (user_data));
}

static void
on_today_activated (GSimpleAction *action G_GNUC_UNUSED,
                    GVariant *param       G_GNUC_UNUSED,
                    gpointer               user_data)
{
  g_autoptr (GDateTime) today = NULL;

  today = g_date_time_new_now_local ();
  update_active_date (STAMP_CALENDAR_VIEW (user_data), today);
}

static void
on_new_event_activated (GSimpleAction *action G_GNUC_UNUSED,
                        GVariant *param       G_GNUC_UNUSED,
                        gpointer               user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);
  GcalContext *context = gcal_get_default_context ();
  g_autoptr (ECalComponent) comp = NULL;
  g_autoptr (GDateTime) start = NULL;
  g_autoptr (GDateTime) end = NULL;
  GcalCalendar *default_calendar;
  GcalManager *manager;
  GcalEvent *event;

  start = g_date_time_new_utc (g_date_time_get_year (self->active_date),
                               g_date_time_get_month (self->active_date),
                               g_date_time_get_day_of_month (self->active_date),
                               0, 0, 0);
  end = g_date_time_add_days (start, 1);

  manager = gcal_context_get_manager (context);
  comp = build_component_from_details ("", start, end);
  default_calendar = gcal_manager_get_default_calendar (manager);
  event = gcal_event_new (default_calendar, comp, NULL);

  gcal_event_editor_dialog_present_event (self->event_editor, GTK_WIDGET (self), event, TRUE);
}

static void
on_show_calendars_activated (GSimpleAction *action G_GNUC_UNUSED,
                             GVariant *param       G_GNUC_UNUSED,
                             gpointer               user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);

  adw_dialog_present (ADW_DIALOG (self->calendar_management), GTK_WIDGET (self));
}

static void
on_view_activated (GSimpleAction *action G_GNUC_UNUSED,
                   GVariant              *param,
                   gpointer               user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);

  adw_view_stack_set_visible_child_name (self->views_stack, g_variant_get_string (param, NULL));
}

static void
on_undo_delete_event_activated (GSimpleAction *action G_GNUC_UNUSED,
                                GVariant *param       G_GNUC_UNUSED,
                                gpointer               user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);
  g_autoptr (GList) widgets = NULL;
  GcalRecurrenceModType modifier;
  GcalEvent *event;

  if (!self->delete_event_toast)
    return;

  event = g_object_get_data (G_OBJECT (self->delete_event_toast), "event");
  modifier = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (self->delete_event_toast), "modifier"));

  g_assert (event != NULL);

  /* Show the hidden to-be-deleted events */
  widgets = gcal_view_get_children_by_uuid (GCAL_VIEW (self->views[self->active_view]),
                                            modifier,
                                            gcal_event_get_uid (event));

  g_list_foreach (widgets, (GFunc) show_widget, NULL);

  g_clear_object (&self->delete_event_toast);
}

/*
 * Sidebar handling, following the contact view
 */

static void
on_toggle_sidebar (GtkToggleButton *btn G_GNUC_UNUSED,
                   gpointer             user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);
  AdwOverlaySplitView *osv = get_current_osv (self);

  adw_overlay_split_view_set_show_sidebar (osv, !adw_overlay_split_view_get_show_sidebar (osv));
}

static void
on_sidebar_visibility_changed (StampCalendarView *self)
{
  AdwOverlaySplitView *osv = get_current_osv (self);
  gboolean shown;

  shown = adw_overlay_split_view_get_show_sidebar (osv);
  if (gtk_toggle_button_get_active (self->sidebar_button) != shown)
    gtk_toggle_button_set_active (self->sidebar_button, shown);

  g_object_set (self->navigation_button, "active", !shown, NULL);
}

static void
on_navigation_button_active_changed (StampCalendarView *self)
{
  AdwOverlaySplitView *osv = get_current_osv (self);
  gboolean active;

  g_object_get (self->navigation_button, "active", &active, NULL);
  if (adw_overlay_split_view_get_show_sidebar (osv) == active)
    adw_overlay_split_view_set_show_sidebar (osv, !active);
}

static void
on_layout_changed (AdwMultiLayoutView *view,
                   GParamSpec *ps      G_GNUC_UNUSED,
                   gpointer            user_data)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (user_data);
  const gchar *name = adw_multi_layout_view_get_layout_name (view);
  gboolean narrow = g_strcmp0 (name, "desktop") != 0;

  gtk_widget_set_visible (GTK_WIDGET (self->sidebar_button), narrow);
}

/*
 * GObject overrides
 */

static void
stamp_calendar_view_dispose (GObject *object)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (object);
  GcalContext *context = gcal_get_default_context ();

  if (context && self->subscribed) {
    GcalTimeline *timeline = gcal_manager_get_timeline (gcal_context_get_manager (context));

    gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->week_view));
    gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->month_view));
    gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->agenda_view));
    gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->date_chooser));
    self->subscribed = FALSE;
  }

  g_clear_pointer (&self->quick_add_popover, gtk_widget_unparent);
  g_clear_weak_pointer (&self->last_focused_widget);

  if (self->delete_event_toast)
    adw_toast_dismiss (self->delete_event_toast);

  g_clear_object (&self->actions);

  G_OBJECT_CLASS (stamp_calendar_view_parent_class)->dispose (object);
}

static void
stamp_calendar_view_finalize (GObject *object)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (object);

  if (self->event_creation_data) {
    g_clear_pointer (&self->event_creation_data->range, gcal_range_unref);
    g_clear_pointer (&self->event_creation_data, g_free);
  }

  gcal_clear_date_time (&self->active_date);
  g_clear_object (&self->colors_provider);

  G_OBJECT_CLASS (stamp_calendar_view_parent_class)->finalize (object);
}

static void
stamp_calendar_view_get_property (GObject    *object,
                                  guint       id,
                                  GValue     *value,
                                  GParamSpec *ps)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (object);

  switch (id) {
    case PROP_ACTIVE_DATE:
      g_value_set_boxed (value, self->active_date);
      break;

    case PROP_ACTIVE_VIEW:
      g_value_set_enum (value, self->active_view);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, ps);
  }
}

static void
stamp_calendar_view_set_property (GObject      *object,
                                  guint         id,
                                  const GValue *value,
                                  GParamSpec   *ps)
{
  StampCalendarView *self = STAMP_CALENDAR_VIEW (object);

  switch (id) {
    case PROP_ACTIVE_DATE:
      update_active_date (self, g_value_get_boxed (value));
      break;

    case PROP_ACTIVE_VIEW:
      self->active_view = g_value_get_enum (value);
      adw_view_stack_set_visible_child (self->views_stack, self->views[self->active_view]);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, ps);
  }
}

void
stamp_calendar_view_class_init (StampCalendarViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (GCAL_TYPE_AGENDA_VIEW);
  g_type_ensure (GCAL_TYPE_CALENDAR_LIST);
  g_type_ensure (GCAL_TYPE_CALENDAR_MANAGEMENT_DIALOG);
  g_type_ensure (GCAL_TYPE_CALENDAR_NAVIGATION_BUTTON);
  g_type_ensure (GCAL_TYPE_DATE_CHOOSER);
  g_type_ensure (GCAL_TYPE_EVENT_EDITOR_DIALOG);
  g_type_ensure (GCAL_TYPE_MANAGER);
  g_type_ensure (GCAL_TYPE_MONTH_VIEW);
  g_type_ensure (GCAL_TYPE_QUICK_ADD_POPOVER);
  g_type_ensure (GCAL_TYPE_SYNC_INDICATOR);
  g_type_ensure (GCAL_TYPE_WEEK_VIEW);

  object_class->dispose = stamp_calendar_view_dispose;
  object_class->finalize = stamp_calendar_view_finalize;
  object_class->get_property = stamp_calendar_view_get_property;
  object_class->set_property = stamp_calendar_view_set_property;

  properties[PROP_ACTIVE_DATE] = g_param_spec_boxed ("active-date", NULL, NULL,
                                                     G_TYPE_DATE_TIME,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_ACTIVE_VIEW] = g_param_spec_enum ("active-view", NULL, NULL,
                                                    GCAL_TYPE_WINDOW_VIEW,
                                                    GCAL_WINDOW_VIEW_MONTH,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/calendar/stamp-calendar-view.ui");

  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, calendar_layout);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, outer_osv);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, tablet_osv);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, sidebar_pane);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, mobile_osv);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, sidebar_button);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, navigation_button);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, views_stack);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, agenda_page);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, month_view);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, week_view);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, agenda_view);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, date_chooser);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, event_editor);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, calendar_management);
  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, quick_add_popover);

  gtk_widget_class_bind_template_callback (widget_class, on_day_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_view_changed);
  gtk_widget_class_bind_template_callback (widget_class, get_previous_date_icon);
  gtk_widget_class_bind_template_callback (widget_class, get_previous_date_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, get_next_date_icon);
  gtk_widget_class_bind_template_callback (widget_class, get_next_date_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, get_view_menu_icon);
  gtk_widget_class_bind_template_callback (widget_class, get_view_menu_label);
  gtk_widget_class_bind_template_callback (widget_class, show_new_event_widget);
  gtk_widget_class_bind_template_callback (widget_class, create_event_detailed_cb);
  gtk_widget_class_bind_template_callback (widget_class, event_activated);
  gtk_widget_class_bind_template_callback (widget_class, edit_event);
  gtk_widget_class_bind_template_callback (widget_class, close_new_event_widget);
  gtk_widget_class_bind_template_callback (widget_class, on_event_editor_dialog_remove_event_cb);
}

void
stamp_calendar_view_init (StampCalendarView *self)
{
  static const GActionEntry actions[] = {
    { .name = "change-view", .activate = on_change_view_activated, .parameter_type = "i" },
    { .name = "next-date", .activate = on_next_date_activated },
    { .name = "new-event", .activate = on_new_event_activated },
    { .name = "previous-date", .activate = on_previous_date_activated },
    { .name = "show-calendars", .activate = on_show_calendars_activated },
    { .name = "today", .activate = on_today_activated },
    { .name = "undo-delete-event", .activate = on_undo_delete_event_activated },
    { .name = "view", .activate = on_view_activated, .parameter_type = "s", .state = "'month'" },
  };
  GcalContext *context;

  ensure_gcal ();
  context = gcal_get_default_context ();

  self->actions = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->actions), actions, G_N_ELEMENTS (actions), self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "calendar", G_ACTION_GROUP (self->actions));

  gtk_widget_init_template (GTK_WIDGET (self));

  self->views[GCAL_WINDOW_VIEW_WEEK] = self->week_view;
  self->views[GCAL_WINDOW_VIEW_MONTH] = self->month_view;
  self->views[GCAL_WINDOW_VIEW_AGENDA] = self->agenda_view;

  self->active_date = g_date_time_new_from_unix_local (0);

  load_css_providers (self);
  g_object_connect (gcal_context_get_manager (context),
                    "swapped-object-signal::calendar-added", recalculate_calendar_colors_css, self,
                    "swapped-object-signal::calendar-changed", recalculate_calendar_colors_css, self,
                    "swapped-object-signal::calendar-removed", recalculate_calendar_colors_css, self,
                    NULL);
  recalculate_calendar_colors_css (self);

  gtk_widget_set_parent (self->quick_add_popover, GTK_WIDGET (self));

  g_signal_connect_object (self->event_editor, "closed", G_CALLBACK (event_editor_closed_cb), self, 0);
  g_signal_connect (self->agenda_page, "notify::visible", G_CALLBACK (on_agenda_page_visible_changed_cb), self);

  adw_multi_layout_view_set_layout_name (self->calendar_layout, "desktop");
  g_signal_connect (self->calendar_layout, "notify::layout-name", G_CALLBACK (on_layout_changed), self);
  g_signal_connect (self->sidebar_button, "clicked", G_CALLBACK (on_toggle_sidebar), self);
  g_signal_connect_swapped (self->navigation_button, "notify::active", G_CALLBACK (on_navigation_button_active_changed), self);
  g_signal_connect_swapped (self->outer_osv, "notify::show-sidebar", G_CALLBACK (on_sidebar_visibility_changed), self);
  g_signal_connect_swapped (self->tablet_osv, "notify::show-sidebar", G_CALLBACK (on_sidebar_visibility_changed), self);
  g_signal_connect_swapped (self->mobile_osv, "notify::show-sidebar", G_CALLBACK (on_sidebar_visibility_changed), self);

  g_settings_bind (gcal_context_get_settings (context),
                   "active-view",
                   self,
                   "active-view",
                   G_SETTINGS_BIND_SET | G_SETTINGS_BIND_GET);

  {
    g_autoptr (GDateTime) today = g_date_time_new_now_local ();
    update_active_date (self, today);
  }
}

GtkWidget *
stamp_calendar_view_new (void)
{
  return g_object_new (STAMP_TYPE_CALENDAR_VIEW, NULL);
}

GtkWidget *
stamp_calendar_view_get_sidebar (StampCalendarView *self)
{
  return self->sidebar_pane;
}
