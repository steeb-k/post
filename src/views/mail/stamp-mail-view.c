/*
 * Copyright 2025-2026 Jan-Michael Brummer
 * Copyright 2026 steeb-k
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stamp-mail-view.h"

#include <glib/gi18n.h>

#include "stamp-conversation-list.h"
#include "stamp-folder-list.h"
#include "stamp-gcal.h"
#include "stamp-header-bar.h"
#include "stamp-message-list.h"
#include "stamp-settings.h"
#include "stamp-sidebar-agenda.h"
#include "stamp-window.h"

#include "gcal-agenda-view.h"
#include "gcal-event-widget.h"
#include "gcal-global.h"
#include "gcal-manager.h"
#include "gcal-timeline.h"
#include "gcal-timeline-subscriber.h"
#include "gcal-view.h"

/* How much room the window has, which is what the breakpoints decide.
 * The layout the user picked says what to do with it. */
typedef enum {
  SIZE_DESKTOP,
  SIZE_TABLET,
  SIZE_MOBILE,
} StampMailViewSize;

struct _StampMailView {
  AdwBreakpointBin parent_instance;

  GtkWidget *sidebar_pane;
  AdwViewStack *stack;

  AdwMultiLayoutView *mail_layout;
  AdwViewSwitcher *app_switcher;
  GtkSizeGroup *switcher_sizes;
  GtkWidget *compose_button;
  gboolean switcher_sized;
  AdwBreakpoint *bp_tablet;
  AdwBreakpoint *bp_mobile;
  AdwOverlaySplitView *tablet_osv;
  AdwOverlaySplitView *tablet_stacked_osv;
  AdwOverlaySplitView *mobile_osv;
  AdwNavigationView *mobile_nav;
  StampFolderList *folder_list;
  StampMessageList *message_list;
  StampConversationList *conversation_list;
  StampSidebarAgenda *sidebar_agenda;
  GtkWidget *folder_spacer;
  GtkSizeGroup *agenda_clearance;
  GtkPaned *desktop_paned;
  GtkPaned *tablet_paned;
  GtkPaned *stacked_paned;
  GtkPaned *dense_paned;
  GtkPaned *tablet_stacked_paned;
  GtkPaned *dense_agenda_paned;
  GtkWidget *dense_agenda;
  AdwHeaderBar *agenda_header;
  AdwToastOverlay *toast_overlay;


  GSimpleActionGroup *actions;
  StampAccount *account;

  StampMailViewSize size;
  StampMailLayout layout;

  gint saved_paned_pos;
  gint saved_stacked_pos;
  gint agenda_width;
  guint agenda_width_pending : 1;
  guint agenda_subscribed : 1;
  guint load_folder_handler;
};

G_DEFINE_FINAL_TYPE (StampMailView, stamp_mail_view, ADW_TYPE_BREAKPOINT_BIN);

static void set_agenda_subscribed (StampMailView *self,
                                   gboolean       subscribed);

static void on_agenda_event_activated (GcalAgendaView  *view,
                                       GcalEventWidget *event_widget,
                                       gpointer         user_data);

static void
stamp_mail_view_get_property (GObject    *object,
                              guint       id,
                              GValue     *value,
                              GParamSpec *ps)
{
  if (id == 1)
    g_value_set_object (value, STAMP_MAIL_VIEW (object)->stack);
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, ps);
}

static void
stamp_mail_view_set_property (GObject      *object,
                              guint         id,
                              const GValue *value,
                              GParamSpec   *ps)
{
  if (id == 1)
    g_set_object (&STAMP_MAIL_VIEW (object)->stack, g_value_get_object (value));
  else
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, ps);
}

/*
 * The split view whose sidebar overlays the content, so it can be
 * toggled and closed again. The desktop layouts keep theirs open.
 */
static AdwOverlaySplitView *
get_current_osv (StampMailView *self)
{
  const gchar *layout = adw_multi_layout_view_get_layout_name (self->mail_layout);

  if (g_strcmp0 (layout, "tablet") == 0)
    return self->tablet_osv;

  if (g_strcmp0 (layout, "tablet-stacked") == 0)
    return self->tablet_stacked_osv;

  if (g_strcmp0 (layout, "mobile") == 0)
    return self->mobile_osv;

  return NULL;
}

