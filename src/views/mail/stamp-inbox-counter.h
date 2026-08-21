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

#include <glib-object.h>

G_BEGIN_DECLS

#define STAMP_TYPE_INBOX_COUNTER (stamp_inbox_counter_get_type ())

G_DECLARE_FINAL_TYPE (StampInboxCounter, stamp_inbox_counter, STAMP, INBOX_COUNTER, GObject);

StampInboxCounter *
stamp_inbox_counter_new (void);

/*
 * How many unread mails are sitting in the inboxes of the accounts the
 * active profile shows. Watch "count" to follow it.
 */
guint
stamp_inbox_counter_get_count (StampInboxCounter *self);

G_END_DECLS
