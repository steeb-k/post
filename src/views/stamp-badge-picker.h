/*
 * Copyright 2026 Jan-Michael Brummer
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

#include "stamp-profile.h"

G_BEGIN_DECLS

#define STAMP_TYPE_BADGE_PICKER (stamp_badge_picker_get_type ())

G_DECLARE_FINAL_TYPE (StampBadgePicker, stamp_badge_picker, STAMP, BADGE_PICKER, AdwDialog);

/*
 * A dialog showing every badge drawn in @profile's own colour, with the
 * one it wears already marked. Picking one emits "selected" with its id
 * and closes; the profile is left for the caller to change.
 */
GtkWidget *
stamp_badge_picker_new (StampProfile *profile);

G_END_DECLS
