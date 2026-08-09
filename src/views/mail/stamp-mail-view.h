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

#pragma once

#include "stamp-settings.h"

G_BEGIN_DECLS

typedef struct _StampConversationList StampConversationList;

#define STAMP_TYPE_MAIL_VIEW (stamp_mail_view_get_type ())

G_DECLARE_FINAL_TYPE (StampMailView, stamp_mail_view, STAMP, MAIL_VIEW, AdwBreakpointBin);

GtkWidget *
stamp_mail_view_new (void);

GtkWidget *
stamp_mail_view_get_sidebar (StampMailView *self);

GtkWidget *
stamp_mail_view_get_primary_action (StampMailView *self);

void
stamp_mail_view_search_contact (StampMailView *self,
                                const gchar    *mail);

GSimpleActionGroup *
stamp_mail_view_get_action_group (StampMailView *self);

void
stamp_mail_view_show_toast (StampMailView *self,
                            const gchar    *message);

StampConversationList *
stamp_mail_view_get_conversation_list (StampMailView *self);

/*
 * How the panes are arranged. The window decides which layout applies;
 * the view only knows how to wear it, and narrows it down to what fits
 * the width it has.
 */
void
stamp_mail_view_set_layout (StampMailView   *self,
                            StampMailLayout  layout);

StampMailLayout
stamp_mail_view_get_layout (StampMailView *self);

G_END_DECLS