/*
 * The layout name for the size the window is at and the layout that was
 * asked for. Dense needs a column the tablet width cannot spare, so it
 * falls back to stacked, which is the closest thing.
 */
static const gchar *
layout_name_for (StampMailView *self)
{
  switch (self->size) {
    case SIZE_MOBILE:
      return "mobile";

    case SIZE_TABLET:
      return self->layout == STAMP_MAIL_LAYOUT_SIDE_BY_SIDE ? "tablet" : "tablet-stacked";

    case SIZE_DESKTOP:
    default:
      switch (self->layout) {
        case STAMP_MAIL_LAYOUT_STACKED:
          return "desktop-stacked";

        case STAMP_MAIL_LAYOUT_DENSE:
          return "desktop-dense";

        case STAMP_MAIL_LAYOUT_SIDE_BY_SIDE:
        default:
          return "desktop";
      }
  }
}

static void
update_layout_name (StampMailView *self)
{
  adw_multi_layout_view_set_layout_name (self->mail_layout, layout_name_for (self));
}


/*
 * AdwViewSwitcher gives its own buttons a common width, so matching the
 * action button to any one of them makes the whole row uniform. There
 * is no API for reaching a switcher button, hence the search; finding
 * nothing leaves the row as it was rather than failing.
 */
static GtkWidget *
find_switcher_button (GtkWidget *widget)
{
  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child;
       child = gtk_widget_get_next_sibling (child)) {
    GtkWidget *found;

    if (GTK_IS_TOGGLE_BUTTON (child))
      return child;

    found = find_switcher_button (child);
    if (found)
      return found;
  }

  return NULL;
}

static void
match_action_to_switcher (StampMailView *self)
{
  GtkWidget *button;

  if (self->switcher_sized)
    return;

  /* The switcher has no buttons until it has a stack, which is handed
   * down after this view is built -- so this waits for the first
   * breakpoint change rather than running at construction. */
  button = find_switcher_button (GTK_WIDGET (self->app_switcher));
  if (!button)
    return;

  gtk_size_group_add_widget (self->switcher_sizes, button);
  self->switcher_sized = TRUE;
}

static void
on_size_changed (GObject *object     G_GNUC_UNUSED,
                 GParamSpec *pspec   G_GNUC_UNUSED,
                 gpointer            user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  AdwBreakpoint *current = adw_breakpoint_bin_get_current_breakpoint (ADW_BREAKPOINT_BIN (self));

  if (current == self->bp_mobile)
    self->size = SIZE_MOBILE;
  else if (current == self->bp_tablet)
    self->size = SIZE_TABLET;
  else
    self->size = SIZE_DESKTOP;

  match_action_to_switcher (self);
  update_layout_name (self);
}

static void
on_details_hidden (AdwNavigationPage *page,
                   gpointer           user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_conversation_list_unselect (self->conversation_list);
}

static void
on_sidebar_visibility_changed (StampMailView *self)
{
  AdwOverlaySplitView *osv = get_current_osv (self);
  GtkToggleButton *toggle_button;
  gboolean shown;

  if (!osv)
    return;

  shown = adw_overlay_split_view_get_show_sidebar (osv);
  toggle_button = stamp_conversation_list_get_sidebar_button (self->conversation_list);
  if (gtk_toggle_button_get_active (toggle_button) != shown)
    gtk_toggle_button_set_active (toggle_button, shown);
}

static void
close_overlay_sidebar (StampMailView *self)
{
  AdwOverlaySplitView *osv = get_current_osv (self);

  if (!osv || !adw_overlay_split_view_get_show_sidebar (osv))
    return;

  adw_overlay_split_view_set_show_sidebar (osv, FALSE);
  gtk_toggle_button_set_active (stamp_conversation_list_get_sidebar_button (self->conversation_list), FALSE);
}

typedef struct {
  StampMailView *self;
  StampAccount *account;
  gchar *full_name;
} LoadFolderData;

