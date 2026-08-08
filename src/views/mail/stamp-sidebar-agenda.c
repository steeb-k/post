/*
 * Copyright 2024-2026 Jan-Michael Brummer
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

/*
 * The agenda strip at the bottom of the mail sidebar. Collapsed it is a
 * single day row - today's events, or nothing at all when the day is
 * empty. Expanded it also reveals the month grid, and the row follows
 * whichever day is picked there.
 *
 * Like StampTodayCounter this is a timeline subscriber in its own right:
 * the subscribed range is the selected day, the timeline keeps the model
 * fed, and GcalAgendaViewDay filters it down to the events that overlap
 * the day. The date chooser subscribes separately, and only while it is
 * revealed, so a collapsed sidebar does not pay for a month of queries.
 */

#include "stamp-sidebar-agenda.h"

#include <glib/gi18n.h>

#include "stamp-gcal.h"
#include "stamp-settings.h"
#include "stamp-window.h"

#include "gcal-agenda-view-day.h"
#include "gcal-agenda-view-day-row.h"
#include "gcal-clock.h"
#include "gcal-date-chooser.h"
#include "gcal-event-widget.h"
#include "gcal-global.h"
#include "gcal-manager.h"
#include "gcal-range.h"
#include "gcal-timeline.h"
#include "gcal-timeline-subscriber.h"
#include "gcal-view.h"

/* Enough for the day heading plus one event, so the strip never
 * degenerates into a scrollbar with nothing visible next to it. */
#define MIN_LIST_HEIGHT 72

struct _StampSidebarAgenda {
  GtkBox parent_instance;

  GtkWidget *tab;
  GtkWidget *body;
  GtkToggleButton *toggle_button;
  GtkRevealer *revealer;
  GcalDateChooser *date_chooser;
  GtkScrolledWindow *scrolled_window;
  GcalAgendaViewDayRow *day_row;

  GcalAgendaViewDay *day;
  GDateTime *date;
  GListModel *calendars;

  gboolean expanded;
  gboolean enabled;
  gboolean subscribed;
  gboolean chooser_subscribed;
};

enum {
  PROP_0,
  PROP_EXPANDED,
  PROP_ENABLED,
  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static void stamp_sidebar_agenda_subscriber_iface_init (GcalTimelineSubscriberInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (StampSidebarAgenda, stamp_sidebar_agenda, GTK_TYPE_BOX,
                               G_IMPLEMENT_INTERFACE (GCAL_TYPE_TIMELINE_SUBSCRIBER,
                                                      stamp_sidebar_agenda_subscriber_iface_init));

static GDateTime *
start_of_day (GDateTime *date)
{
  return g_date_time_new_local (g_date_time_get_year (date),
                                g_date_time_get_month (date),
                                g_date_time_get_day_of_month (date),
                                0, 0, 0);
}

/*
 * An empty day means no heading either: the toggle is all that is left.
 */
static void
update_day_row_visible (StampSidebarAgenda *self)
{
  guint events = g_list_model_get_n_items (G_LIST_MODEL (self->day));

  gtk_widget_set_visible (GTK_WIDGET (self->scrolled_window), events > 0);
}

/*
 * The strip may take half of the sidebar at most; the month grid and the
 * toggle come out of that share first, so it is the event list that
 * scrolls once there is more than fits.
 */
static void
update_list_height_cap (StampSidebarAgenda *self)
{
  GtkWidget *pane;
  int available;
  int used;

  pane = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_TOOLBAR_VIEW);
  if (!pane)
    return;

  available = gtk_widget_get_height (pane) / 2;
  if (available <= 0)
    return;

  gtk_widget_measure (self->tab, GTK_ORIENTATION_VERTICAL, -1, NULL, &used, NULL, NULL);
  available -= used;

  if (self->expanded) {
    gtk_widget_measure (GTK_WIDGET (self->date_chooser), GTK_ORIENTATION_VERTICAL, -1, NULL, &used, NULL, NULL);
    available -= used;
  }

  available = MAX (available, MIN_LIST_HEIGHT);

  if (gtk_scrolled_window_get_max_content_height (self->scrolled_window) != available)
    gtk_scrolled_window_set_max_content_height (self->scrolled_window, available);
}

/*
 * Without a calendar there is nothing to expand into, so the whole strip
 * goes away - and comes back by itself once an account brings one in.
 */
static void
update_visible (StampSidebarAgenda *self)
{
  gboolean have_calendars = self->calendars && g_list_model_get_n_items (self->calendars) > 0;

  gtk_widget_set_visible (GTK_WIDGET (self), self->enabled && have_calendars);
}

static void
set_date (StampSidebarAgenda *self,
          GDateTime          *date)
{
  g_autoptr (GDateTime) day_start = start_of_day (date);

  if (self->date && g_date_time_equal (self->date, day_start))
    return;

  g_clear_pointer (&self->date, g_date_time_unref);
  self->date = g_steal_pointer (&day_start);

  gcal_agenda_view_day_set_date (self->day, self->date);
  gcal_timeline_subscriber_range_changed (GCAL_TIMELINE_SUBSCRIBER (self));

  update_day_row_visible (self);
}

/*
 * The date chooser only earns its keep while it is on screen.
 */
static void
update_chooser_subscription (StampSidebarAgenda *self)
{
  GcalContext *context = gcal_get_default_context ();
  GcalTimeline *timeline;

  if (!context || self->chooser_subscribed == self->expanded)
    return;

  timeline = gcal_manager_get_timeline (gcal_context_get_manager (context));

  if (self->expanded)
    gcal_timeline_add_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->date_chooser));
  else
    gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->date_chooser));

  self->chooser_subscribed = self->expanded;
}

