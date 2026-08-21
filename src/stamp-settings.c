/*
 * Copyright 2024-2026 Jan-Michael Brummer
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

#include "stamp-config.h"

#include "stamp-settings.h"

#include "stamp-profile-manager.h"

#include <gio/gio.h>
#include <glib.h>

static GHashTable *settings = NULL;

/* Indexed by StampMailLayout. */
static const gchar *layout_nicks[] = {
  "side-by-side",
  "stacked",
  "dense",
};

const gchar *
stamp_mail_layout_to_nick (StampMailLayout layout)
{
  if (layout >= G_N_ELEMENTS (layout_nicks))
    return layout_nicks[STAMP_MAIL_LAYOUT_SIDE_BY_SIDE];

  return layout_nicks[layout];
}

StampMailLayout
stamp_mail_layout_from_nick (const gchar *nick)
{
  if (nick) {
    for (guint i = 0; i < G_N_ELEMENTS (layout_nicks); i++) {
      if (g_strcmp0 (nick, layout_nicks[i]) == 0)
        return (StampMailLayout)i;
    }
  }

  return STAMP_MAIL_LAYOUT_SIDE_BY_SIDE;
}

gboolean
stamp_mail_layout_nick_is_valid (const gchar *nick)
{
  if (!nick || *nick == '\0')
    return FALSE;

  for (guint i = 0; i < G_N_ELEMENTS (layout_nicks); i++) {
    if (g_strcmp0 (nick, layout_nicks[i]) == 0)
      return TRUE;
  }

  return FALSE;
}

GSettings *
stamp_settings_get (const gchar *schema)
{
  GSettings *gsettings = NULL;

  if (settings) {
    gsettings = g_hash_table_lookup (settings, schema);
    if (gsettings)
      return gsettings;
  }

  gsettings = g_settings_new (schema);
  if (!gsettings)
    g_warning ("%s: Invalid schema %s requested", G_STRFUNC, schema);
  else
    g_hash_table_insert (settings, g_strdup (schema), gsettings);

  return gsettings;
}

/*
 * Whether the mail list groups mails by subject right now: what the
 * active profile asks for, else what the preferences say. There is no
 * hand-picked tier the way there is for the layout, because this is not
 * offered anywhere a profile could be overridden by a single click.
 */
gboolean
stamp_mail_clustering_enabled (void)
{
  StampProfile *active = stamp_profile_manager_get_active (stamp_profile_manager_get_default ());
  const gchar *override = active ? stamp_profile_get_clustering (active) : NULL;

  if (override)
    return g_strcmp0 (override, "on") == 0;

  return g_settings_get_boolean (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_CLUSTERED);
}

guint
stamp_mail_cluster_window_days (void)
{
  return g_settings_get_uint (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_CLUSTER_WINDOW);
}

/*
 * The badges follow clustering exactly: the active profile decides if
 * it was told to, otherwise the preferences do.
 */
gboolean
stamp_mail_badge_enabled (void)
{
  StampProfile *active = stamp_profile_manager_get_active (stamp_profile_manager_get_default ());
  const gchar *override = active ? stamp_profile_get_mail_badge (active) : NULL;

  if (override)
    return g_strcmp0 (override, "on") == 0;

  return g_settings_get_boolean (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_SHOW_BADGE);
}

gboolean
stamp_calendar_badge_enabled (void)
{
  StampProfile *active = stamp_profile_manager_get_active (stamp_profile_manager_get_default ());
  const gchar *override = active ? stamp_profile_get_calendar_badge (active) : NULL;

  if (override)
    return g_strcmp0 (override, "on") == 0;

  return g_settings_get_boolean (STAMP_SETTINGS_CALENDAR, STAMP_PREFS_CALENDAR_SHOW_BADGE);
}

void
stamp_settings_init (void)
{
  settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

void
stamp_settings_shutdown (void)
{
  if (settings) {
    g_hash_table_remove_all (settings);
    g_clear_pointer (&settings, g_hash_table_unref);
  }
}