static void
load_folder_data_free (gpointer user_data)
{
  LoadFolderData *data = user_data;

  g_object_unref (data->account);
  g_clear_pointer (&data->full_name, g_free);
  g_free (data);
}

static gboolean
load_folder_idle (gpointer user_data)
{
  LoadFolderData *data = user_data;
  StampMailView *self = data->self;

  close_overlay_sidebar (self);

  g_set_object (&self->account, data->account);
  stamp_conversation_list_load_folder (self->conversation_list, data->account, data->full_name);

  self->load_folder_handler = 0;
  return G_SOURCE_REMOVE;
}

static void
on_folder_selected (GtkWidget    *object,
                    StampAccount *account,
                    gchar        *full_name,
                    gpointer      user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  LoadFolderData *data;

  data = g_new0 (LoadFolderData, 1);
  data->self = self;
  data->account = g_object_ref (account);
  data->full_name = g_strdup (full_name);

  g_clear_handle_id (&self->load_folder_handler, g_source_remove);
  self->load_folder_handler = g_idle_add_full (G_PRIORITY_DEFAULT, load_folder_idle, data, load_folder_data_free);
  stamp_message_list_set_conversation (self->message_list, self->account, NULL);
}

/* The active profile stopped showing the account whose folder was open,
 * so there is nothing to select and nothing that should stay on screen. */
static void
on_folder_cleared (GtkWidget *object,
                   gpointer   user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  g_clear_handle_id (&self->load_folder_handler, g_source_remove);
  g_clear_object (&self->account);

  stamp_message_list_set_conversation (self->message_list, NULL, NULL);
  stamp_conversation_list_clear (self->conversation_list);
}

static void
on_conversation_selected (GtkWidget *object,
                          gpointer   thread_node,
                          gpointer   user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  if (thread_node) {
    const gchar *layout = adw_multi_layout_view_get_layout_name (self->mail_layout);

    if (g_strcmp0 (layout, "mobile") == 0) {
      AdwNavigationPage *visible = adw_navigation_view_get_visible_page (self->mobile_nav);
      const gchar *tag = adw_navigation_page_get_tag (visible);

      if (g_strcmp0 (tag, "content") != 0)
        adw_navigation_view_push_by_tag (self->mobile_nav, "content");
    }
  }

  stamp_message_list_set_conversation (self->message_list, self->account, (CamelFolderThreadNode *)thread_node);
}

static void
on_trash_undo (AdwToast *toast,
               gpointer  user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_conversation_list_undo_trash (self->conversation_list);
}

static void
on_trash_dismissed (AdwToast *toast,
                    gpointer  user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_conversation_list_finalize_trash (self->conversation_list);
}

static void
trash (StampMailView         *self,
       StampConversationItem *item)
{
  if (stamp_conversation_list_trash (self->conversation_list, item)) {
    AdwToast *toast;

    toast = adw_toast_new (_("Conversations moved to trash"));
    adw_toast_set_button_label (toast, _("_Undo"));
    adw_toast_set_timeout (toast, 5);
    g_signal_connect (toast, "button-clicked", G_CALLBACK (on_trash_undo), self);
    g_signal_connect (toast, "dismissed", G_CALLBACK (on_trash_dismissed), self);
    adw_toast_overlay_add_toast (self->toast_overlay, toast);
  }

  stamp_message_list_set_conversation (self->message_list, NULL, NULL);
}

static void
on_conversation_trash (GtkWidget             *object,
                       StampConversationItem *item,
                       gpointer               user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  trash (self, item);
}

static void
stamp_mail_view_dispose (GObject *object)
{
  StampMailView *self = STAMP_MAIL_VIEW (object);

  g_clear_handle_id (&self->load_folder_handler, g_source_remove);

  if (self->dense_agenda)
    set_agenda_subscribed (self, FALSE);

  g_clear_object (&self->actions);
  g_clear_object (&self->account);
  g_clear_object (&self->agenda_clearance);

  /* FIXME: Does not work with AdwBreakpointBin */
  /* gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_MAIL_VIEW); */

  G_OBJECT_CLASS (stamp_mail_view_parent_class)->dispose (object);
}

