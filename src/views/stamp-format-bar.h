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

#include "stamp-webview.h"

G_BEGIN_DECLS

#define STAMP_TYPE_FORMAT_BAR (stamp_format_bar_get_type ())

G_DECLARE_FINAL_TYPE (StampFormatBar, stamp_format_bar, STAMP, FORMAT_BAR, GtkWidget);

GtkWidget *
stamp_format_bar_new (void);

void
stamp_format_bar_set_webview (StampFormatBar *self,
                              StampWebView   *webview);

StampWebView *
stamp_format_bar_get_webview (StampFormatBar *self);

void
stamp_format_bar_update_actions (StampFormatBar *self);

void
stamp_format_bar_undo (StampFormatBar *self);

void
stamp_format_bar_redo (StampFormatBar *self);

G_END_DECLS
