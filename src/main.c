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

#include "config.h"

#include <glib/gi18n.h>
#include <gst/gst.h>

#include "stamp-application.h"
#include "stamp-settings.h"

gint
main (gint    argc,
      gchar **argv)
{
  g_autoptr (StampApplication) app = NULL;
  gint ret;

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gst_init (&argc, &argv);

  g_set_prgname ("stamp");
  g_set_application_name (_("Stamp"));
  stamp_settings_init ();

  app = stamp_application_new ("org.tabos.stamp", G_APPLICATION_HANDLES_COMMAND_LINE);
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  stamp_settings_shutdown ();

  return ret;
}
