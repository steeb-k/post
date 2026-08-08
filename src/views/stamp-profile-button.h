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

#define STAMP_TYPE_PROFILE_BUTTON (stamp_profile_button_get_type ())

G_DECLARE_FINAL_TYPE (StampProfileButton, stamp_profile_button, STAMP, PROFILE_BUTTON, AdwBin);

GtkWidget *
stamp_profile_button_new (void);

/*
 * Dresses @avatar as @profile's badge, or as the "Show All" badge when
 * @profile is NULL. Shared so the switcher and the management dialog
 * render a profile identically.
 */
void
stamp_profile_button_style_avatar (AdwAvatar    *avatar,
                                   StampProfile *profile);

G_END_DECLS
