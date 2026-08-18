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

#include "stamp-folder-color.h"

#include <gio/gio.h>

#include "stamp-palette.h"

struct _StampFolderColors {
  GObject parent_instance;

  /* account uid -> GSettings, and account uid -> GHashTable of folder
   * full name to colour id. The map is what the rows actually read: a
   * conversation row asks about every folder a mail sits in, once per
   * bind, and going through g_settings_get_value each time would parse
   * the same dictionary over and over while the list scrolls. */
  GHashTable *settings;
  GHashTable *colors;
};

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (StampFolderColors, stamp_folder_colors, G_TYPE_OBJECT);

static void
stamp_folder_colors_finalize (GObject *object)
{
  StampFolderColors *self = STAMP_FOLDER_COLORS (object);

  g_clear_pointer (&self->settings, g_hash_table_unref);
  g_clear_pointer (&self->colors, g_hash_table_unref);

  G_OBJECT_CLASS (stamp_folder_colors_parent_class)->finalize (object);
}

static void
stamp_folder_colors_class_init (StampFolderColorsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = stamp_folder_colors_finalize;

  signals[CHANGED] = g_signal_new ("changed",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 2,
                                   G_TYPE_STRING,
                                   G_TYPE_STRING);
}

static void
stamp_folder_colors_init (StampFolderColors *self)
{
  self->settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  self->colors = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_hash_table_unref);
}

StampFolderColors *
stamp_folder_colors_get_default (void)
{
  static StampFolderColors *instance = NULL;

  if (!instance)
    instance = g_object_new (STAMP_TYPE_FOLDER_COLORS, NULL);

  return instance;
}

static GHashTable *
read_colors (GSettings *settings)
{
  g_autoptr (GVariant) value = g_settings_get_value (settings, "folder-colors");
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  GVariantIter iter;
  const gchar *folder;
  const gchar *color;

  g_variant_iter_init (&iter, value);
  while (g_variant_iter_next (&iter, "{&s&s}", &folder, &color))
    g_hash_table_insert (table, g_strdup (folder), g_strdup (color));

  return table;
}

static void
on_settings_changed (GSettings   *settings,
                     const gchar *key,
                     gpointer     user_data)
{
  StampFolderColors *self = stamp_folder_colors_get_default ();
  const gchar *account_uid = user_data;

  g_hash_table_insert (self->colors, g_strdup (account_uid), read_colors (settings));

  /* No folder name: the whole account's colours were re-read, and
   * whoever cares has to look again anyway. */
  g_signal_emit (self, signals[CHANGED], 0, account_uid, NULL);
}

static GSettings *
account_settings (StampFolderColors *self,
                  const gchar       *account_uid)
{
  GSettings *settings = g_hash_table_lookup (self->settings, account_uid);
  g_autofree char *path = NULL;

  if (settings)
    return settings;

  path = g_strconcat ("/io/github/steeb_k/Post/mail/accounts/", account_uid, "/", NULL);
  settings = g_settings_new_with_path ("io.github.steeb_k.Post.mail.accounts", path);

  g_hash_table_insert (self->settings, g_strdup (account_uid), settings);
  g_hash_table_insert (self->colors, g_strdup (account_uid), read_colors (settings));

  g_signal_connect_data (settings, "changed::folder-colors",
                         G_CALLBACK (on_settings_changed),
                         g_strdup (account_uid),
                         (GClosureNotify)(GCallback)g_free,
                         0);

  return settings;
}

const gchar *
stamp_folder_color_lookup (const gchar *account_uid,
                           const gchar *full_name)
{
  StampFolderColors *self = stamp_folder_colors_get_default ();
  GHashTable *colors;
  const gchar *color_id;

  if (!account_uid || !full_name)
    return NULL;

  account_settings (self, account_uid);
  colors = g_hash_table_lookup (self->colors, account_uid);
  color_id = g_hash_table_lookup (colors, full_name);

  /* An id the palette has since dropped counts as no colour rather than
   * as the palette's first entry -- a folder should not silently turn
   * blue because a colour was renamed. */
  return stamp_palette_lookup (color_id) ? color_id : NULL;
}

void
stamp_folder_color_set (const gchar *account_uid,
                        const gchar *full_name,
                        const gchar *color_id)
{
  StampFolderColors *self = stamp_folder_colors_get_default ();
  GSettings *settings;
  GHashTable *colors;
  GVariantBuilder builder;
  GHashTableIter iter;
  gpointer key;
  gpointer value;

  g_return_if_fail (account_uid != NULL);
  g_return_if_fail (full_name != NULL);

  settings = account_settings (self, account_uid);
  colors = g_hash_table_lookup (self->colors, account_uid);

  if (color_id && *color_id)
    g_hash_table_insert (colors, g_strdup (full_name), g_strdup (color_id));
  else
    g_hash_table_remove (colors, full_name);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  g_hash_table_iter_init (&iter, colors);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_variant_builder_add (&builder, "{ss}", (const gchar *)key, (const gchar *)value);

  g_settings_set_value (settings, "folder-colors", g_variant_builder_end (&builder));

  /* Said here rather than left to on_settings_changed(): that one lands
   * a main loop turn later and cannot say which folder moved, and the
   * rows should repaint on the click, not after it. Its later blanket
   * emission is harmless -- repainting a row twice draws the same row. */
  g_signal_emit (self, signals[CHANGED], 0, account_uid, full_name);
}
