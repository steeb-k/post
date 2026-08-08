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

#include <gio/gio.h>

#include "stamp-profile.h"

G_BEGIN_DECLS

#define STAMP_TYPE_PROFILE_MANAGER (stamp_profile_manager_get_type ())

G_DECLARE_FINAL_TYPE (StampProfileManager, stamp_profile_manager, STAMP, PROFILE_MANAGER, GObject);

StampProfileManager *
stamp_profile_manager_get_default (void);

void
stamp_profile_manager_shutdown (void);

/*
 * The profiles, in the order they are shown. A #GListStore of
 * #StampProfile; watch its items-changed to follow additions and
 * removals.
 */
GListModel *
stamp_profile_manager_get_profiles (StampProfileManager *self);

StampProfile *
stamp_profile_manager_find_profile (StampProfileManager *self,
                                    const gchar         *id);

StampProfile *
stamp_profile_manager_add_profile (StampProfileManager *self,
                                   const gchar         *name,
                                   const gchar         *color);

void
stamp_profile_manager_remove_profile (StampProfileManager *self,
                                      StampProfile        *profile);

/*
 * Writes the profiles back out and re-resolves what should be active.
 * Call after editing a profile in place.
 */
void
stamp_profile_manager_save (StampProfileManager *self);

/*
 * Activation
 */

/*
 * The profile currently filtering the views, or NULL when everything is
 * shown.
 */
StampProfile *
stamp_profile_manager_get_active (StampProfileManager *self);

/*
 * Picks @profile by hand -- NULL for "Show All" -- overriding the
 * schedule until it next wants to change something.
 */
void
stamp_profile_manager_set_active (StampProfileManager *self,
                                  StampProfile        *profile);

/*
 * The profile that applies during hours no rule claims, or NULL for
 * "Show All".
 */
StampProfile *
stamp_profile_manager_get_default_profile (StampProfileManager *self);

void
stamp_profile_manager_set_default_profile (StampProfileManager *self,
                                           StampProfile        *profile);

/*
 * Whether a manual choice is currently overriding the schedule. Only
 * ever TRUE when there is a schedule to override.
 */
gboolean
stamp_profile_manager_get_overridden (StampProfileManager *self);

/*
 * Drops the manual choice and hands control back to the schedule.
 */
void
stamp_profile_manager_resume_schedule (StampProfileManager *self);

/*
 * Whether any profile carries a schedule rule at all.
 */
gboolean
stamp_profile_manager_has_schedule (StampProfileManager *self);

/*
 * Filtering
 */

/*
 * Whether the account is shown under the active profile. Always TRUE
 * when no profile is active.
 */
gboolean
stamp_profile_manager_shows_account (StampProfileManager *self,
                                     const gchar         *account_uid);

/*
 * Convenience for the same question against the default manager, for
 * the many call sites that only ever want this.
 */
gboolean
stamp_profile_shows_account (const gchar *account_uid);

G_END_DECLS
