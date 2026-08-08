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

/*
 * Owns the profiles and decides which one is active.
 *
 * Two things can claim the active profile: the schedule, and the user.
 * The schedule is a pure function of the clock -- the first profile
 * whose rules cover this minute, falling back to the profile the user
 * nominated as the default. A manual pick overrides that until the
 * schedule next wants to change something, which is what
 * next_transition() works out.
 *
 * Resolution runs off a once-a-minute tick rather than a timer armed
 * for the exact boundary, so a suspended laptop, a travelled timezone
 * or a stepped clock all heal themselves within a minute instead of
 * leaving a timer armed for a moment that already passed.
 */

#include "stamp-profile-manager.h"

#include "stamp-settings.h"

/* Long enough to cover every distinct state a weekly schedule can
 * reach, with a day to spare for the wrapping rules. */
#define TRANSITION_SEARCH_DAYS 8

/* Stands in for "the manual choice never expires", which is the case
 * while no profile carries a schedule at all. */
#define OVERRIDE_FOREVER G_MAXINT64

struct _StampProfileManager {
  GObject parent_instance;

  GListStore *profiles;
  GSettings *settings;

  /* Not a reference: the profiles store owns them. */
  StampProfile *active;

  gchar *default_id;
  gchar *override_id;
  gint64 override_until;

  guint tick_id;
  guint loading : 1;
};

enum {
  CHANGED,
  PROFILES_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_FINAL_TYPE (StampProfileManager, stamp_profile_manager, G_TYPE_OBJECT);

static StampProfileManager *default_manager = NULL;

static void resolve_active (StampProfileManager *self,
                            gboolean             force_notify);

/*
 * Auxiliary methods
 */

static StampProfile *
profile_at (StampProfileManager *self,
            guint                index)
{
  g_autoptr (StampProfile) profile = g_list_model_get_item (G_LIST_MODEL (self->profiles), index);

  /* The store holds a reference for as long as the profile is in it,
   * so handing back a borrowed pointer is safe. */
  return profile;
}

static guint
n_profiles (StampProfileManager *self)
{
  return g_list_model_get_n_items (G_LIST_MODEL (self->profiles));
}

/*
 * The profile the schedule asks for at @when, or NULL for "Show All".
 */
static StampProfile *
scheduled_profile_at (StampProfileManager *self,
                      GDateTime           *when)
{
  guint len = n_profiles (self);

  for (guint i = 0; i < len; i++) {
    StampProfile *profile = profile_at (self, i);

    if (stamp_profile_matches_time (profile, when))
      return profile;
  }

  return stamp_profile_manager_find_profile (self, self->default_id);
}

static gint
compare_int64 (gconstpointer a,
               gconstpointer b)
{
  gint64 lhs = *(const gint64 *)a;
  gint64 rhs = *(const gint64 *)b;

  return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
}

/*
 * When the schedule would next land on a different profile than it does
 * at @now. The schedule only ever changes on a rule edge, so gathering
 * the distinct edge minutes and trying each of them on each of the next
 * few days covers every moment it could turn over. Over-generating
 * candidates is harmless: one that resolves to the same profile is
 * simply skipped.
 */
static gint64
next_transition (StampProfileManager *self,
                 GDateTime           *now)
{
  g_autoptr (GArray) minutes = g_array_new (FALSE, FALSE, sizeof (guint16));
  g_autoptr (GArray) candidates = g_array_new (FALSE, FALSE, sizeof (gint64));
  g_autoptr (GDateTime) midnight = NULL;
  StampProfile *baseline = scheduled_profile_at (self, now);
  guint len = n_profiles (self);

  for (guint i = 0; i < len; i++) {
    StampProfile *profile = profile_at (self, i);
    guint n_rules = stamp_profile_get_n_rules (profile);

    for (guint r = 0; r < n_rules; r++) {
      StampProfileRule rule;

      if (!stamp_profile_get_rule (profile, r, &rule))
        continue;

      g_array_append_val (minutes, rule.start_minute);
      g_array_append_val (minutes, rule.end_minute);
    }
  }

  if (minutes->len == 0)
    return OVERRIDE_FOREVER;

  midnight = g_date_time_new_local (g_date_time_get_year (now),
                                    g_date_time_get_month (now),
                                    g_date_time_get_day_of_month (now),
                                    0, 0, 0);

  for (gint day = 0; day < TRANSITION_SEARCH_DAYS; day++) {
    g_autoptr (GDateTime) base = g_date_time_add_days (midnight, day);

    for (guint i = 0; i < minutes->len; i++) {
      guint16 minute = g_array_index (minutes, guint16, i);
      g_autoptr (GDateTime) candidate = g_date_time_add_minutes (base, minute);
      gint64 stamp;

      /* A DST spring-forward can swallow the wall-clock time this rule
       * edge names; there is simply no such moment on that day. */
      if (!candidate)
        continue;

      stamp = g_date_time_to_unix (candidate);
      if (stamp > g_date_time_to_unix (now))
        g_array_append_val (candidates, stamp);
    }
  }

  g_array_sort (candidates, compare_int64);

  for (guint i = 0; i < candidates->len; i++) {
    gint64 stamp = g_array_index (candidates, gint64, i);
    g_autoptr (GDateTime) when = g_date_time_new_from_unix_local (stamp);

    if (scheduled_profile_at (self, when) != baseline)
      return stamp;
  }

  /* Rules exist but never change what is active -- a single profile
   * covering the whole week, say. Nothing will ever reclaim control. */
  return OVERRIDE_FOREVER;
}

static void
store_profiles (StampProfileManager *self)
{
  GVariantBuilder builder;
  guint len = n_profiles (self);

  if (self->loading)
    return;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sssasa(yqq))"));

