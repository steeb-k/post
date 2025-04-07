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

#include <gtk/gtk.h>

#include "stamp-account.h"

G_BEGIN_DECLS

#define STAMP_TYPE_TAG (stamp_tag_get_type ())
G_DECLARE_FINAL_TYPE (StampTag, stamp_tag, STAMP, TAG, GtkBox);

GtkWidget *
stamp_tag_new (StampAccount *account);

void
stamp_tag_set_label (StampTag   *self,
                     const char *label);

void
stamp_tag_set_email (StampTag   *self,
                     const char *mail);

const char *
stamp_tag_get_label (StampTag *self);

const char *
stamp_tag_get_email (StampTag *self);

void
stamp_tag_set_show_button (StampTag *self,
                           gboolean  show);

void
stamp_tag_set_show_email (StampTag *self,
                          gboolean  show);

void
stamp_tag_set_show_avatar (StampTag *self,
                           gboolean  show);

G_END_DECLS

