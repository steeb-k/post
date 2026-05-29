/*
 * Copyright 2024-2026 Jan-Michael Brummer
 *
 * This file is part of Stamp.
 * Based on ephy-time-helpers.h from Epiphany (Copyright © 2002 Jorn Baayen)
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

#include <time.h>

#include <glib.h>

G_BEGIN_DECLS

gchar      *eel_strdup_strftime                        (const gchar *format,
                                                       struct tm *time_pieces);

gchar *
stamp_time_helpers_utf_friendly_time (time_t date, gboolean short_format);

G_END_DECLS