static gboolean
is_mobile_view (StampMailView *self)
{
  return self->size == SIZE_MOBILE;
}

void
stamp_mail_view_class_init (StampMailViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_ensure (STAMP_TYPE_SIDEBAR_AGENDA);
  g_type_ensure (GCAL_TYPE_AGENDA_VIEW);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/mail/stamp-mail-view.ui");

  object_class->dispose = stamp_mail_view_dispose;
  object_class->get_property = stamp_mail_view_get_property;
  object_class->set_property = stamp_mail_view_set_property;

  g_object_class_install_property (object_class, 1, g_param_spec_object ("stack", NULL, NULL,
                                                                         ADW_TYPE_VIEW_STACK,
                                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gtk_widget_class_bind_template_child (widget_class, StampMailView, mail_layout);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, app_switcher);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, switcher_sizes);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, compose_button);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, bp_tablet);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, bp_mobile);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, folder_list);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, message_list);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, conversation_list);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, desktop_paned);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, tablet_paned);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, stacked_paned);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, dense_paned);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, tablet_stacked_paned);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, dense_agenda_paned);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, dense_agenda);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, agenda_header);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, tablet_osv);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, tablet_stacked_osv);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, sidebar_pane);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, sidebar_agenda);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, folder_spacer);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, mobile_osv);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, mobile_nav);
  gtk_widget_class_bind_template_callback (widget_class, on_details_hidden);
  gtk_widget_class_bind_template_callback (widget_class, on_size_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_agenda_event_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_folder_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_folder_cleared);
  gtk_widget_class_bind_template_callback (widget_class, on_conversation_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_conversation_trash);
}

static void
on_reply (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (self->message_list, STAMP_COMPOSER_REPLY, parameter);
}

static void
on_reply_all (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (self->message_list, STAMP_COMPOSER_REPLY_ALL, parameter);
}

static void
on_forward (GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (self->message_list, STAMP_COMPOSER_FORWARD, parameter);
}

static void
on_trash (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  trash (self, NULL);
}

static void
on_composer_new (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (self->message_list, STAMP_COMPOSER_NEW, parameter);
}

static void
on_edit (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (self->message_list, STAMP_COMPOSER_DRAFT, parameter);
}

static void
junk (StampMailView         *self,
      StampConversationItem *item)
{
  AdwToast *toast;

  stamp_conversation_list_junk (self->conversation_list, item);

  toast = adw_toast_new (_("Conversations moved to junk"));
  adw_toast_overlay_add_toast (self->toast_overlay, toast);
}

static void
on_junk (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  junk (self, NULL);
}

static const GActionEntry stamp_mail_view_action_entries[] = {
  { .name = "composer-new", .activate = on_composer_new },
  { .name = "edit", .activate = on_edit },
  { .name = "forward", .activate = on_forward, .parameter_type = "s" },
  { .name = "forward-current", .activate = on_forward },
  { .name = "reply", .activate = on_reply, .parameter_type = "s" },
  { .name = "reply-all", .activate = on_reply_all, .parameter_type = "s" },
  { .name = "reply-all-current", .activate = on_reply_all },
  { .name = "reply-current", .activate = on_reply },
  { .name = "trash", .activate = on_trash },
  { .name = "junk", .activate = on_junk },
};

static void
on_toggle_sidebar (GtkToggleButton *btn G_GNUC_UNUSED,
                   gpointer             user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  AdwOverlaySplitView *osv = get_current_osv (self);

  if (!osv)
    return;

  stamp_folder_list_unselect (self->folder_list);
  adw_overlay_split_view_set_show_sidebar (osv, !adw_overlay_split_view_get_show_sidebar (osv));
}

/*
 * The dense agenda only costs anything while it is on screen, so it
 * joins the timeline when the dense layout does and leaves with it.
 */
