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

/*
 * Bridges mail attachments into GNOME Calendar's import dialog, which
 * reads from GFiles. Mail parts live in memory, so they take a detour
 * through a temporary file that lives exactly as long as the dialog.
 */

#include "stamp-calendar-import.h"

#include "stamp-gcal.h"

#include "gcal-import-dialog.h"

#include <adwaita.h>

static void
on_import_dialog_closed (AdwDialog *dialog,
                         gpointer   user_data)
{
  g_autoptr (GFile) file = G_FILE (user_data);

  g_file_delete (file, NULL, NULL);
}

static GtkWidget *
present_import_dialog (GFile     *file,
                       GtkWidget *parent)
{
  g_autoptr (GList) files = NULL;
  GtkWidget *dialog;

  stamp_gcal_ensure_context ();

  files = g_list_append (NULL, file);
  dialog = gcal_import_dialog_new_for_file_list (files);

  adw_dialog_present (ADW_DIALOG (dialog), parent);

  return dialog;
}

void
stamp_calendar_import_ics_file (GFile     *file,
                                GtkWidget *parent)
{
  g_return_if_fail (G_IS_FILE (file));
  g_return_if_fail (GTK_IS_WIDGET (parent));

  present_import_dialog (file, parent);
}

void
stamp_calendar_import_ics_data (GBytes      *data,
                                const gchar *display_name,
                                GtkWidget   *parent)
{
  g_autoptr (GFileIOStream) stream = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *basename = NULL;
  g_autofree char *template = NULL;
  GtkWidget *dialog;

  g_return_if_fail (data != NULL);
  g_return_if_fail (GTK_IS_WIDGET (parent));

  basename = g_path_get_basename (display_name && *display_name ? display_name : "invite.ics");
  template = g_strdup_printf ("XXXXXX-%s", basename);

  file = g_file_new_tmp (template, &stream, &error);
  if (error) {
    g_warning ("%s: Could not create temporary file: %s", G_STRFUNC, error->message);
    return;
  }

  if (!g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (stream)),
                                  g_bytes_get_data (data, NULL),
                                  g_bytes_get_size (data),
                                  NULL,
                                  NULL,
                                  &error)) {
    g_warning ("%s: Could not write the calendar data: %s", G_STRFUNC, error->message);
    g_file_delete (file, NULL, NULL);
    return;
  }

  if (!g_io_stream_close (G_IO_STREAM (stream), NULL, &error)) {
    g_warning ("%s: Could not close the calendar data: %s", G_STRFUNC, error->message);
    g_file_delete (file, NULL, NULL);
    return;
  }

  dialog = present_import_dialog (file, parent);

  g_signal_connect (dialog, "closed", G_CALLBACK (on_import_dialog_closed), g_steal_pointer (&file));
}
