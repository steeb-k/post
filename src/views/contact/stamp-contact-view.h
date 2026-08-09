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

#include <adwaita.h>
#include <gtk/gtk.h>

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_CONTACT_VIEW (stamp_contact_view_get_type ())

G_DECLARE_FINAL_TYPE (StampContactView, stamp_contact_view, STAMP, CONTACT_VIEW, AdwBreakpointBin);

GtkWidget *
stamp_contact_view_new (void);

GtkWidget *
stamp_contact_view_get_sidebar (StampContactView *self);

GtkWidget *
stamp_contact_view_get_primary_action (StampContactView *self);

void
stamp_contact_view_show_contact (StampContactView *self,
                                 const gchar       *mail);

G_END_DECLS

