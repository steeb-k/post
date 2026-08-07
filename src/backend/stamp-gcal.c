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
 * Shared entry point to the vendored GNOME Calendar backend. GNOME
 * Calendar builds its GcalContext in GcalApplication; Post has no such
 * singleton, and several unrelated places (the calendar view, mail RSVP
 * replies, the .ics importer, the today badge) need the same context.
 * They all go through here, whichever one runs first.
 */

#include "stamp-gcal.h"

#include "gcal-calendar.h"
#include "gcal-global.h"
#include "gcal-manager.h"

GcalContext *
stamp_gcal_ensure_context (void)
{
  GcalContext *context = gcal_get_default_context ();

  if (context)
    return context;

  context = gcal_context_new ();
  gcal_set_default_context (context);
  gcal_context_startup (context);

  return context;
}

/*
 * The calendar events end up in when Post has no better idea: the
 * configured default, unless that one cannot be written to.
 */
GcalCalendar *
stamp_gcal_get_writable_calendar (void)
{
  GcalContext *context = stamp_gcal_ensure_context ();
  GcalManager *manager = gcal_context_get_manager (context);
  GcalCalendar *calendar = gcal_manager_get_default_calendar (manager);
  g_autoptr (GList) calendars = NULL;

  if (calendar && !gcal_calendar_is_read_only (calendar))
    return calendar;

  calendars = gcal_manager_get_calendars (manager);
  for (GList *l = calendars; l; l = l->next) {
    if (!gcal_calendar_is_read_only (l->data))
      return l->data;
  }

  return NULL;
}

/*
 * Invitations should land in the calendar of the account that received
 * them, so that the reply travels back over the same collection. Falls
 * back to any writable calendar when the account has none.
 */
GcalCalendar *
stamp_gcal_get_calendar_for_collection (const gchar *collection_uid)
{
  GcalContext *context = stamp_gcal_ensure_context ();
  GcalManager *manager = gcal_context_get_manager (context);
  g_autoptr (GList) calendars = NULL;

  if (collection_uid) {
    calendars = gcal_manager_get_calendars (manager);

    for (GList *l = calendars; l; l = l->next) {
      GcalCalendar *calendar = l->data;
      ESource *parent = gcal_calendar_get_parent_source (calendar);

      if (gcal_calendar_is_read_only (calendar))
        continue;

      if (parent && g_strcmp0 (e_source_get_uid (parent), collection_uid) == 0)
        return calendar;
    }
  }

  return stamp_gcal_get_writable_calendar ();
}
