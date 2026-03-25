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

#include <camel/camel.h>
#include <glib.h>

#define LIBICAL_GLIB_UNSTABLE_API 1
#include <libical-glib/libical-glib.h>

typedef struct _ICalComponent ICalComponent;

typedef struct _StampMimeParser StampMimeParser;

typedef enum {
  STAMP_MIME_SIGNATURE_NONE,
  STAMP_MIME_SIGNATURE_GOOD,
  STAMP_MIME_SIGNATURE_BAD,
  STAMP_MIME_SIGNATURE_UNKNOWN,
} StampMimeSignatureStatus;

typedef enum {
  STAMP_MIME_ENCRYPTION_NONE,
  STAMP_MIME_ENCRYPTION_VALID,
  STAMP_MIME_ENCRYPTION_INVALID,
} StampMimeEncryptionStatus;

typedef struct {
  StampMimeSignatureStatus status;
  StampMimeEncryptionStatus encryption;
  char *description;
  char **signers;
  guint n_signers;
} StampMimeValidation;

typedef struct {
  char *content;
  gsize length;
  char *charset;
  gboolean is_html;
  gboolean is_signed;
  gboolean is_encrypted;
} StampMimeContent;

typedef struct {
  char *filename;
  char *content_type;
  char *content_id;
  char *disposition;
  GBytes *data;
  gsize size;
} StampMimeAttachment;

typedef struct {
  char *content_id;
  char *content_type;
  GBytes *data;
} StampMimeInlinePart;

typedef struct {
  char *uid;
  char *method;
  ICalComponent *ical;
} StampMimeCalendar;

typedef void (*StampMimeParserCallback) (StampMimeParser *self,
                                        gpointer user_data);

struct _StampMimeParser {
  CamelMimeMessage *message;
  CamelSession *session;
  GCancellable *cancellable;

  CamelDataWrapper *root_content;
  CamelMimePart *current_part;

  StampMimeValidation *validation;
  GPtrArray *attachments;
  GPtrArray *inline_parts;
  StampMimeContent *body;
  StampMimeCalendar *calendar;

  gboolean is_multipart;
  gboolean decryption_attempted;
  gboolean decryption_succeeded;

  GError *error;
};

StampMimeParser *
stamp_mime_parser_new (CamelMimeMessage *message,
                       CamelSession     *session,
                       GCancellable     *cancellable);

void
stamp_mime_parser_free (StampMimeParser *self);

gboolean
stamp_mime_parser_parse (StampMimeParser *self);

StampMimeContent *
stamp_mime_parser_get_body (StampMimeParser *self);

GPtrArray *
stamp_mime_parser_get_attachments (StampMimeParser *self);

StampMimeValidation *
stamp_mime_parser_get_validation (StampMimeParser *self);

StampMimeCalendar *
stamp_mime_parser_get_calendar (StampMimeParser *self);

gboolean
stamp_mime_parser_has_calendar (StampMimeParser *self);

gboolean
stamp_mime_parser_has_attachments (StampMimeParser *self);

GPtrArray *
stamp_mime_parser_get_inline_parts (StampMimeParser *self);

char *
stamp_mime_parser_embed_inline_images (StampMimeParser *self,
                                       const char      *html_content);