  for (guint i = 0; i < len; i++)
    g_variant_builder_add_value (&builder, stamp_profile_to_variant (profile_at (self, i)));

  g_settings_set_value (self->settings, STAMP_PREFS_PROFILES, g_variant_builder_end (&builder));
}

/*
 * The layout a profile overrides lives in a map of its own rather than
 * in the profile tuple, so that profiles written before layouts existed
 * still load: growing the tuple would change the key's type and make
 * GSettings drop every stored profile.
 */
static void
store_layouts (StampProfileManager *self)
{
  GVariantBuilder builder;
  guint len = n_profiles (self);

  if (self->loading)
    return;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));

  for (guint i = 0; i < len; i++) {
    StampProfile *profile = profile_at (self, i);
    const gchar *layout = stamp_profile_get_layout (profile);

    if (layout)
      g_variant_builder_add (&builder, "{ss}", stamp_profile_get_id (profile), layout);
  }

  g_settings_set_value (self->settings, STAMP_PREFS_PROFILE_LAYOUTS, g_variant_builder_end (&builder));
}

static void
load_layouts (StampProfileManager *self)
{
  g_autoptr (GVariant) layouts = NULL;
  guint len = n_profiles (self);

  layouts = g_settings_get_value (self->settings, STAMP_PREFS_PROFILE_LAYOUTS);

  for (guint i = 0; i < len; i++) {
    StampProfile *profile = profile_at (self, i);
    const gchar *layout = NULL;

    if (!g_variant_lookup (layouts, stamp_profile_get_id (profile), "&s", &layout))
      continue;

    /* A nick this version does not know is no override at all. */
    if (stamp_mail_layout_nick_is_valid (layout))
      stamp_profile_set_layout (profile, layout);
  }
}

static void
store_override (StampProfileManager *self)
{
  if (self->loading)
    return;

  g_settings_set_string (self->settings, STAMP_PREFS_OVERRIDE_PROFILE, self->override_id ? self->override_id : "");
  g_settings_set_int64 (self->settings, STAMP_PREFS_OVERRIDE_UNTIL, self->override_until);
}

static gboolean
override_is_live (StampProfileManager *self,
                  GDateTime           *now)
{
  if (self->override_until == 0)
    return FALSE;

  /* An override naming a profile that has since been deleted is stale;
   * an empty identifier legitimately means "Show All". */
  if (self->override_id && *self->override_id != '\0' &&
      !stamp_profile_manager_find_profile (self, self->override_id))
    return FALSE;

  return self->override_until == OVERRIDE_FOREVER ||
         g_date_time_to_unix (now) < self->override_until;
}

static void
resolve_active (StampProfileManager *self,
                gboolean             force_notify)
{
  g_autoptr (GDateTime) now = g_date_time_new_now_local ();
  StampProfile *active;

  if (override_is_live (self, now)) {
    active = stamp_profile_manager_find_profile (self, self->override_id);
  } else {
    /* Expired or stale: let the schedule have it back. */
    if (self->override_until != 0) {
      self->override_until = 0;
      g_clear_pointer (&self->override_id, g_free);
      store_override (self);
    }

    active = scheduled_profile_at (self, now);
  }

  if (active == self->active && !force_notify)
    return;

  self->active = active;

  g_signal_emit (self, signals[CHANGED], 0);
}

