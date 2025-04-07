/*
 * Copyright 2025 Jan-Michael Brummer
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

#include <gtk/gtk.h>

#include "stamp-account.h"
#include "stamp-item.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_BOOK_ACCOUNT_ITEM (stamp_book_account_item_get_type ())

G_DECLARE_FINAL_TYPE (StampBookAccountItem, stamp_book_account_item, STAMP, BOOK_ACCOUNT_ITEM, StampItem);

StampBookAccountItem *
stamp_book_account_item_new (StampAccount *account);

void
stamp_book_account_item_load (StampBookAccountItem *self);

G_END_DECLS

