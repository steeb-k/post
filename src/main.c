/*
 * Copyright 2025-2026 Jan-Michael Brummer
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

#include <glib/gi18n.h>
#include <gst/gst.h>
#include <unistd.h>

#include "stamp-application.h"
#include "stamp-profile-manager.h"
#include "stamp-settings.h"

/* Launchers routinely start the app with stderr on /dev/null, and every
 * warning it logs about a failed sync or a dropped connection goes with
 * it -- which leaves nothing at all to read after the fact. Prefer the
 * journal when stderr is not a terminal, and fall back to the default
 * writer wherever the journal is not reachable, such as inside the
 * flatpak sandbox. */
static GLogWriterOutput
log_writer (GLogLevelFlags   log_level,
            const GLogField *fields,
            gsize            n_fields,
            gpointer         user_data)
{
  if (g_log_writer_journald (log_level, fields, n_fields, user_data) == G_LOG_WRITER_HANDLED)
    return G_LOG_WRITER_HANDLED;

  return g_log_writer_default (log_level, fields, n_fields, user_data);
}

gint
main (gint    argc,
      gchar **argv)
{
  g_autoptr (StampApplication) app = NULL;
  gint ret;

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  if (!isatty (STDERR_FILENO))
    g_log_set_writer_func (log_writer, NULL, NULL);

  gst_init (&argc, &argv);

  g_set_prgname ("post-mail");
  g_set_application_name (_("Post"));
  stamp_settings_init ();

  app = stamp_application_new ("io.github.steeb_k.Post", G_APPLICATION_HANDLES_COMMAND_LINE);
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  /* Before the settings go, since the manager writes through them. */
  stamp_profile_manager_shutdown ();
  stamp_settings_shutdown ();

  return ret;
}