static gboolean
on_tick (gpointer user_data)
{
  StampProfileManager *self = STAMP_PROFILE_MANAGER (user_data);

  resolve_active (self, FALSE);

  return G_SOURCE_CONTINUE;
}

static void
load (StampProfileManager *self)
{
  g_autoptr (GVariant) profiles = NULL;
  GVariantIter iter;
  GVariant *child;

  self->loading = TRUE;

  profiles = g_settings_get_value (self->settings, STAMP_PREFS_PROFILES);
  g_variant_iter_init (&iter, profiles);

  while ((child = g_variant_iter_next_value (&iter))) {
    StampProfile *profile = stamp_profile_new_from_variant (child);

    if (profile)
      g_list_store_append (self->profiles, profile);

    g_clear_object (&profile);
    g_variant_unref (child);
  }

  load_layouts (self);

  self->default_id = g_settings_get_string (self->settings, STAMP_PREFS_DEFAULT_PROFILE);
  self->override_id = g_settings_get_string (self->settings, STAMP_PREFS_OVERRIDE_PROFILE);
  self->override_until = g_settings_get_int64 (self->settings, STAMP_PREFS_OVERRIDE_UNTIL);

  self->loading = FALSE;
}

/*
 * GObject
 */

static void
stamp_profile_manager_dispose (GObject *object)
{
  StampProfileManager *self = STAMP_PROFILE_MANAGER (object);

  g_clear_handle_id (&self->tick_id, g_source_remove);

  self->active = NULL;

  g_clear_object (&self->profiles);
  g_clear_object (&self->settings);
  g_clear_pointer (&self->default_id, g_free);
  g_clear_pointer (&self->override_id, g_free);

  G_OBJECT_CLASS (stamp_profile_manager_parent_class)->dispose (object);
}

static void
stamp_profile_manager_class_init (StampProfileManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_profile_manager_dispose;

  /* The active profile changed, or the accounts it shows did: whatever
   * filters on it needs to run again. */
  signals[CHANGED] = g_signal_new ("changed", G_OBJECT_CLASS_TYPE (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);

  /* A profile was added, removed or edited: anything showing the list
   * of profiles needs to rebuild. */
  signals[PROFILES_CHANGED] = g_signal_new ("profiles-changed", G_OBJECT_CLASS_TYPE (klass),
                                            G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL, NULL,
                                            G_TYPE_NONE, 0);
}

static void
stamp_profile_manager_init (StampProfileManager *self)
{
  self->profiles = g_list_store_new (STAMP_TYPE_PROFILE);
  self->settings = g_object_ref (STAMP_SETTINGS);

  load (self);
  resolve_active (self, TRUE);

  self->tick_id = g_timeout_add_seconds (60, on_tick, self);
}

StampProfileManager *
stamp_profile_manager_get_default (void)
{
  if (!default_manager)
    default_manager = g_object_new (STAMP_TYPE_PROFILE_MANAGER, NULL);

  return default_manager;
}

void
stamp_profile_manager_shutdown (void)
{
  g_clear_object (&default_manager);
}

/*
 * Profiles
 */

GListModel *
stamp_profile_manager_get_profiles (StampProfileManager *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE_MANAGER (self), NULL);

  return G_LIST_MODEL (self->profiles);
}

StampProfile *
stamp_profile_manager_find_profile (StampProfileManager *self,
                                    const gchar         *id)
{
  guint len;

  g_return_val_if_fail (STAMP_IS_PROFILE_MANAGER (self), NULL);

  if (!id || *id == '\0')
    return NULL;

  len = n_profiles (self);
  for (guint i = 0; i < len; i++) {
    StampProfile *profile = profile_at (self, i);

    if (g_strcmp0 (stamp_profile_get_id (profile), id) == 0)
      return profile;
  }

  return NULL;
}

StampProfile *
stamp_profile_manager_add_profile (StampProfileManager *self,
                                   const gchar         *name,
                                   const gchar         *color)
{
  g_autoptr (StampProfile) profile = NULL;

  g_return_val_if_fail (STAMP_IS_PROFILE_MANAGER (self), NULL);

  profile = stamp_profile_new (NULL, name, color);
  g_list_store_append (self->profiles, profile);

  stamp_profile_manager_save (self);

  return profile;
}

