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

#include "stamp-helper.h"

#include <gio/gdesktopappinfo.h>

#include "stamp-application.h"

static gchar *cache_dir = NULL;
static gchar *data_dir = NULL;
static GMutex dir_mutex;
static gsize dir_mutex_initialized = 0;

static void
init_dir_mutex (void)
{
  if (g_once_init_enter (&dir_mutex_initialized)) {
    g_mutex_init (&dir_mutex);
    g_once_init_leave (&dir_mutex_initialized, 1);
  }
}

const gchar *
stamp_get_cache_dir (void)
{
  init_dir_mutex ();
  g_mutex_lock (&dir_mutex);
  if (!cache_dir)
    cache_dir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_cache_dir (), "stamp", NULL);
  g_mutex_unlock (&dir_mutex);

  return cache_dir;
}

const gchar *
stamp_get_data_dir (void)
{
  init_dir_mutex ();
  g_mutex_lock (&dir_mutex);
  if (!data_dir)
    data_dir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_data_dir (), "stamp", NULL);
  g_mutex_unlock (&dir_mutex);

  return data_dir;
}

gchar *
stamp_strip_department (const gchar *str)
{
  gchar *ret = g_strdup (str);
  const gchar *pos;

  pos = strchr (ret, '(');
  if (pos)
    ret[pos - ret] = '\0';

  return ret;
}

#define STAMP_GOA_DESKTOP_ID "gnome-online-accounts-gtk.desktop"
#define STAMP_GOA_COMMAND "gnome-online-accounts-gtk"

gboolean
stamp_goa_is_available (void)
{
  g_autoptr (GDesktopAppInfo) app_info = NULL;
  g_autofree gchar *path = NULL;

  /* Inside a Flatpak the account editor lives on the host, where we
   * cannot look for its desktop file. Assume it is there and report the
   * failure when the launch does not work out. */
  if (getenv ("FLATPAK_ID"))
    return TRUE;

  app_info = g_desktop_app_info_new (STAMP_GOA_DESKTOP_ID);
  if (app_info)
    return TRUE;

  path = g_find_program_in_path (STAMP_GOA_COMMAND);

  return path != NULL;
}

gboolean
stamp_launch_goa (void)
{
  g_autoptr (GDesktopAppInfo) app_info = NULL;
  g_autoptr (GdkAppLaunchContext) context = NULL;
  g_autoptr (GError) error = NULL;
  GtkWindow *window;

  if (getenv ("FLATPAK_ID")) {
    if (!g_spawn_command_line_async ("flatpak-spawn --host " STAMP_GOA_COMMAND, &error)) {
      g_warning ("%s: Could not launch the account editor: %s", G_STRFUNC, error->message);
      return FALSE;
    }

    return TRUE;
  }

  app_info = g_desktop_app_info_new (STAMP_GOA_DESKTOP_ID);
  if (!app_info) {
    if (!g_spawn_command_line_async (STAMP_GOA_COMMAND, &error)) {
      g_warning ("%s: Could not launch the account editor: %s", G_STRFUNC, error->message);
      return FALSE;
    }

    return TRUE;
  }

  window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));
  if (window)
    context = gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (window)));

  if (!g_app_info_launch (G_APP_INFO (app_info), NULL, G_APP_LAUNCH_CONTEXT (context), &error)) {
    g_warning ("%s: Could not launch the account editor: %s", G_STRFUNC, error->message);
    return FALSE;
  }

  return TRUE;
}


gchar **
g_strv_remove (const gchar * const *strv,
               const gchar         *str)
{
  gchar **new_strv;
  gchar **n;
  const gchar * const *s;
  guint len;

  if (!g_strv_contains (strv, str))
    return g_strdupv ((char **)strv);

  /* Needs room for one fewer string than before, plus one for trailing NULL. */
  len = g_strv_length ((char **)strv);
  new_strv = g_malloc ((len - 1 + 1) * sizeof (char *));
  n = new_strv;
  s = strv;

  while (*s != NULL) {
    if (!g_str_equal (*s, str)) {
      *n = g_strdup (*s);
      n++;
    }
    s++;
  }
  new_strv[len - 1] = NULL;

  return new_strv;
}

gchar **
g_strv_append (const gchar * const *strv,
               const gchar         *str)
{
  gchar **new_strv;
  gchar **n;
  const gchar * const *s;
  guint len;

  if (g_strv_contains (strv, str))
    return g_strdupv ((char **)strv);

  /* Needs room for one fewer string than before, plus one for trailing NULL. */
  len = g_strv_length ((char **)strv) + 2;
  new_strv = g_malloc (len * sizeof (char *));
  n = new_strv;
  s = strv;

  while (*s != NULL) {
    *n = g_strdup (*s);
    n++;
    s++;
  }

  new_strv[len - 2] = g_strdup (str);
  new_strv[len - 1] = NULL;

  return new_strv;
}
