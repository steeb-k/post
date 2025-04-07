/* stamp-application.c
 *
 * Copyright 2025 Jan-Michael Brummer
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

#include "stamp-application.h"
#include "stamp-window.h"

#include "backend/stamp-session.h"

struct _StampApplication
{
  AdwApplication parent_instance;

  gboolean first_activation;
  gboolean run_in_background;
  StampSession *session;
};

G_DEFINE_FINAL_TYPE (StampApplication, stamp_application, ADW_TYPE_APPLICATION)

StampApplication *
stamp_application_new (const char        *application_id,
                       GApplicationFlags  flags)
{
	g_return_val_if_fail (application_id != NULL, NULL);

	return g_object_new (STAMP_TYPE_APPLICATION,
	                     "application-id", application_id,
	                     "flags", flags,
	                     "resource-base-path", "/org/tabos/stamp",
	                     NULL);
}

static void
stamp_application_activate (GApplication *app)
{
  StampApplication *self = STAMP_APPLICATION (app);
  GtkWindow *main_window = NULL;
  GList *windows = NULL;

  g_print ("%s: ENTER\n", G_STRFUNC);

  if (self->first_activation) {
    self->first_activation = FALSE;
    g_application_hold (app);
  }

  if (self->run_in_background) {
    /* request_background (app); */
    self->run_in_background = FALSE;
    return;
  }

  /* window = gtk_application_get_active_window (GTK_APPLICATION (app)); */
  windows = gtk_application_get_windows (GTK_APPLICATION (app));
  for (GList *iter = windows; iter; iter = iter->next) {
    GtkWidget *win = iter->data;

    if (STAMP_IS_WINDOW (win)) {
      main_window = GTK_WINDOW (win);
      break;
    }
  }

  if (!main_window) {
    main_window = g_object_new (STAMP_TYPE_WINDOW,
                           "application", app,
                           NULL);

    // Set size
  }

  gtk_window_present (main_window);
}

static int
stamp_application_command_line (GApplication            *app,
                                GApplicationCommandLine *cmdline)
{
  int argc;
  char **argv = g_application_command_line_get_arguments (cmdline, &argc);
  g_autoptr (GError) error = NULL;

  g_print ("%s: ENTER\n", G_STRFUNC);
  stamp_application_activate (app);

  for (int i = 1; argv[i]; i++) {
    char *mailto_uri = argv[i];
    GUri *mailto = NULL;
    g_autofree char *to = NULL;

    mailto = g_uri_parse (mailto_uri, G_URI_FLAGS_NONE, &error);
    if (!mailto) {
      g_warning ("Argument is not a URL.");
      return G_OPTION_ERROR_BAD_VALUE;
    }


    if (g_strcmp0 (g_uri_get_scheme (mailto), "mailto") != 0) {
      g_warning ("Cannot open non-mailto URL");
      return -1;
    }

    to = g_uri_unescape_string (g_uri_get_path (mailto), NULL);
  }

  return 0;
}

static void
stamp_application_startup (GApplication *app)
{
  StampApplication *self = STAMP_APPLICATION (app);

  g_print ("%s: ENTER\n", G_STRFUNC);
  G_APPLICATION_CLASS (stamp_application_parent_class)->startup (app);

  adw_init ();

  // Add CSS

  // start monitor
  self->session = stamp_session_get_default ();
  stamp_session_start (self->session, NULL, NULL, NULL);
}

static void
stamp_application_class_init (StampApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  app_class->activate = stamp_application_activate;
  app_class->command_line = stamp_application_command_line;
  app_class->startup = stamp_application_startup;
}

static void
stamp_application_about_action (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
	static const char *developers[] = {"Jan-Michael Brummer", NULL};
	StampApplication *self = user_data;
	GtkWindow *window = NULL;

	g_assert (STAMP_IS_APPLICATION (self));

	window = gtk_application_get_active_window (GTK_APPLICATION (self));

	adw_show_about_dialog (GTK_WIDGET (window),
	                       "application-name", "stamp",
	                       "application-icon", "org.tabos.stamp",
	                       "developer-name", "Jan-Michael Brummer",
	                       "translator-credits", _("translator-credits"),
	                       "version", "0.1.0",
	                       "developers", developers,
	                       "copyright", "© 2025 Jan-Michael Brummer",
	                       NULL);
}

static void
stamp_application_quit_action (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
	StampApplication *self = user_data;

	g_assert (STAMP_IS_APPLICATION (self));

	g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
	{ "quit", stamp_application_quit_action },
	{ "about", stamp_application_about_action },
};

static void
stamp_application_init (StampApplication *self)
{
	g_action_map_add_action_entries (G_ACTION_MAP (self),
	                                 app_actions,
	                                 G_N_ELEMENTS (app_actions),
	                                 self);
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.quit",
	                                       (const char *[]) { "<primary>q", NULL });
}
