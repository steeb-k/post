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

#include "stamp-signature.h"

static void
on_signature_replace (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  ESource *source = E_SOURCE (source_object);

  if (!e_source_mail_signature_replace_finish (source, res, &error)) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("%s: Could not save signature: %s", G_STRFUNC, error->message);
    return;
  }
}

void
stamp_signature_save (StampSignature *self,
                      char           *html_signature)
{
  ESourceMailSignature *ext;

  ext = e_source_get_extension (self->source, E_SOURCE_EXTENSION_MAIL_SIGNATURE);
  e_source_mail_signature_set_mime_type (ext, "text/html");
  e_source_mail_signature_replace (self->source, html_signature, strlen (html_signature), G_PRIORITY_DEFAULT, self->cancellable, on_signature_replace, self);
}