static void
set_agenda_subscribed (StampMailView *self,
                       gboolean       subscribed)
{
  GcalContext *context;
  GcalTimeline *timeline;

  if (self->agenda_subscribed == subscribed)
    return;

  context = subscribed ? stamp_gcal_ensure_context () : gcal_get_default_context ();
  if (!context)
    return;

  timeline = gcal_manager_get_timeline (gcal_context_get_manager (context));

  if (subscribed) {
    g_autoptr (GDateTime) now = g_date_time_new_now_local ();

    /* The day may well have turned over while it was away. */
    gcal_view_set_date (GCAL_VIEW (self->dense_agenda), now);
    gcal_timeline_add_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->dense_agenda));
  } else {
    gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->dense_agenda));
  }

  self->agenda_subscribed = subscribed;
}

/* The agenda starts at the day it was given, so a dense layout left up
 * overnight would still be starting at yesterday. */
static void
on_day_changed (GcalClock     *clock G_GNUC_UNUSED,
                StampMailView *self)
{
  g_autoptr (GDateTime) now = NULL;

  if (!self->agenda_subscribed)
    return;

  now = g_date_time_new_now_local ();
  gcal_view_set_date (GCAL_VIEW (self->dense_agenda), now);
}

static void
on_agenda_event_activated (GcalAgendaView  *view G_GNUC_UNUSED,
                           GcalEventWidget *event_widget,
                           gpointer         user_data)
{
  StampWindow *window = stamp_get_main_window ();

  if (window)
    stamp_window_show_event (window, gcal_event_widget_get_event (event_widget));
}

/*
 * The window controls belong in the window's top right corner, which is
 * a different pane in every layout: the reading pane when it is beside
 * the list, the list when the reading pane is below it, and the agenda
 * column when there is one. On mobile each page is the whole window, so
 * the two that can be on top both need a set.
 *
 * The layouts that put the reading pane below the list leave it short,
 * so the list packs its rows tighter there.
 */
static void
update_window_controls (StampMailView *self,
                        const gchar   *layout)
{
  gboolean stacked = g_strcmp0 (layout, "desktop-stacked") == 0 ||
                     g_strcmp0 (layout, "tablet-stacked") == 0;
  gboolean dense = g_strcmp0 (layout, "desktop-dense") == 0;
  gboolean mobile = g_strcmp0 (layout, "mobile") == 0;

  stamp_consersation_list_set_show_buttons (self->conversation_list, stacked || mobile);
  stamp_conversation_list_set_compact (self->conversation_list, stacked || dense);
  stamp_message_list_set_show_window_controls (self->message_list, mobile || !(stacked || dense));
  adw_header_bar_set_show_end_title_buttons (self->agenda_header, dense);
}

static void
on_layout_changed (AdwMultiLayoutView *view,
                   GParamSpec *ps      G_GNUC_UNUSED,
                   gpointer            user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  const gchar *name = adw_multi_layout_view_get_layout_name (view);
  gboolean dense = g_strcmp0 (name, "desktop-dense") == 0;
  gboolean narrow = self->size != SIZE_DESKTOP;
  GtkToggleButton *toggle_button = stamp_conversation_list_get_sidebar_button (self->conversation_list);

  gtk_widget_set_visible (GTK_WIDGET (toggle_button), narrow);

  stamp_message_list_set_mobile_mode (self->message_list, self->size == SIZE_MOBILE);
  stamp_conversation_list_set_show_view_button (self->conversation_list, self->size != SIZE_MOBILE);

  update_window_controls (self, name);

  /* The sidebar strip stands down where the agenda has a pane of its
   * own, and on mobile, where the sidebar is a narrow overlay and the
   * calendar's own agenda is one tap away in the switcher. */
  stamp_sidebar_agenda_set_enabled (self->sidebar_agenda, !dense && self->size != SIZE_MOBILE);

  set_agenda_subscribed (self, dense);

  if (dense)
    self->agenda_width_pending = TRUE;

  if (!narrow && self->saved_paned_pos > 50) {
    gtk_paned_set_position (self->desktop_paned, self->saved_paned_pos);
    gtk_paned_set_position (self->tablet_paned, self->saved_paned_pos);
  }

  if (self->saved_stacked_pos > 50) {
    gtk_paned_set_position (self->stacked_paned, self->saved_stacked_pos);
    gtk_paned_set_position (self->dense_paned, self->saved_stacked_pos);
    gtk_paned_set_position (self->tablet_stacked_paned, self->saved_stacked_pos);
  }
}

