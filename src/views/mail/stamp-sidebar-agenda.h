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

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define STAMP_TYPE_SIDEBAR_AGENDA (stamp_sidebar_agenda_get_type ())

G_DECLARE_FINAL_TYPE (StampSidebarAgenda, stamp_sidebar_agenda, STAMP, SIDEBAR_AGENDA, GtkBox)

/*
 * The opaque part of the card, below the tab. Whatever scrolls beneath
 * the strip has to leave this much room at its end, and a vertical
 * GtkSizeGroup is how that is kept in step.
 */
GtkWidget *
stamp_sidebar_agenda_get_body (StampSidebarAgenda *self);

/*
 * Whether the strip shows at all. Layouts that put the agenda somewhere
 * else, and widths too narrow to spare the room, turn it off.
 */
void
stamp_sidebar_agenda_set_enabled (StampSidebarAgenda *self,
                                  gboolean            enabled);

G_END_DECLS
