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

#pragma once

#include <adwaita.h>

#include "stamp-mail-view.h"

G_BEGIN_DECLS

#define STAMP_TYPE_WINDOW (stamp_window_get_type())

G_DECLARE_FINAL_TYPE (StampWindow, stamp_window, STAMP, WINDOW, AdwApplicationWindow);

void
stamp_window_search_contact (StampWindow *self,
                             const gchar  *mail);

void
stamp_window_show_contact (StampWindow *self,
                           const gchar  *mail);

StampMailView *
stamp_window_get_mail_view (StampWindow *self);

StampWindow *
stamp_get_main_window (void);

void
stamp_window_show_mail_view (StampWindow *self);

G_END_DECLS