static void
on_paned_changed (GtkPaned      *paned,
                  GParamSpec *ps G_GNUC_UNUSED,
                  gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  gint pos = gtk_paned_get_position (paned);

  if (pos < 50)
    return;

  self->saved_paned_pos = pos;
}

static void
on_stacked_paned_changed (GtkPaned      *paned,
                          GParamSpec *ps G_GNUC_UNUSED,
                          gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  gint pos = gtk_paned_get_position (paned);

  if (pos < 50)
    return;

  self->saved_stacked_pos = pos;
}

/*
 * GtkPaned counts from the start, so the agenda column would grow with
 * the window if left alone. Its width is what is worth remembering, so
 * position is worked out from it once the pane has a width to work
 * with, and read back out of it whenever the handle is dragged.
 */
static void
on_agenda_paned_changed (GtkPaned      *paned,
                         GParamSpec *ps G_GNUC_UNUSED,
                         gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  gint width = gtk_widget_get_width (GTK_WIDGET (paned));
  gint pos = gtk_paned_get_position (paned);

  if (width <= 0)
    return;

  if (self->agenda_width_pending) {
    self->agenda_width_pending = FALSE;
    gtk_paned_set_position (paned, width - self->agenda_width);
    return;
  }

  if (width - pos > 0)
    self->agenda_width = width - pos;
}

typedef struct {
  const gchar *action;
  const gchar *shortcut;
} Shortcut;

static const Shortcut MailShortcuts[] = {
  { "mail.composer-new", "<primary>n" },
  { "mail.forward-current", "<primary><shift>f" },
  { "mail.reply-all-current", "<primary><shift>r" },
  { "mail.reply-current", "<primary>r" },
  { "mail.mark-flag-current", "<primary>s" },
  { "mail.mark-read-current", "<primary><shift>i" },
  { "mail.mark-unflag-current", "<primary><shift>s" },
  { "mail.mark-unread-current", "<primary><shift>u" },
  { "mail.print-current", "<primary>p" },
  { "mail.trash", "<primary>d" },
  { "mail.trash", "Delete" },
  { "mail.junk", "<primary>j" },
  { "mail.view-source", "<primary>s" },
};

static void
stamp_mail_view_navigate_back (StampMailView *self)
{
  const gchar *layout = adw_multi_layout_view_get_layout_name (self->mail_layout);

  if (g_strcmp0 (layout, "mobile") == 0)
    adw_navigation_view_pop (self->mobile_nav);
}

