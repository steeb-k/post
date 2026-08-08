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

#include <glib-object.h>

G_BEGIN_DECLS

#define STAMP_TYPE_PROFILE (stamp_profile_get_type ())

G_DECLARE_FINAL_TYPE (StampProfile, stamp_profile, STAMP, PROFILE, GObject);

/* Weekdays as a bitmask, Monday first to match how the schedule reads. */
typedef enum {
  STAMP_PROFILE_MONDAY    = 1 << 0,
  STAMP_PROFILE_TUESDAY   = 1 << 1,
  STAMP_PROFILE_WEDNESDAY = 1 << 2,
  STAMP_PROFILE_THURSDAY  = 1 << 3,
  STAMP_PROFILE_FRIDAY    = 1 << 4,
  STAMP_PROFILE_SATURDAY  = 1 << 5,
  STAMP_PROFILE_SUNDAY    = 1 << 6,
} StampProfileWeekday;

#define STAMP_PROFILE_ALL_DAYS  0x7f
#define STAMP_PROFILE_WEEKDAYS  0x1f
#define STAMP_PROFILE_MINUTES_PER_DAY 1440

/*
 * One span of the week the profile claims. @days says which days the
 * span starts on; @start_minute and @end_minute are minutes since
 * midnight. A rule whose end is not after its start wraps past midnight
 * into the following day.
 */
typedef struct {
  guint8  days;
  guint16 start_minute;
  guint16 end_minute;
} StampProfileRule;

/*
 * Setup
 */

StampProfile *
stamp_profile_new (const gchar *id,
                   const gchar *name,
                   const gchar *color);

StampProfile *
stamp_profile_copy (StampProfile *self);

/*
 * Getter/Setter
 */

const gchar *
stamp_profile_get_id (StampProfile *self);

const gchar *
stamp_profile_get_name (StampProfile *self);

void
stamp_profile_set_name (StampProfile *self,
                        const gchar  *name);

const gchar *
stamp_profile_get_color (StampProfile *self);

void
stamp_profile_set_color (StampProfile *self,
                         const gchar  *color);

/*
 * The letter shown in the switcher. Owned by @self.
 */
const gchar *
stamp_profile_get_initial (StampProfile *self);

/*
 * Accounts
 */

const gchar * const *
stamp_profile_get_accounts (StampProfile *self);

void
stamp_profile_set_accounts (StampProfile        *self,
                            const gchar * const *account_uids);

gboolean
stamp_profile_has_account (StampProfile *self,
                           const gchar  *account_uid);

void
stamp_profile_add_account (StampProfile *self,
                           const gchar  *account_uid);

void
stamp_profile_remove_account (StampProfile *self,
                              const gchar  *account_uid);

/*
 * Schedule
 */

guint
stamp_profile_get_n_rules (StampProfile *self);

gboolean
stamp_profile_get_rule (StampProfile     *self,
                        guint             index,
                        StampProfileRule *rule);

void
stamp_profile_add_rule (StampProfile           *self,
                        const StampProfileRule *rule);

void
stamp_profile_set_rule (StampProfile           *self,
                        guint                   index,
                        const StampProfileRule *rule);

void
stamp_profile_remove_rule (StampProfile *self,
                           guint         index);

/*
 * Whether any rule of @self covers @when.
 */
gboolean
stamp_profile_matches_time (StampProfile *self,
                            GDateTime    *when);

/*
 * A human readable rendering of the schedule, or NULL when the profile
 * has no rules.
 */
gchar *
stamp_profile_dup_schedule_summary (StampProfile *self);

/*
 * The abbreviated name of a weekday, counting Monday as 0.
 */
const gchar *
stamp_profile_get_weekday_label (gint day);

/*
 * Serialization, as the (sssasa(yqq)) the profiles setting stores.
 */

GVariant *
stamp_profile_to_variant (StampProfile *self);

StampProfile *
stamp_profile_new_from_variant (GVariant *variant);

/*
 * Colors
 */

typedef struct {
  const gchar *id;
  const gchar *name;
  const gchar *hex;
} StampProfileColor;

const StampProfileColor *
stamp_profile_get_palette (guint *n_colors);

/*
 * Falls back to the first palette entry for an unknown @color_id, so a
 * profile always renders as something.
 */
const StampProfileColor *
stamp_profile_find_color (const gchar *color_id);

G_END_DECLS
