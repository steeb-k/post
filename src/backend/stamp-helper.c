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

#include "stamp-application.h"

#include <gio/gdesktopappinfo.h>

static char *cache_dir = NULL;
static char *data_dir = NULL;
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

const char *
stamp_get_cache_dir (void)
{
  init_dir_mutex ();
  g_mutex_lock (&dir_mutex);
  if (!cache_dir)
    cache_dir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_cache_dir (), "stamp", NULL);
  g_mutex_unlock (&dir_mutex);

  return cache_dir;
}

const char *
stamp_get_data_dir (void)
{
  init_dir_mutex ();
  g_mutex_lock (&dir_mutex);
  if (!data_dir)
    data_dir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_data_dir (), "stamp", NULL);
  g_mutex_unlock (&dir_mutex);

  return data_dir;
}

char *
stamp_strip_department (const char *str)
{
  char *ret = g_strdup (str);
  const char *pos;

  pos = strchr (ret, '(');
  if (pos)
    ret[pos - ret] = '\0';

  return ret;
}

void
stamp_launch_goa (void)
{
  const char *flatpak_id = getenv ("FLATPAK_ID");

  if (flatpak_id) {
    g_spawn_command_line_async ("flatpak-spawn --host gnome-control-center online-accounts", NULL);
  } else {
    StampApplication *self = STAMP_APPLICATION (g_application_get_default ());
    GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (self));
    g_autoptr (GError) error = NULL;
    g_autoptr (GDesktopAppInfo) app_info = g_desktop_app_info_new ("gnome-online-accounts-panel.desktop");
    g_autoptr (GdkAppLaunchContext) context = NULL;

    context = gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (window)));
    g_app_info_launch (G_APP_INFO (app_info), NULL, G_APP_LAUNCH_CONTEXT (context), &error);
  }
}


char **
g_strv_remove (const char * const *strv,
               const char         *str)
{
  char **new_strv;
  char **n;
  const char * const *s;
  guint len;

  if (!g_strv_contains (strv, str))
    return g_strdupv ((char **)strv);

  /* Needs room for one fewer string than before, plus one for trailing NULL. */
  len = g_strv_length ((char **)strv);
  new_strv = g_malloc ((len - 1 + 1) * sizeof (char *));
  n = new_strv;
  s = strv;

  while (*s != NULL) {
    if (strcmp (*s, str) != 0) {
      *n = g_strdup (*s);
      n++;
    }
    s++;
  }
  new_strv[len - 1] = NULL;

  return new_strv;
}

char **
g_strv_append (const char * const *strv,
               const char         *str)
{
  char **new_strv;
  char **n;
  const char * const *s;
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
