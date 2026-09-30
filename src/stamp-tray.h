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

#include <gio/gio.h>

G_BEGIN_DECLS

#define STAMP_TYPE_TRAY (stamp_tray_get_type ())

G_DECLARE_FINAL_TYPE (StampTray, stamp_tray, STAMP, TRAY, GObject);

/*
 * Nothing is shown until the tray is enabled, and nothing at all on a
 * desktop that has no tray.
 *
 * "activate" is emitted when the window should be shown or hidden,
 * with an activation token if the desktop passed one on, and "quit"
 * when the menu asks to leave.
 */
StampTray *
stamp_tray_new (const gchar *application_id);

void
stamp_tray_set_enabled (StampTray *self,
                        gboolean   enabled);

/*
 * The icon as pixels: an a(iiay) of width, height and ARGB32 data in
 * network byte order, one entry for each size on offer. Without it the
 * desktop is left to look up "<application_id>-symbolic" by name.
 */
void
stamp_tray_set_icon (StampTray *self,
                     GVariant  *pixmaps);

/*
 * Whether the window is on screen, which is what decides if the menu
 * offers to show it or to hide it.
 */
void
stamp_tray_set_window_shown (StampTray *self,
                             gboolean   shown);

G_END_DECLS