/*
 * Callbacks
 */

/* The wording and the arrow both live in the stack; this picks which. */
static gchar *
get_toggle_page (StampSidebarAgenda *self G_GNUC_UNUSED,
                 gboolean            expanded)
{
  return g_strdup (expanded ? "expanded" : "collapsed");
}

/* Collapsing also drops the strip back to today, which is worth saying
 * before it happens rather than after. */
static gchar *
get_toggle_tooltip (StampSidebarAgenda *self G_GNUC_UNUSED,
                    gboolean            expanded)
{
  return g_strdup (expanded ? _("Hide the calendar and go back to today")
                            : _("Show a calendar to pick another day"));
}

static void
on_day_selected (GcalDateChooser    *chooser,
                 StampSidebarAgenda *self)
{
  GDateTime *date = gcal_view_get_date (GCAL_VIEW (chooser));

  if (date)
    set_date (self, date);
}

static void
on_event_activated (GcalAgendaViewDayRow *day_row G_GNUC_UNUSED,
                    GcalEventWidget      *event_widget,
                    StampSidebarAgenda   *self G_GNUC_UNUSED)
{
  StampWindow *window = stamp_get_main_window ();

  if (window)
    stamp_window_show_event (window, gcal_event_widget_get_event (event_widget));
}

static void
on_day_items_changed (GListModel         *model G_GNUC_UNUSED,
                      guint               position,
                      guint               removed,
                      guint               added,
                      StampSidebarAgenda *self)
{
  update_day_row_visible (self);
}

static void
on_calendars_changed (GListModel         *model G_GNUC_UNUSED,
                      guint               position,
                      guint               removed,
                      guint               added,
                      StampSidebarAgenda *self)
{
  update_visible (self);
}

static void
on_day_changed (GcalClock          *clock G_GNUC_UNUSED,
                StampSidebarAgenda *self)
{
  g_autoptr (GDateTime) now = g_date_time_new_now_local ();

  /* Only follow the clock while the collapsed strip is showing today;
   * a day the user picked themselves stays picked. */
  if (!self->expanded)
    set_date (self, now);

  gcal_view_set_date (GCAL_VIEW (self->date_chooser), self->date);
}