void
stamp_mail_view_init (StampMailView *self)
{
  GtkEventController *controller;

  /* The dense agenda's date chooser reaches for the clock as it is
   * built, so the context has to exist before the template does. */
  GcalContext *context = stamp_gcal_ensure_context ();

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (gcal_context_get_clock (context), "day-changed",
                           G_CALLBACK (on_day_changed), self, G_CONNECT_DEFAULT);

  /* The sidebar's content reaches the bottom edge so that folder names
   * stay visible in the gap beside the agenda's tab, which would leave
   * the last of them stranded behind the card. A spacer at the end of
   * the list holds that room open, and a size group keeps it exactly as
   * tall as the card's body -- including through the reveal animation,
   * which changes that height on every frame. */
  self->agenda_clearance = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
  gtk_size_group_add_widget (self->agenda_clearance, stamp_sidebar_agenda_get_body (self->sidebar_agenda));
  gtk_size_group_add_widget (self->agenda_clearance, self->folder_spacer);

  self->size = SIZE_DESKTOP;
  self->layout = STAMP_MAIL_LAYOUT_SIDE_BY_SIDE;
  self->agenda_width = 320;
  update_layout_name (self);

  /* Starting in the layout the view already had leaves no notify to
   * hang this off, so place the controls once up front. */
  update_window_controls (self, layout_name_for (self));

  self->actions = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->actions),
                                   stamp_mail_view_action_entries,
                                   G_N_ELEMENTS (stamp_mail_view_action_entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "mail", G_ACTION_GROUP (self->actions));

  /* Note: Blueprint currently (January 2026) does not handled GtkNamedActions, so add it in code */
  controller = gtk_shortcut_controller_new ();
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  for (guint idx = 0; idx < G_N_ELEMENTS (MailShortcuts); idx++) {
    GtkShortcut *shortcut;

    shortcut = gtk_shortcut_new (gtk_shortcut_trigger_parse_string (MailShortcuts[idx].shortcut), gtk_named_action_new (MailShortcuts[idx].action));
    gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);
  }

  g_signal_connect (self->mail_layout, "notify::layout-name", G_CALLBACK (on_layout_changed), self);

  self->saved_paned_pos = 360;
  self->saved_stacked_pos = 300;

  g_signal_connect (stamp_conversation_list_get_sidebar_button (self->conversation_list), "clicked", G_CALLBACK (on_toggle_sidebar), self);

  stamp_folder_list_set_conversation_list (self->folder_list, self->conversation_list);

  /* Remember paned position */
  g_signal_connect (self->desktop_paned, "notify::position", G_CALLBACK (on_paned_changed), self);
  g_signal_connect (self->tablet_paned, "notify::position", G_CALLBACK (on_paned_changed), self);
  g_signal_connect (self->stacked_paned, "notify::position", G_CALLBACK (on_stacked_paned_changed), self);
  g_signal_connect (self->dense_paned, "notify::position", G_CALLBACK (on_stacked_paned_changed), self);
  g_signal_connect (self->tablet_stacked_paned, "notify::position", G_CALLBACK (on_stacked_paned_changed), self);
  g_signal_connect (self->dense_agenda_paned, "notify::position", G_CALLBACK (on_agenda_paned_changed), self);

  /* The width to work the position out from only exists once the pane
   * has been allocated, which is what moves max-position. */
  g_signal_connect (self->dense_agenda_paned, "notify::max-position", G_CALLBACK (on_agenda_paned_changed), self);

  /* Synchronize button state with show-sidebar */
  g_signal_connect_swapped (self->tablet_osv, "notify::show-sidebar", G_CALLBACK (on_sidebar_visibility_changed), self);
  g_signal_connect_swapped (self->mobile_osv, "notify::show-sidebar", G_CALLBACK (on_sidebar_visibility_changed), self);

  g_signal_connect_swapped (self->message_list, "switch-conversation",
                            G_CALLBACK (stamp_conversation_list_select_relative), self->conversation_list);
  g_signal_connect_swapped (self->message_list, "navigate-back",
                            G_CALLBACK (stamp_mail_view_navigate_back), self);
}

GtkWidget *
stamp_mail_view_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_VIEW, NULL);
}

void
stamp_mail_view_search_contact (StampMailView *self,
                                const gchar   *mail)
{
  stamp_mail_conversation_list_search_contact (self->conversation_list, mail);

  if (is_mobile_view (self))
    adw_navigation_view_pop_to_tag (self->mobile_nav, "main");
}

GSimpleActionGroup *
stamp_mail_view_get_action_group (StampMailView *self)
{
  return self->actions;
}

void
stamp_mail_view_show_toast (StampMailView *self,
                            const gchar   *message)
{
  AdwToast *toast;

  toast = adw_toast_new (message);
  adw_toast_overlay_add_toast (self->toast_overlay, toast);
}

StampConversationList *
stamp_mail_view_get_conversation_list (StampMailView *self)
{
  return self->conversation_list;
}

GtkWidget *
stamp_mail_view_get_sidebar (StampMailView *self)
{
  return self->sidebar_pane;
}

GtkWidget *
stamp_mail_view_get_primary_action (StampMailView *self)
{
  return self->compose_button;
}

void
stamp_mail_view_set_layout (StampMailView   *self,
                            StampMailLayout  layout)
{
  g_return_if_fail (STAMP_IS_MAIL_VIEW (self));

  if (self->layout == layout)
    return;

  self->layout = layout;
  update_layout_name (self);
}

StampMailLayout
stamp_mail_view_get_layout (StampMailView *self)
{
  g_return_val_if_fail (STAMP_IS_MAIL_VIEW (self), STAMP_MAIL_LAYOUT_SIDE_BY_SIDE);

  return self->layout;
}
