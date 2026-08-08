/*
 * Copyright 2024-2026 Jan-Michael Brummer
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
#include <glib.h>

G_BEGIN_DECLS

#define STAMP_PREFS_SCHEMA                    "io.github.steeb_k.Post"
#define STAMP_PREFS_WINDOW_MAXIMIZED          "window-maximized"
#define STAMP_PREFS_WINDOW_SIZE               "window-size"
#define STAMP_PREFS_WINDOW_WIDTH              "window-width"
#define STAMP_PREFS_WINDOW_HEIGHT             "window-height"
#define STAMP_PREFS_BACKGROUND_NOTIFICATIONS  "background-notifications"
#define STAMP_PREFS_BACKGROUND_AUTOSTART      "autostart"
#define STAMP_PREFS_VIEW                      "view"
#define STAMP_PREFS_ACCOUNT_ORDER             "account-order"
#define STAMP_PREFS_PROFILES                  "profiles"
#define STAMP_PREFS_DEFAULT_PROFILE           "default-profile"
#define STAMP_PREFS_OVERRIDE_PROFILE          "override-profile"
#define STAMP_PREFS_OVERRIDE_UNTIL            "override-until"
#define STAMP_PREFS_COMPOSER_MAXIMIZED        "composer-maximized"
#define STAMP_PREFS_COMPOSER_WIDTH            "composer-width"
#define STAMP_PREFS_COMPOSER_HEIGHT           "composer-height"

#define STAMP_PREFS_MAIL_SCHEMA               "io.github.steeb_k.Post.mail"
#define STAMP_PREFS_MAIL_SELECTED_FOLDER      "selected-folder"
#define STAMP_PREFS_MAIL_ALWAYS_SHOW_IMAGES   "always-show-images"
#define STAMP_PREFS_MAIL_LOAD_BIMI_IMAGES     "load-bimi-images"
#define STAMP_PREFS_MAIL_MARK_READ_TIMEOUT    "mark-read-timeout"
#define STAMP_PREFS_MAIL_PLAY_INCOMING_SOUND  "play-incoming-sound"
#define STAMP_PREFS_MAIL_IMPORTANT_FIRST      "important-first"
#define STAMP_PREFS_MAIL_REFRESH_INTERVAL     "refresh-interval"
#define STAMP_PREFS_MAIL_AGENDA_EXPANDED      "agenda-expanded"
#define STAMP_PREFS_MAIL_LAYOUT               "layout"

#define STAMP_PREFS_ACCOUNTS_SCHEMA            "io.github.steeb_k.Post.accounts"
#define STAMP_PREFS_MAIL_DEFAULT_SIGNATURE     "default-signature"

#define STAMP_SETTINGS stamp_settings_get (STAMP_PREFS_SCHEMA)
#define STAMP_SETTINGS_MAIL stamp_settings_get (STAMP_PREFS_MAIL_SCHEMA)
#define STAMP_SETTINGS_ACCOUNTS stamp_settings_get (STAMP_PREFS_ACCOUNTS_SCHEMA)

/*
 * How the mail view arranges its panes. The nicks match the
 * io.github.steeb_k.Post.MailLayout enum in the schema, and are what a
 * profile stores when it overrides the layout.
 */
typedef enum {
  STAMP_MAIL_LAYOUT_SIDE_BY_SIDE,
  STAMP_MAIL_LAYOUT_STACKED,
  STAMP_MAIL_LAYOUT_DENSE,
} StampMailLayout;

/*
 * The nick @layout is stored under. Never NULL.
 */
const gchar *
stamp_mail_layout_to_nick (StampMailLayout layout);

/*
 * The layout @nick names, or side by side for anything unrecognized --
 * including NULL, which is what "no override" reads as.
 */
StampMailLayout
stamp_mail_layout_from_nick (const gchar *nick);

/*
 * Whether @nick names a layout at all. An empty or NULL nick does not.
 */
gboolean
stamp_mail_layout_nick_is_valid (const gchar *nick);

GSettings *
stamp_settings_get (const gchar *schema);

void
stamp_settings_init (void);

void
stamp_settings_shutdown (void);

G_END_DECLS

