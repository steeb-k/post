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

#include <adwaita.h>
#include <gtk/gtk.h>

#include "gcal-event.h"
#include "stamp-window.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_CALENDAR_VIEW (stamp_calendar_view_get_type ())

G_DECLARE_FINAL_TYPE (StampCalendarView, stamp_calendar_view, STAMP, CALENDAR_VIEW, AdwBreakpointBin);

GtkWidget *
stamp_calendar_view_new (void);

GtkWidget *
stamp_calendar_view_get_sidebar (StampCalendarView *self);

GtkWidget *
stamp_calendar_view_get_primary_action (StampCalendarView *self);

void
stamp_calendar_view_create_event (StampCalendarView *self,
                                  const gchar       *summary,
                                  const gchar       *description);

void
stamp_calendar_view_show_event (StampCalendarView *self,
                                GcalEvent         *event);

G_END_DECLS

