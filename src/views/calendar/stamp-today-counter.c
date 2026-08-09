/*
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

/*
 * Counts the events that touch today, so the Calendar button in the
 * view switcher can carry a badge. It is a timeline subscriber like the
 * calendar views themselves, only without a widget attached: the
 * timeline keeps its model in sync, and we publish the item count.
 */

#include "stamp-today-counter.h"

#include "stamp-gcal.h"

#include "gcal-clock.h"
#include "gcal-global.h"
#include "gcal-manager.h"
#include "gcal-range.h"
#include "gcal-timeline.h"
#include "gcal-timeline-subscriber.h"

struct _StampTodayCounter {
  GObject parent_instance;

  GListModel *model;
  GDateTime *day_start;
  guint count;
  gboolean subscribed;
};

enum {
  PROP_0,
  PROP_COUNT,
  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static void stamp_today_counter_subscriber_iface_init (GcalTimelineSubscriberInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (StampTodayCounter, stamp_today_counter, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (GCAL_TYPE_TIMELINE_SUBSCRIBER,
                                                      stamp_today_counter_subscriber_iface_init));

static void
update_count (StampTodayCounter *self)
{
  guint count = self->model ? g_list_model_get_n_items (self->model) : 0;

  if (self->count == count)
    return;

  self->count = count;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
}

static void
on_model_items_changed (GListModel        *model,
                        guint              position,
                        guint              removed,
                        guint              added,
                        StampTodayCounter *self)
{
  update_count (self);
}

static void
on_day_changed (GcalClock         *clock,
                StampTodayCounter *self)
{
  g_autoptr (GDateTime) now = g_date_time_new_now_local ();

  g_clear_pointer (&self->day_start, g_date_time_unref);
  self->day_start = g_date_time_new_local (g_date_time_get_year (now),
                                           g_date_time_get_month (now),
                                           g_date_time_get_day_of_month (now),
                                           0, 0, 0);

  gcal_timeline_subscriber_range_changed (GCAL_TIMELINE_SUBSCRIBER (self));
}

/*
 * GcalTimelineSubscriber
 */

static GcalRange *
stamp_today_counter_get_range (GcalTimelineSubscriber *subscriber)
{
  StampTodayCounter *self = STAMP_TODAY_COUNTER (subscriber);

  return gcal_range_new_take (g_date_time_ref (self->day_start),
                              g_date_time_add_days (self->day_start, 1),
                              GCAL_RANGE_DEFAULT);
}

static void
stamp_today_counter_set_model (GcalTimelineSubscriber *subscriber,
                               GListModel             *model)
{
  StampTodayCounter *self = STAMP_TODAY_COUNTER (subscriber);

  if (self->model)
    g_signal_handlers_disconnect_by_func (self->model, on_model_items_changed, self);

  g_set_object (&self->model, model);

  if (self->model)
    g_signal_connect (self->model, "items-changed", G_CALLBACK (on_model_items_changed), self);

  update_count (self);
}

static void
stamp_today_counter_subscriber_iface_init (GcalTimelineSubscriberInterface *iface)
{
  iface->get_range = stamp_today_counter_get_range;
  iface->set_model = stamp_today_counter_set_model;
}

/*
 * GObject
 */

static void
stamp_today_counter_constructed (GObject *object)
{
  StampTodayCounter *self = STAMP_TODAY_COUNTER (object);
  GcalContext *context = stamp_gcal_ensure_context ();
  GcalManager *manager = gcal_context_get_manager (context);

  G_OBJECT_CLASS (stamp_today_counter_parent_class)->constructed (object);

  gcal_timeline_add_subscriber (gcal_manager_get_timeline (manager),
                                GCAL_TIMELINE_SUBSCRIBER (self));
  self->subscribed = TRUE;

  g_signal_connect_object (gcal_context_get_clock (context),
                           "day-changed",
                           G_CALLBACK (on_day_changed),
                           self,
                           G_CONNECT_DEFAULT);
}

static void
stamp_today_counter_dispose (GObject *object)
{
  StampTodayCounter *self = STAMP_TODAY_COUNTER (object);
  GcalContext *context = gcal_get_default_context ();

  if (self->subscribed && context) {
    GcalManager *manager = gcal_context_get_manager (context);

    gcal_timeline_remove_subscriber (gcal_manager_get_timeline (manager),
                                     GCAL_TIMELINE_SUBSCRIBER (self));
    self->subscribed = FALSE;
  }

  if (self->model) {
    g_signal_handlers_disconnect_by_func (self->model, on_model_items_changed, self);
    g_clear_object (&self->model);
  }

  g_clear_pointer (&self->day_start, g_date_time_unref);

  G_OBJECT_CLASS (stamp_today_counter_parent_class)->dispose (object);
}

static void
stamp_today_counter_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  StampTodayCounter *self = STAMP_TODAY_COUNTER (object);

  switch (property_id) {
    case PROP_COUNT:
      g_value_set_uint (value, self->count);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
stamp_today_counter_class_init (StampTodayCounterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = stamp_today_counter_constructed;
  object_class->dispose = stamp_today_counter_dispose;
  object_class->get_property = stamp_today_counter_get_property;

  properties[PROP_COUNT] = g_param_spec_uint ("count",
                                              NULL,
                                              NULL,
                                              0,
                                              G_MAXUINT,
                                              0,
                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
stamp_today_counter_init (StampTodayCounter *self)
{
  g_autoptr (GDateTime) now = g_date_time_new_now_local ();

  self->day_start = g_date_time_new_local (g_date_time_get_year (now),
                                           g_date_time_get_month (now),
                                           g_date_time_get_day_of_month (now),
                                           0, 0, 0);
}

StampTodayCounter *
stamp_today_counter_new (void)
{
  return g_object_new (STAMP_TYPE_TODAY_COUNTER, NULL);
}

guint
stamp_today_counter_get_count (StampTodayCounter *self)
{
  g_return_val_if_fail (STAMP_IS_TODAY_COUNTER (self), 0);

  return self->count;
}
