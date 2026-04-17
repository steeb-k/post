/*
 * Copyright 2025-2026 Jan-Michael Brummer
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

#include "stamp-week-view.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

struct _StampWeekView {
  GtkBox parent_instance;

  GtkWidget *avatar;
  GtkWidget *name;
  GtkWidget *stack;

  GtkWidget *mail;
  GtkWidget *phone;
  GtkWidget *birthday;
  GtkWidget *address;

  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (StampWeekView, stamp_week_view, GTK_TYPE_BOX);

static void
stamp_week_view_init (StampWeekView *self)
{
}

static void
stamp_week_view_constructed (GObject *object)
{
  StampWeekView *self = STAMP_WEEK_VIEW (object);

  G_OBJECT_CLASS (stamp_week_view_parent_class)->constructed (object);

  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();
}

static void
stamp_week_view_dispose (GObject *object)
{
  /* StampContactDetails *self = STAMP_CONTACT_DETAILS (object); */

  G_OBJECT_CLASS (stamp_week_view_parent_class)->dispose (object);
}

static void
stamp_week_view_class_init (StampWeekViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/calendar/week-view/stamp-week-view.ui");

  object_class->constructed = stamp_week_view_constructed;
  object_class->dispose = stamp_week_view_dispose;

  /* gtk_widget_class_bind_template_child (widget_class, StampContactDetails, avatar); */
}

GtkWidget *
stamp_week_view_new (void)
{
  return g_object_new (STAMP_TYPE_WEEK_VIEW, NULL);
}
