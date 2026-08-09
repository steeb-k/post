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

#define G_LOG_DOMAIN "stamp-badge"

#include "config.h"

#include <glib/gi18n.h>

#include "stamp-badge.h"

/*
 * The empty id leads, because "no badge" is a choice rather than the
 * absence of one, and the picker shows it first. The two pictures come
 * next as the ones that are ours; the rest are icons, grouped the way
 * someone browsing them would expect rather than alphabetically.
 */
static const StampBadge badges[] = {
  { "",                                  NULL,                                NULL,                                         N_("Letter") },
  { "emu",                               NULL,                                "/io/github/steeb_k/Post/badges/emu.png",     N_("Emu") },
  { "mailbox",                           NULL,                                "/io/github/steeb_k/Post/badges/mailbox.png", N_("Mailbox") },
  { "building-symbolic",                 "building-symbolic",                 NULL, N_("Office") },
  { "applications-engineering-symbolic", "applications-engineering-symbolic", NULL, N_("Engineering") },
  { "utilities-terminal-symbolic",       "utilities-terminal-symbolic",       NULL, N_("Terminal") },
  { "accessories-dictionary-symbolic",   "accessories-dictionary-symbolic",   NULL, N_("Study") },
  { "input-gaming-symbolic",             "input-gaming-symbolic",             NULL, N_("Gaming") },
  { "applications-graphics-symbolic",    "applications-graphics-symbolic",    NULL, N_("Art") },
  { "audio-x-generic-symbolic",          "audio-x-generic-symbolic",          NULL, N_("Music") },
  { "emoji-food-symbolic",               "emoji-food-symbolic",               NULL, N_("Food") },
  { "emoji-nature-symbolic",             "emoji-nature-symbolic",             NULL, N_("Nature") },
  { "emoji-activities-symbolic",         "emoji-activities-symbolic",         NULL, N_("Sport") },
  { "applications-science-symbolic",     "applications-science-symbolic",     NULL, N_("Science") },
  { "rx-symbolic",                       "rx-symbolic",                       NULL, N_("Prescription") },
  { "camera-photo-symbolic",             "camera-photo-symbolic",             NULL, N_("Photography") },
  { "emoji-travel-symbolic",             "emoji-travel-symbolic",             NULL, N_("Travel") },
  { "emote-love-symbolic",               "emote-love-symbolic",               NULL, N_("Heart") },
  { "starred-symbolic",                  "starred-symbolic",                  NULL, N_("Star") },
  { "avatar-default-symbolic",           "avatar-default-symbolic",           NULL, N_("Person") },
};

const StampBadge *
stamp_badge_get_all (guint *n_badges)
{
  if (n_badges)
    *n_badges = G_N_ELEMENTS (badges);

  return badges;
}

const StampBadge *
stamp_badge_find (const gchar *id)
{
  if (!id)
    id = "";

  for (guint i = 0; i < G_N_ELEMENTS (badges); i++) {
    if (g_strcmp0 (badges[i].id, id) == 0)
      return &badges[i];
  }

  return NULL;
}

/*
 * One texture per picture, however many avatars wear it: an avatar in
 * every profile row would otherwise decode the same PNG again.
 */
static GdkPaintable *
picture_for (const StampBadge *badge)
{
  static GHashTable *textures = NULL;
  GdkTexture *texture;

  if (!textures)
    textures = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  texture = g_hash_table_lookup (textures, badge->resource);

  if (!texture) {
    texture = gdk_texture_new_from_resource (badge->resource);
    g_hash_table_insert (textures, g_strdup (badge->resource), texture);
  }

  return GDK_PAINTABLE (texture);
}

void
stamp_badge_apply (AdwAvatar        *avatar,
                   const StampBadge *badge,
                   const gchar      *initial)
{
  g_return_if_fail (ADW_IS_AVATAR (avatar));

  if (!badge)
    badge = stamp_badge_find (NULL);

  if (badge->resource) {
    /* A picture covers the whole circle, so the letter under it would
     * never show; the icon name is what AdwAvatar falls back to if the
     * resource ever goes missing. */
    adw_avatar_set_custom_image (avatar, picture_for (badge));
    adw_avatar_set_show_initials (avatar, FALSE);
    adw_avatar_set_icon_name (avatar, "avatar-default-symbolic");
    return;
  }

  adw_avatar_set_custom_image (avatar, NULL);

  if (badge->icon_name) {
    adw_avatar_set_show_initials (avatar, FALSE);
    adw_avatar_set_icon_name (avatar, badge->icon_name);
    return;
  }

  /* AdwAvatar derives its own initials, which would turn "Work Mail"
   * into "WM"; the avatar wants the single letter we picked. */
  adw_avatar_set_show_initials (avatar, initial && *initial);
  adw_avatar_set_text (avatar, initial);
  adw_avatar_set_icon_name (avatar, "avatar-default-symbolic");
}