/*
 * GcalTimelineSubscriber
 */

static GcalRange *
stamp_sidebar_agenda_get_range (GcalTimelineSubscriber *subscriber)
{
  StampSidebarAgenda *self = STAMP_SIDEBAR_AGENDA (subscriber);

  return gcal_range_new_take (g_date_time_ref (self->date),
                              g_date_time_add_days (self->date, 1),
                              GCAL_RANGE_DEFAULT);
}

static void
stamp_sidebar_agenda_set_model (GcalTimelineSubscriber *subscriber,
                                GListModel             *model)
{
  StampSidebarAgenda *self = STAMP_SIDEBAR_AGENDA (subscriber);

  gcal_agenda_view_day_set_model (self->day, model);
  update_day_row_visible (self);
}

static void
stamp_sidebar_agenda_subscriber_iface_init (GcalTimelineSubscriberInterface *iface)
{
  iface->get_range = stamp_sidebar_agenda_get_range;
  iface->set_model = stamp_sidebar_agenda_set_model;
}

/*
 * GObject
 */

static void
stamp_sidebar_agenda_constructed (GObject *object)
{
  StampSidebarAgenda *self = STAMP_SIDEBAR_AGENDA (object);
  GcalContext *context = stamp_gcal_ensure_context ();
  GcalManager *manager = gcal_context_get_manager (context);

  G_OBJECT_CLASS (stamp_sidebar_agenda_parent_class)->constructed (object);

  gcal_view_set_date (GCAL_VIEW (self->date_chooser), self->date);

  gcal_timeline_add_subscriber (gcal_manager_get_timeline (manager),
                                GCAL_TIMELINE_SUBSCRIBER (self));
  self->subscribed = TRUE;
  update_chooser_subscription (self);

  g_signal_connect_object (gcal_context_get_clock (context),
                           "day-changed",
                           G_CALLBACK (on_day_changed),
                           self,
                           G_CONNECT_DEFAULT);

  self->calendars = g_object_ref (gcal_manager_get_filtered_calendars_model (manager));
  g_signal_connect (self->calendars, "items-changed", G_CALLBACK (on_calendars_changed), self);

  g_settings_bind (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_AGENDA_EXPANDED,
                   self, "expanded", G_SETTINGS_BIND_DEFAULT);

  update_visible (self);
}

static void
stamp_sidebar_agenda_dispose (GObject *object)
{
  StampSidebarAgenda *self = STAMP_SIDEBAR_AGENDA (object);
  GcalContext *context = gcal_get_default_context ();

  if (context) {
    GcalTimeline *timeline = gcal_manager_get_timeline (gcal_context_get_manager (context));

    if (self->subscribed) {
      gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self));
      self->subscribed = FALSE;
    }

    if (self->chooser_subscribed) {
      gcal_timeline_remove_subscriber (timeline, GCAL_TIMELINE_SUBSCRIBER (self->date_chooser));
      self->chooser_subscribed = FALSE;
    }
  }

  if (self->calendars) {
    g_signal_handlers_disconnect_by_func (self->calendars, on_calendars_changed, self);
    g_clear_object (&self->calendars);
  }

  if (self->day) {
    g_signal_handlers_disconnect_by_func (self->day, on_day_items_changed, self);
    g_clear_object (&self->day);
  }

  g_clear_pointer (&self->date, g_date_time_unref);

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_SIDEBAR_AGENDA);

  G_OBJECT_CLASS (stamp_sidebar_agenda_parent_class)->dispose (object);
}

