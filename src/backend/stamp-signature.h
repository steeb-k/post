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

#include <glib.h>
#include <libebackend/libebackend.h>

G_BEGIN_DECLS

typedef struct _StampSignature StampSignature;

void
stamp_signature_save (StampSignature *signature,
                      char           *html_signature);

void
stamp_signature_clear (gpointer user_data);

StampSignature *
stamp_signature_new (ESource    *source,
                     const char *mime_type,
                     const char *content);

const char *
stamp_signature_get_mime_type (StampSignature *self);

const char *
stamp_signature_get_content (StampSignature *self);

G_END_DECLS