void
stamp_profile_manager_remove_profile (StampProfileManager *self,
                                      StampProfile        *profile)
{
  guint position;

  g_return_if_fail (STAMP_IS_PROFILE_MANAGER (self));
  g_return_if_fail (STAMP_IS_PROFILE (profile));

  if (!g_list_store_find (self->profiles, profile, &position))
    return;

  /* Drop every pointer to it first: resolve_active() must not be able
   * to hand back a profile that is on its way out. */
  if (g_strcmp0 (self->default_id, stamp_profile_get_id (profile)) == 0) {
    g_clear_pointer (&self->default_id, g_free);
    g_settings_set_string (self->settings, STAMP_PREFS_DEFAULT_PROFILE, "");
  }

  if (self->active == profile)
    self->active = NULL;

  g_list_store_remove (self->profiles, position);

  stamp_profile_manager_save (self);
}

void
stamp_profile_manager_save (StampProfileManager *self)
{
  g_return_if_fail (STAMP_IS_PROFILE_MANAGER (self));

  store_profiles (self);
  store_layouts (self);

  /* Editing the schedule can move the moment the manual choice was
   * meant to last until, so work it out again. */
  if (self->override_until != 0) {
    g_autoptr (GDateTime) now = g_date_time_new_now_local ();

    self->override_until = next_transition (self, now);
    store_override (self);
  }

  g_signal_emit (self, signals[PROFILES_CHANGED], 0);

  /* The account membership of the active profile may have changed
   * without the active profile itself changing. */
  resolve_active (self, TRUE);
}

/*
 * Activation
 */

StampProfile *
stamp_profile_manager_get_active (StampProfileManager *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE_MANAGER (self), NULL);

  return self->active;
}

void
stamp_profile_manager_set_active (StampProfileManager *self,
                                  StampProfile        *profile)
{
  g_autoptr (GDateTime) now = NULL;

  g_return_if_fail (STAMP_IS_PROFILE_MANAGER (self));

  now = g_date_time_new_now_local ();

  g_clear_pointer (&self->override_id, g_free);
  self->override_id = g_strdup (profile ? stamp_profile_get_id (profile) : "");
  self->override_until = next_transition (self, now);

  store_override (self);
  resolve_active (self, FALSE);
}

StampProfile *
stamp_profile_manager_get_default_profile (StampProfileManager *self)
{
  g_return_val_if_fail (STAMP_IS_PROFILE_MANAGER (self), NULL);

  return stamp_profile_manager_find_profile (self, self->default_id);
}

void
stamp_profile_manager_set_default_profile (StampProfileManager *self,
                                           StampProfile        *profile)
{
  const gchar *id = profile ? stamp_profile_get_id (profile) : "";

  g_return_if_fail (STAMP_IS_PROFILE_MANAGER (self));

  if (g_strcmp0 (self->default_id, id) == 0)
    return;

  g_set_str (&self->default_id, id);
  g_settings_set_string (self->settings, STAMP_PREFS_DEFAULT_PROFILE, id);

  resolve_active (self, FALSE);
}

gboolean
stamp_profile_manager_get_overridden (StampProfileManager *self)
{
  g_autoptr (GDateTime) now = NULL;

  g_return_val_if_fail (STAMP_IS_PROFILE_MANAGER (self), FALSE);

  /* Without a schedule there is nothing to override; the manual choice
   * is simply the choice. */
  if (!stamp_profile_manager_has_schedule (self))
    return FALSE;

  now = g_date_time_new_now_local ();

  return override_is_live (self, now);
}

void
stamp_profile_manager_resume_schedule (StampProfileManager *self)
{
  g_return_if_fail (STAMP_IS_PROFILE_MANAGER (self));

  if (self->override_until == 0)
    return;

  self->override_until = 0;
  g_clear_pointer (&self->override_id, g_free);

  store_override (self);
  resolve_active (self, FALSE);
}

gboolean
stamp_profile_manager_has_schedule (StampProfileManager *self)
{
  guint len;

  g_return_val_if_fail (STAMP_IS_PROFILE_MANAGER (self), FALSE);

  len = n_profiles (self);
  for (guint i = 0; i < len; i++) {
    if (stamp_profile_get_n_rules (profile_at (self, i)) > 0)
      return TRUE;
  }

  return FALSE;
}

/*
 * Filtering
 */

gboolean
stamp_profile_manager_shows_account (StampProfileManager *self,
                                     const gchar         *account_uid)
{
  g_return_val_if_fail (STAMP_IS_PROFILE_MANAGER (self), TRUE);

  if (!self->active)
    return TRUE;

  return stamp_profile_has_account (self->active, account_uid);
}

gboolean
stamp_profile_shows_account (const gchar *account_uid)
{
  return stamp_profile_manager_shows_account (stamp_profile_manager_get_default (), account_uid);
}