static void
stamp_sidebar_agenda_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  StampSidebarAgenda *self = STAMP_SIDEBAR_AGENDA (object);

  switch (property_id) {
    case PROP_EXPANDED:
      g_value_set_boolean (value, self->expanded);
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, self->enabled);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
stamp_sidebar_agenda_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  StampSidebarAgenda *self = STAMP_SIDEBAR_AGENDA (object);

  switch (property_id) {
    case PROP_EXPANDED: {
      gboolean expanded = g_value_get_boolean (value);

      if (self->expanded == expanded)
        break;

      self->expanded = expanded;

      /* Collapsing drops back to today: the strip's resting state is
       * "what is on today", not the last day that was browsed. */
      if (!expanded) {
        g_autoptr (GDateTime) now = g_date_time_new_now_local ();

        set_date (self, now);
        gcal_view_set_date (GCAL_VIEW (self->date_chooser), self->date);
      }

      update_chooser_subscription (self);
      update_list_height_cap (self);
      g_object_notify_by_pspec (object, properties[PROP_EXPANDED]);
      break;
    }

    case PROP_ENABLED:
      self->enabled = g_value_get_boolean (value);
      update_visible (self);
      g_object_notify_by_pspec (object, properties[PROP_ENABLED]);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
stamp_sidebar_agenda_class_init (StampSidebarAgendaClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (GCAL_TYPE_DATE_CHOOSER);
  g_type_ensure (GCAL_TYPE_AGENDA_VIEW_DAY_ROW);

  object_class->constructed = stamp_sidebar_agenda_constructed;
  object_class->dispose = stamp_sidebar_agenda_dispose;
  object_class->get_property = stamp_sidebar_agenda_get_property;
  object_class->set_property = stamp_sidebar_agenda_set_property;

  properties[PROP_EXPANDED] = g_param_spec_boolean ("expanded",
                                                    NULL,
                                                    NULL,
                                                    FALSE,
                                                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  properties[PROP_ENABLED] = g_param_spec_boolean ("enabled",
                                                   NULL,
                                                   NULL,
                                                   TRUE,
                                                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/mail/stamp-sidebar-agenda.ui");

  gtk_widget_class_bind_template_child (widget_class, StampSidebarAgenda, tab);
  gtk_widget_class_bind_template_child (widget_class, StampSidebarAgenda, body);
  gtk_widget_class_bind_template_child (widget_class, StampSidebarAgenda, toggle_button);
  gtk_widget_class_bind_template_child (widget_class, StampSidebarAgenda, revealer);
  gtk_widget_class_bind_template_child (widget_class, StampSidebarAgenda, date_chooser);
  gtk_widget_class_bind_template_child (widget_class, StampSidebarAgenda, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, StampSidebarAgenda, day_row);

  gtk_widget_class_bind_template_callback (widget_class, get_toggle_page);
  gtk_widget_class_bind_template_callback (widget_class, get_toggle_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, on_day_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_event_activated);

  /* Inherits the agenda styling the calendar view already ships. */
  gtk_widget_class_set_css_name (widget_class, "agenda-view");
}

static void
stamp_sidebar_agenda_init (StampSidebarAgenda *self)
{
  g_autoptr (GDateTime) now = NULL;

  self->enabled = TRUE;

  /* The date chooser reaches for the clock as it is built, so the
   * context has to exist before the template does. */
  stamp_gcal_ensure_context ();

  gtk_widget_init_template (GTK_WIDGET (self));

  now = g_date_time_new_now_local ();
  self->date = start_of_day (now);

  self->day = gcal_agenda_view_day_new ();
  gcal_agenda_view_day_set_date (self->day, self->date);
  g_signal_connect (self->day, "items-changed", G_CALLBACK (on_day_items_changed), self);

  gcal_agenda_view_day_row_set_day (self->day_row, self->day);

  update_day_row_visible (self);
}

GtkWidget *
stamp_sidebar_agenda_get_body (StampSidebarAgenda *self)
{
  g_return_val_if_fail (STAMP_IS_SIDEBAR_AGENDA (self), NULL);

  return self->body;
}

void
stamp_sidebar_agenda_set_enabled (StampSidebarAgenda *self,
                                  gboolean            enabled)
{
  g_return_if_fail (STAMP_IS_SIDEBAR_AGENDA (self));

  g_object_set (self, "enabled", enabled, NULL);
}
