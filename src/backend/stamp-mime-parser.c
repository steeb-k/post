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

#include "stamp-mime-parser.h"

#include <libsoup/soup.h>
#include <libecal/libecal.h>

static void
stamp_mime_validation_free (StampMimeValidation *validation)
{
  g_assert (validation);

  g_clear_pointer (&validation->description, g_free);
  g_clear_pointer (&validation->signers, g_strfreev);
  g_clear_pointer (&validation, g_free);
}

static StampMimeValidation *
stamp_mime_validation_new (void)
{
  StampMimeValidation *validation = g_new0 (StampMimeValidation, 1);

  validation->status = STAMP_MIME_SIGNATURE_NONE;
  validation->encryption = STAMP_MIME_ENCRYPTION_NONE;

  return validation;
}

static void
stamp_mime_content_free (StampMimeContent *content)
{
  g_assert (content);

  g_clear_pointer (&content->content, g_free);
  g_clear_pointer (&content->charset, g_free);
  g_clear_pointer (&content, g_free);
}

static StampMimeContent *
stamp_mime_content_new (void)
{
  return g_new0 (StampMimeContent, 1);
}

static void
stamp_mime_attachment_free (StampMimeAttachment *attachment)
{
  g_assert (attachment);

  g_clear_pointer (&attachment->filename, g_free);
  g_clear_pointer (&attachment->content_type, g_free);
  g_clear_pointer (&attachment->content_id, g_free);
  g_clear_pointer (&attachment->disposition, g_free);
  g_clear_pointer (&attachment->data, g_bytes_unref);
  g_clear_pointer (&attachment, g_free);
}

static StampMimeAttachment *
stamp_mime_attachment_new (void)
{
  return g_new0 (StampMimeAttachment, 1);
}

static void
stamp_mime_calendar_free (StampMimeCalendar *calendar)
{
  g_assert (calendar);

  g_clear_pointer (&calendar->uid, g_free);
  g_clear_pointer (&calendar->method, g_free);
  g_clear_object (&calendar->ical);
  g_clear_pointer (&calendar, g_free);
}

static StampMimeCalendar *
stamp_mime_calendar_new (void)
{
  return g_new0 (StampMimeCalendar, 1);
}

static void
stamp_mime_list_unsubscribe_free (StampMimeListUnsubscribe *list_unsubscribe)
{
  g_assert (list_unsubscribe);

  g_clear_pointer (&list_unsubscribe->url, g_free);
  g_clear_pointer (&list_unsubscribe, g_free);
}

static StampMimeListUnsubscribe *
stamp_mime_list_unsubscribe_new (void)
{
  return g_new0 (StampMimeListUnsubscribe, 1);
}

static void
stamp_mime_inline_part_free (StampMimeInlinePart *part)
{
  g_assert (part);

  g_clear_pointer (&part->content_id, g_free);
  g_clear_pointer (&part->content_type, g_free);
  g_clear_pointer (&part->data, g_bytes_unref);
  g_clear_pointer (&part, g_free);
}

StampMimeParser *
stamp_mime_parser_new (CamelMimeMessage *message,
                       CamelSession     *session,
                       GCancellable     *cancellable)
{
  StampMimeParser *parser;

  g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), NULL);

  parser = g_new0 (StampMimeParser, 1);
  parser->message = g_object_ref (message);
  parser->session = session ? g_object_ref (session) : NULL;
  parser->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
  parser->attachments = g_ptr_array_new_with_free_func ((GDestroyNotify)stamp_mime_attachment_free);
  parser->inline_parts = g_ptr_array_new_with_free_func ((GDestroyNotify)stamp_mime_inline_part_free);
  parser->root_content = CAMEL_DATA_WRAPPER (message);

  return parser;
}

void
stamp_mime_parser_free (StampMimeParser *parser)
{
  g_assert (parser);

  g_clear_object (&parser->message);
  g_clear_object (&parser->session);
  g_clear_object (&parser->cancellable);
  g_clear_object (&parser->root_content);
  g_clear_object (&parser->current_part);

  g_clear_pointer (&parser->validation, stamp_mime_validation_free);
  g_clear_pointer (&parser->body, stamp_mime_content_free);
  g_clear_pointer (&parser->calendar, stamp_mime_calendar_free);
  g_clear_pointer (&parser->list_unsubscribe, stamp_mime_list_unsubscribe_free);

  g_clear_pointer (&parser->attachments, g_ptr_array_unref);
  g_clear_pointer (&parser->inline_parts, g_ptr_array_unref);
  g_clear_error (&parser->error);

  g_clear_pointer (&parser, g_free);
}

static char *
convert_content_to_utf8 (const char *data,
                         gsize       len,
                         const char *encoding)
{
  g_autofree char *utf8 = NULL;
  g_autoptr (GError) error = NULL;
  const char *iconv_encoding;

  if (!data || len == 0)
    return NULL;

  if (encoding) {
    iconv_encoding = camel_iconv_charset_name (encoding);

    if (iconv_encoding) {
      utf8 = g_convert (data, len, "UTF-8", iconv_encoding, NULL, NULL, &error);
      if (error)
        g_clear_pointer (&utf8, g_free);
    }
  }

  if (!utf8 || !g_utf8_validate (utf8, -1, NULL)) {
    g_clear_pointer (&utf8, g_free);
    utf8 = g_convert (data, len, "UTF-8", "ISO-8859-1", NULL, NULL, &error);
    if (error)
      g_clear_pointer (&utf8, g_free);
  }

  return g_steal_pointer (&utf8);
}

static char *
unpack_flowed_format (const char *text,
                      gsize       len)
{
  GString *result;
  const char *line_start;
  const char *line_end;
  gboolean trailing_space;
  gsize line_len;

  if (!text || len == 0)
    return g_strdup (text);

  result = g_string_sized_new (len);
  line_start = text;
  trailing_space = FALSE;

  while (line_start < text + len) {
    line_end = line_start;

    while (line_end < text + len && *line_end != '\r' && *line_end != '\n')
      line_end++;

    line_len = line_end - line_start;

    if (line_len > 0) {
      if (line_start[line_len - 1] == ' ' || line_start[line_len - 1] == '\t') {
        trailing_space = TRUE;
      } else {
        trailing_space = FALSE;
      }

      g_string_append_len (result, line_start, line_len);

      if (trailing_space && line_end < text + len) {
        g_string_append_c (result, ' ');
      } else if (!trailing_space && line_end < text + len) {
        g_string_append_c (result, '\n');
      }
    }

    while (line_end < text + len && (*line_end == '\r' || *line_end == '\n')) {
      line_end++;
    }

    line_start = line_end;
  }

  return g_string_free_and_steal (result);
}

static void
convert_newlines_to_br (char **content)
{
  static GRegex *newline_regex = NULL;
  g_autofree char *converted = NULL;

  if (!newline_regex)
    newline_regex = g_regex_new ("\r?\n", 0, 0, NULL);

  if (newline_regex && *content) {
    g_autoptr (GError) error = NULL;

    converted = g_regex_replace_literal (newline_regex, *content, -1, 0, "<br/>", 0, &error);
    if (error) {
      g_warning ("%s: Could not apply regex: %s", G_STRFUNC, error->message);
    } else if (converted) {
      g_free (*content);
      *content = g_steal_pointer (&converted);
    }
  }
}

static gboolean
handle_text_content (StampMimeParser  *parser,
                     CamelDataWrapper *content,
                     CamelContentType *content_type)
{
  GMemoryOutputStream *os;
  g_autoptr (GError) error = NULL;
  g_autofree char *text = NULL;
  g_autofree char *body = NULL;
  gsize body_len = 0;
  gboolean is_html;

  if (!content || !content_type)
    return FALSE;

  os = G_MEMORY_OUTPUT_STREAM (g_memory_output_stream_new_resizable ());

  if (!camel_data_wrapper_decode_to_output_stream_sync (content, G_OUTPUT_STREAM (os), parser->cancellable, &error)) {
    g_warning ("%s: Could not decode text content: %s", G_STRFUNC, error ? error->message : "unknown error");
    return FALSE;
  }

  if (!g_output_stream_close (G_OUTPUT_STREAM (os), parser->cancellable, &error)) {
    g_warning ("%s: Could not close stream: %s", G_STRFUNC, error->message);
    return FALSE;
  }

  text = convert_content_to_utf8 (g_memory_output_stream_get_data (os),
                                  g_memory_output_stream_get_size (os),
                                  camel_content_type_param (content_type, "charset"));
  if (!text)
    return FALSE;

  is_html = g_strcmp0 (content_type->subtype, "html") == 0;

  if (!is_html) {
    body = g_strdup (text);
    body_len = strlen (body);

    if (g_strcmp0 (camel_content_type_param (content_type, "format"), "flowed") == 0) {
      g_autofree char *unpacked = unpack_flowed_format (body, body_len);
      if (unpacked) {
        g_clear_pointer (&body, g_free);
        body = g_steal_pointer (&unpacked);
        body_len = strlen (body);
      }
    }
  } else {
    body = g_strdup (text);
    body_len = text ? strlen (text) : 0;
  }

  if (parser->body && parser->body->is_html && !is_html)
    return TRUE;

  if (!parser->body)
    parser->body = stamp_mime_content_new ();
  else
    g_clear_pointer (&parser->body->content, g_free);

  parser->body->content = g_strndup (body, body_len);
  parser->body->length = body_len;
  parser->body->charset = g_strdup (camel_content_type_param (content_type, "charset"));
  parser->body->is_html = is_html;

  if (!is_html) {
    static GRegex *email_regex = NULL;
    static GRegex *url_regex = NULL;
    g_autofree char *linked_content = NULL;

    if (!email_regex) {
      email_regex = g_regex_new (
        "([a-zA-Z0-9._%+\\-]+@[a-zA-Z0-9.\\-]+\\.[a-zA-Z]{2,})",
        0,
        0,
        NULL);
    }

    if (!url_regex) {
      url_regex = g_regex_new (
        "((https?://|ftp://|git://)[^[:space:]<>\"]+)",
        0,
        0,
        NULL);
    }

    if (parser->body->content) {
      if (email_regex) {
        linked_content = g_regex_replace (email_regex, parser->body->content, -1, 0, "<a href=\"mailto:\\1\">\\1</a>", 0, NULL);

        if (linked_content) {
          g_free (parser->body->content);
          parser->body->content = g_steal_pointer (&linked_content);
        }
      }

      if (url_regex) {
        linked_content = g_regex_replace (url_regex, parser->body->content, -1, 0, "<a href=\"\\1\">\\1</a>", 0, NULL);

        if (linked_content) {
          g_free (parser->body->content);
          parser->body->content = g_steal_pointer (&linked_content);
        }
      }

      convert_newlines_to_br (&parser->body->content);
      parser->body->length = strlen (parser->body->content);
    }
  }

  return TRUE;
}

static gboolean
handle_calendar_content (StampMimeParser  *parser,
                         CamelDataWrapper *content,
                         CamelContentType *content_type)
{
  g_autoptr (GMemoryOutputStream) os = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *ical_text = NULL;
  ICalComponent *ical = NULL;
  const char *ical_start = NULL;
  const char *body = NULL;

  if (!content)
    return FALSE;

  os = G_MEMORY_OUTPUT_STREAM (g_memory_output_stream_new_resizable ());

  if (!camel_data_wrapper_decode_to_output_stream_sync (content, G_OUTPUT_STREAM (os), parser->cancellable, &error)) {
    g_warning ("%s: Could not decode calendar content: %s", G_STRFUNC, error ? error->message : "unknown error");
    return FALSE;
  }

  if (!g_output_stream_close (G_OUTPUT_STREAM (os), parser->cancellable, &error)) {
    g_warning ("%s: Could not close stream: %s", G_STRFUNC, error->message);
    return FALSE;
  }

  ical_text = convert_content_to_utf8 (g_memory_output_stream_get_data (os),
                                       g_memory_output_stream_get_size (os),
                                       camel_content_type_param (content_type, "charset"));
  if (!ical_text)
    return FALSE;

  ical_start = strstr (ical_text, "BEGIN:VCALENDAR");
  if (!ical_start)
    ical_start = strstr (ical_text, "BEGIN:VCARD");

  if (!ical_start) {
    body = strstr (ical_text, "\r\n\r\n");

    if (!body)
      body = strstr (ical_text, "\n\n");

    if (body) {
      body += (body[0] == '\r') ? 4 : 2;
      ical_start = strstr (body, "BEGIN:VCALENDAR");

      if (!ical_start)
        ical_start = strstr (body, "BEGIN:VCARD");
    }
  }

  if (ical_start) {
    g_autofree char *tmp = g_strdup (ical_start);

    ical = e_cal_util_parse_ics_string (tmp);
  } else {
    ical = e_cal_util_parse_ics_string (ical_text);
  }

  if (!ical)
    return FALSE;

  if (parser->calendar)
    stamp_mime_calendar_free (parser->calendar);

  parser->calendar = stamp_mime_calendar_new ();
  parser->calendar->ical = ical;
  parser->calendar->uid = g_strdup (i_cal_component_get_uid (ical));

  return TRUE;
}

static void
process_single_part (StampMimeParser *parser,
                     CamelMimePart   *part);

static void
handle_inline_content (StampMimeParser *parser,
                       CamelMimePart   *part)
{
  const char *content_id;
  CamelContentType *content_type;
  CamelDataWrapper *content;
  GOutputStream *os;
  g_autoptr (GError) error = NULL;
  GBytes *bytes;
  StampMimeInlinePart *inline_part;

  content_type = camel_mime_part_get_content_type (part);
  content_id = camel_mime_part_get_content_id (part);
  if (!content_id) {
    handle_text_content (parser, camel_medium_get_content (CAMEL_MEDIUM (part)), content_type);
    return;
  }

  content = camel_medium_get_content (CAMEL_MEDIUM (part));
  if (!CAMEL_IS_DATA_WRAPPER (content))
    return;

  os = g_memory_output_stream_new_resizable ();
  if (!camel_data_wrapper_decode_to_output_stream_sync (content, os, parser->cancellable, &error)) {
    g_warning ("%s: Could not decode inline content: %s", G_STRFUNC, error->message);
    return;
  }

  g_output_stream_close (os, parser->cancellable, NULL);

  bytes = g_memory_output_stream_steal_as_bytes ((GMemoryOutputStream *)os);

  inline_part = g_new0 (StampMimeInlinePart, 1);
  inline_part->content_id = g_strdup (content_id);
  inline_part->data = bytes;

  if (content_type && content_type->subtype) {
    inline_part->content_type = g_strdup_printf ("%s/%s", content_type->type, content_type->subtype);
  } else {
    /* Fallback */
    inline_part->content_type = g_strdup ("image/png");
  }

  g_ptr_array_add (parser->inline_parts, inline_part);
}

static void
handle_attachment (StampMimeParser *parser,
                   CamelMimePart   *part)
{
  StampMimeAttachment *attachment;
  CamelContentType *content_type;
  const char *filename;
  g_autoptr (GError) error = NULL;
  g_autoptr (GByteArray) byte_array = NULL;
  CamelStream *stream;
  g_autoptr (GOutputStream) os = NULL;

  content_type = camel_mime_part_get_content_type (part);
  filename = camel_mime_part_get_filename (part);

  byte_array = g_byte_array_new ();
  stream = camel_stream_mem_new ();
  camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream), byte_array);

  if (!camel_data_wrapper_write_to_stream_sync (CAMEL_DATA_WRAPPER (camel_medium_get_content (CAMEL_MEDIUM (part))), stream, parser->cancellable, &error)) {
    g_warning ("%s: Could not get attachment content: %s", G_STRFUNC, error ? error->message : "unknown error");
    return;
  }

  attachment = stamp_mime_attachment_new ();
  attachment->filename = g_strdup (filename);
  attachment->content_type = g_strdup (content_type ? content_type->type : "application/octet-stream");
  attachment->content_id = g_strdup (camel_mime_part_get_content_id (part));

  os = g_memory_output_stream_new_resizable ();
  if (camel_data_wrapper_decode_to_output_stream_sync (CAMEL_DATA_WRAPPER (camel_medium_get_content (CAMEL_MEDIUM (part))), os, parser->cancellable, &error)) {
    g_output_stream_close (os, parser->cancellable, NULL);
    attachment->data = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (os));
    attachment->size = attachment->data ? g_bytes_get_size (attachment->data) : 0;
  }

  g_ptr_array_add (parser->attachments, attachment);
}

static StampMimeSignatureStatus
map_validity_status (CamelCipherValiditySign status)
{
  switch (status) {
    case CAMEL_CIPHER_VALIDITY_SIGN_GOOD:
      return STAMP_MIME_SIGNATURE_GOOD;
    case CAMEL_CIPHER_VALIDITY_SIGN_BAD:
      return STAMP_MIME_SIGNATURE_BAD;
    case CAMEL_CIPHER_VALIDITY_SIGN_NEED_PUBLIC_KEY:
    case CAMEL_CIPHER_VALIDITY_SIGN_UNKNOWN:
    case CAMEL_CIPHER_VALIDITY_SIGN_NONE:
    default:
      return STAMP_MIME_SIGNATURE_UNKNOWN;
  }
}

static void
process_signature_validation (StampMimeParser     *parser,
                              CamelCipherValidity *validity)
{
  GString *description;
  GPtrArray *signers;
  CamelCipherCertInfo *info;
  GQueue *signer_list;
  guint i;

  if (!validity)
    return;

  if (!parser->validation)
    parser->validation = stamp_mime_validation_new ();

  parser->validation->status = map_validity_status (validity->sign.status);
  parser->validation->encryption = validity->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE
                                    ? STAMP_MIME_ENCRYPTION_VALID
                                    : STAMP_MIME_ENCRYPTION_NONE;

  if (validity->sign.description) {
    description = g_string_new (g_strstrip (validity->sign.description));
  } else {
    description = g_string_new ("");
  }

  signers = g_ptr_array_new_with_free_func (g_free);
  signer_list = &validity->sign.signers;

  if (signer_list->length > 0) {
    for (i = 0; i < signer_list->length; i++) {
      info = g_queue_peek_nth (signer_list, i);

      g_ptr_array_add (signers, g_strdup (info->name ? info->name : ""));
      g_ptr_array_add (signers, g_strdup (info->email ? info->email : ""));
    }

    g_ptr_array_add (signers, NULL);
    parser->validation->signers = (char **)g_ptr_array_free (signers, FALSE);
    parser->validation->n_signers = signer_list->length;
  } else {
    g_ptr_array_free (signers, TRUE);
    parser->validation->signers = NULL;
    parser->validation->n_signers = 0;
  }

  parser->validation->description = g_string_free_and_steal (description);
}

static gboolean
handle_pgp_encrypted (StampMimeParser *parser,
                      CamelMimePart   *part)
{
  g_autoptr (CamelCipherContext) cipher = NULL;
  g_autoptr (CamelMimePart) decrypted_part = NULL;
  CamelDataWrapper *content;
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelCipherValidity) validity = NULL;

  if (!parser->session)
    return FALSE;

  parser->decryption_attempted = TRUE;

  cipher = camel_gpg_context_new (parser->session);
  decrypted_part = camel_mime_part_new ();

  validity = camel_cipher_context_decrypt_sync (cipher, CAMEL_MIME_PART (parser->message),
                                                decrypted_part, parser->cancellable, &error);
  if (error) {
    g_warning ("%s: PGP decryption failed: %s", G_STRFUNC, error->message);
    return FALSE;
  }

  if (validity)
    process_signature_validation (parser, validity);

  content = camel_medium_get_content (CAMEL_MEDIUM (decrypted_part));
  if (content) {
    parser->decryption_succeeded = TRUE;
    parser->is_multipart = CAMEL_IS_MULTIPART (content);

    g_clear_object (&parser->root_content);
    parser->root_content = g_object_ref (content);
  }

  return parser->decryption_succeeded;
}

static gboolean
handle_smime_encrypted (StampMimeParser *parser,
                        CamelMimePart   *part)
{
  CamelCipherContext *cipher;
  CamelMimePart *decrypted_part = NULL;
  g_autoptr (CamelDataWrapper) content = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelCipherValidity) validity = NULL;
  CamelContentType *content_type;

  if (!parser->session)
    return FALSE;

  parser->decryption_attempted = TRUE;

  cipher = camel_smime_context_new (parser->session);
  decrypted_part = camel_mime_part_new ();

  validity = camel_cipher_context_decrypt_sync (cipher, CAMEL_MIME_PART (parser->message),
                                                decrypted_part, parser->cancellable, &error);
  if (error) {
    g_warning ("S/MIME decryption failed: %s", error->message);
    g_propagate_error (&parser->error, g_steal_pointer (&error));
    return FALSE;
  }

  if (validity)
    process_signature_validation (parser, validity);

  content = camel_medium_get_content (CAMEL_MEDIUM (decrypted_part));
  if (!content) {
    return FALSE;
  }

  content_type = camel_mime_part_get_content_type (decrypted_part);
  parser->decryption_succeeded = TRUE;

  if (CAMEL_IS_MULTIPART (content)) {
    parser->is_multipart = TRUE;
    g_clear_object (&parser->root_content);
    parser->root_content = g_object_ref (content);
  } else if (content_type && g_strcmp0 (content_type->type, "text") == 0) {
    parser->is_multipart = FALSE;
    handle_text_content (parser, content, content_type);
  } else if (content_type && g_strcmp0 (content_type->type, "message") == 0) {
    CamelMimeMessage *inner_msg = (CamelMimeMessage *)content;

    if (CAMEL_IS_MIME_MESSAGE (inner_msg)) {
      CamelDataWrapper *inner_body = camel_medium_get_content (CAMEL_MEDIUM (inner_msg));

      if (inner_body) {
        CamelContentType *body_ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (inner_msg));

        if (CAMEL_IS_MULTIPART (inner_body)) {
          parser->is_multipart = TRUE;
          g_clear_object (&parser->root_content);
          parser->root_content = g_object_ref (inner_body);
        } else if (body_ct && g_strcmp0 (body_ct->type, "text") == 0) {
          CamelStream *stream;
          g_autofree char *text = NULL;
          g_autofree char *body_str = NULL;
          gsize body_len = 0;
          GByteArray *byte_array;

          byte_array = g_byte_array_new ();
          stream = camel_stream_mem_new ();
          camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream), byte_array);

          camel_data_wrapper_write_to_stream_sync (inner_body, stream, parser->cancellable, NULL);

          text = convert_content_to_utf8 ((const char *)byte_array->data,
                                          byte_array->len,
                                          camel_content_type_param (body_ct, "charset"));

          if (text) {
            body_str = g_strdup (text);
            body_len = strlen (body_str);

            if (g_strcmp0 (camel_content_type_param (body_ct, "format"), "flowed") == 0) {
              g_autofree char *unpacked = unpack_flowed_format (body_str, body_len);
              if (unpacked) {
                g_clear_pointer (&body_str, g_free);
                body_str = g_steal_pointer (&unpacked);
                body_len = strlen (body_str);
              }
            }

            if (!parser->body) {
              parser->body = stamp_mime_content_new ();
            } else {
              g_clear_pointer (&parser->body->content, g_free);
            }

            parser->body->content = g_strndup (body_str, body_len);
            parser->body->length = body_len;
            parser->body->charset = g_strdup (camel_content_type_param (body_ct, "charset"));

            if (g_strcmp0 (body_ct->subtype, "html") == 0)
              parser->body->is_html = TRUE;
            else if (g_strcmp0 (body_ct->subtype, "plain") == 0)
              parser->body->is_html = FALSE;
          }
        } else {
          parser->is_multipart = FALSE;
          g_clear_object (&parser->root_content);
          parser->root_content = g_object_ref (inner_body);
        }
      }
    }
  } else {
    parser->is_multipart = FALSE;
    g_clear_object (&parser->root_content);
    parser->root_content = g_object_ref (content);
  }

  return parser->decryption_succeeded;
}

static gboolean
handle_pgp_signature (StampMimeParser *parser,
                      CamelMimePart   *part)
{
  g_autoptr (CamelCipherContext) cipher = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelCipherValidity) validity = NULL;

  if (!parser->session)
    return FALSE;

  cipher = camel_gpg_context_new (parser->session);

  validity = camel_cipher_context_verify_sync (cipher, CAMEL_MIME_PART (parser->message),
                                               parser->cancellable, &error);
  if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    g_warning ("%s: PGP signature verification failed: %s", G_STRFUNC, error->message);
  }

  if (validity) {
    if (parser->body)
      parser->body->is_signed = TRUE;

    process_signature_validation (parser, validity);
  }

  return validity != NULL;
}

static gboolean
handle_smime_signature (StampMimeParser *parser,
                        CamelMimePart   *part)
{
  g_autoptr (CamelCipherContext) cipher = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelCipherValidity) validity = NULL;

  if (!parser->session)
    return FALSE;

  cipher = camel_smime_context_new (parser->session);

  validity = camel_cipher_context_verify_sync (cipher, CAMEL_MIME_PART (parser->message), parser->cancellable, &error);
  if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    g_warning ("%s: S/MIME signature verification failed: %s", G_STRFUNC, error->message);
  }

  if (validity) {
    if (parser->body)
      parser->body->is_signed = TRUE;

    process_signature_validation (parser, validity);
  }

  return validity != NULL;
}

static void
process_multipart (StampMimeParser *parser,
                   CamelMultipart  *multipart)
{
  guint num_parts;
  guint i;

  num_parts = camel_multipart_get_number (multipart);

  for (i = 0; i < num_parts; i++) {
    CamelMimePart *part;
    CamelContentType *content_type;
    const char *disposition;
    CamelDataWrapper *content;

    part = camel_multipart_get_part (multipart, i);
    if (!part)
      continue;

    content_type = camel_mime_part_get_content_type (part);
    disposition = camel_mime_part_get_disposition (part);
    content = camel_medium_get_content (CAMEL_MEDIUM (part));

    if (g_strcmp0 (disposition, "inline") == 0) {
      handle_inline_content (parser, part);
      continue;
    }

    if (g_strcmp0 (disposition, "attachment") == 0) {
      handle_attachment (parser, part);
      if (camel_mime_part_get_content_id (part))
        handle_inline_content (parser, part);
    }

    if (camel_mime_part_get_content_id (part) && content_type &&
        g_strcmp0 (content_type->type, "image") == 0) {
      handle_inline_content (parser, part);
    }

    if (!content_type)
      continue;

    if (g_strcmp0 (content_type->type, "multipart") == 0) {
      if (g_strcmp0 (content_type->subtype, "encrypted") == 0) {
        handle_smime_encrypted (parser, part);
      } else if (g_strcmp0 (content_type->subtype, "signed") == 0) {
        handle_smime_signature (parser, part);
      } else if (CAMEL_IS_MULTIPART (content)) {
        process_multipart (parser, CAMEL_MULTIPART (content));
      }
      continue;
    }

    if (g_strcmp0 (content_type->type, "text") == 0) {
      if (g_strcmp0 (content_type->subtype, "calendar") == 0) {
        handle_calendar_content (parser, content, content_type);
      } else {
        handle_text_content (parser, content, content_type);
      }
    } else if (g_strcmp0 (content_type->type, "application") == 0) {
      if (g_strcmp0 (content_type->subtype, "pgp-encrypted") == 0) {
        handle_pgp_encrypted (parser, part);
      } else if (g_strcmp0 (content_type->subtype, "pkcs7-mime") == 0 || g_strcmp0 (content_type->subtype, "x-pkcs7-mime") == 0) {
        handle_smime_encrypted (parser, part);
      } else if (g_strcmp0 (content_type->subtype, "pgp-signature") == 0) {
        handle_pgp_signature (parser, part);
      } else if (g_strcmp0 (content_type->subtype, "pkcs7-signature") == 0 || g_strcmp0 (content_type->subtype, "x-pkcs7-signature") == 0) {
        handle_smime_signature (parser, part);
      }
    } else if (g_strcmp0 (content_type->type, "multipart") == 0) {
      if (CAMEL_IS_MULTIPART (content)) {
        process_multipart (parser, CAMEL_MULTIPART (content));
      }
    } else if (g_strcmp0 (content_type->type, "message") == 0) {
      if (CAMEL_IS_MIME_MESSAGE (content)) {
        CamelMimeMessage *inner_msg = CAMEL_MIME_MESSAGE (content);
        CamelDataWrapper *inner_body = camel_medium_get_content (CAMEL_MEDIUM (inner_msg));

        if (inner_body) {
          if (CAMEL_IS_MULTIPART (inner_body)) {
            process_multipart (parser, CAMEL_MULTIPART (inner_body));
          } else if (CAMEL_IS_DATA_WRAPPER (inner_body)) {
            CamelContentType *body_ct = camel_mime_part_get_content_type (CAMEL_MIME_PART (inner_msg));

            if (body_ct && g_strcmp0 (body_ct->type, "text") == 0) {
              handle_text_content (parser, inner_body, body_ct);
            }
          }
        }
      }
    }
  }
}

static void
process_single_part (StampMimeParser *parser,
                     CamelMimePart   *part)
{
  CamelContentType *content_type;
  CamelDataWrapper *content;
  const char *disposition;

  content_type = camel_mime_part_get_content_type (part);
  disposition = camel_mime_part_get_disposition (part);
  content = camel_medium_get_content (CAMEL_MEDIUM (part));

  if (g_strcmp0 (disposition, "inline") == 0) {
    handle_inline_content (parser, part);
    return;
  }

  if (g_strcmp0 (disposition, "attachment") == 0) {
    handle_attachment (parser, part);

    if (camel_mime_part_get_content_id (part))
      handle_inline_content (parser, part);
  }

  if (camel_mime_part_get_content_id (part) && content_type && g_strcmp0 (content_type->type, "image") == 0 && g_strcmp0 (disposition, "inline") != 0 && g_strcmp0 (disposition, "attachment") != 0) {
    handle_inline_content (parser, part);
  }

  if (!content_type || !content)
    return;

  if (g_strcmp0 (content_type->type, "text") == 0) {
    if (g_strcmp0 (content_type->subtype, "calendar") == 0)
      handle_calendar_content (parser, content, content_type);
    else
      handle_text_content (parser, content, content_type);
  } else if (g_strcmp0 (content_type->type, "application") == 0) {
    if (g_strcmp0 (content_type->subtype, "pkcs7-mime") == 0 || g_strcmp0 (content_type->subtype, "x-pkcs7-mime") == 0) {
      handle_smime_encrypted (parser, part);
    }
  }
}

static void
check_root_encryption (StampMimeParser *parser)
{
  CamelContentType *content_type;
  CamelMimePart *part;

  part = CAMEL_MIME_PART (parser->message);
  content_type = camel_mime_part_get_content_type (part);
  if (!content_type)
    return;

  if (g_strcmp0 (content_type->type, "application") == 0) {
    if (g_strcmp0 (content_type->subtype, "pkcs7-mime") == 0 || g_strcmp0 (content_type->subtype, "x-pkcs7-mime") == 0) {
      handle_smime_encrypted (parser, part);
    } else if (g_strcmp0 (content_type->subtype, "pgp-encrypted") == 0) {
      handle_pgp_encrypted (parser, part);
    }
  }
}

static void
parse_list_unsubscribe (StampMimeParser *parser)
{
  const char *list_unsubscribe;
  const char *list_unsubscribe_post;

  list_unsubscribe = camel_medium_get_header (CAMEL_MEDIUM (parser->message), "List-Unsubscribe");
  if (!list_unsubscribe)
    return;

  list_unsubscribe_post = camel_medium_get_header (CAMEL_MEDIUM (parser->message), "List-Unsubscribe-Post");
  if (list_unsubscribe_post && g_str_equal (list_unsubscribe_post, "List-Unsubscribe=One-Click")) {
    char *url_start = strstr (list_unsubscribe, "<http");
    char *url_end = url_start ? strchr (url_start, '>') : NULL;

    if (url_start && url_end) {
      url_start += 1;
      parser->list_unsubscribe = stamp_mime_list_unsubscribe_new ();
      parser->list_unsubscribe->url = g_strndup (url_start, url_end - url_start);
      parser->list_unsubscribe->one_click = TRUE;
    }
  }
}

gboolean
stamp_mime_parser_parse (StampMimeParser *parser)
{
  CamelDataWrapper *content;

  g_return_val_if_fail (parser != NULL, FALSE);

  check_root_encryption (parser);
  parse_list_unsubscribe (parser);

  if (!parser->decryption_succeeded) {
    content = camel_medium_get_content (CAMEL_MEDIUM (parser->message));
    if (content) {
      g_clear_object (&parser->root_content);

      parser->root_content = g_object_ref (content);
    }
  }

  parser->is_multipart = CAMEL_IS_MULTIPART (parser->root_content);

  if (parser->is_multipart) {
    process_multipart (parser, CAMEL_MULTIPART (parser->root_content));
  } else if (!parser->body) {
    CamelMimePart *part = CAMEL_MIME_PART (parser->message);

    process_single_part (parser, part);
  }

  return TRUE;
}

StampMimeContent *
stamp_mime_parser_get_body (StampMimeParser *parser)
{
  g_return_val_if_fail (parser != NULL, NULL);
  return parser->body;
}

GPtrArray *
stamp_mime_parser_get_attachments (StampMimeParser *parser)
{
  g_return_val_if_fail (parser != NULL, NULL);
  return parser->attachments;
}

StampMimeValidation *
stamp_mime_parser_get_validation (StampMimeParser *parser)
{
  g_return_val_if_fail (parser != NULL, NULL);
  return parser->validation;
}

StampMimeCalendar *
stamp_mime_parser_get_calendar (StampMimeParser *parser)
{
  g_return_val_if_fail (parser != NULL, NULL);
  return parser->calendar;
}

gboolean
stamp_mime_parser_has_calendar (StampMimeParser *parser)
{
  g_return_val_if_fail (parser != NULL, FALSE);
  return parser->calendar != NULL && parser->calendar->ical != NULL;
}

gboolean
stamp_mime_parser_has_attachments (StampMimeParser *parser)
{
  g_return_val_if_fail (parser != NULL, FALSE);
  return parser->attachments->len > 0;
}

StampMimeListUnsubscribe *
stamp_mime_parser_get_list_unsubscribe (StampMimeParser *parser)
{
  g_return_val_if_fail (parser != NULL, NULL);
  return parser->list_unsubscribe;
}

static void
send_unsubscribe_callback (GObject      *source,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  g_autoptr (GBytes) bytes = NULL;
  g_autoptr (GError) error = NULL;

  bytes = soup_session_send_and_read_finish (SOUP_SESSION (source), res, &error);
  if (error) {
    g_warning ("%s: Failed to unsubscribe: %s", G_STRFUNC, error->message);
  } else {
    g_debug ("%s: Successfully unsubscribed", G_STRFUNC);
  }
}

void
stamp_mime_parser_send_unsubscribe (StampMimeParser *parser,
                                    GCancellable    *cancellable)
{
  g_autoptr (SoupSession) session = NULL;
  g_autoptr (SoupMessage) msg = NULL;

  g_return_if_fail (parser != NULL);
  g_return_if_fail (parser->list_unsubscribe != NULL);
  g_return_if_fail (parser->list_unsubscribe->url != NULL);

  session = soup_session_new ();
  msg = soup_message_new ("POST", parser->list_unsubscribe->url);

  if (!msg) {
    g_warning ("%s: Could not create SoupMessage for %s", G_STRFUNC, parser->list_unsubscribe->url);
    return;
  }

  soup_message_headers_replace (soup_message_get_request_headers (msg), "Content-Type", "application/x-www-form-urlencoded");

  soup_session_send_and_read_async (session,
                                    msg,
                                    G_PRIORITY_DEFAULT,
                                    cancellable,
                                    send_unsubscribe_callback,
                                    NULL);
}

GPtrArray *
stamp_mime_parser_get_inline_parts (StampMimeParser *parser)
{
  g_return_val_if_fail (parser != NULL, NULL);
  return parser->inline_parts;
}

static gboolean
is_valid_content_id (const char *cid)
{
  if (!cid)
    return FALSE;

  return g_str_has_prefix (cid, "<") && g_str_has_suffix (cid, ">");
}

static char *
normalize_content_id (const char *cid)
{
  if (!cid)
    return NULL;

  if (is_valid_content_id (cid))
    return g_strndup (cid + 1, strlen (cid) - 2);

  return g_strdup (cid);
}

char *
stamp_mime_parser_embed_inline_images (StampMimeParser *parser,
                                       const char      *html_content)
{
  GString *result;
  const char *p = html_content;
  guint i;

  if (!html_content || !parser->inline_parts || parser->inline_parts->len == 0)
    return g_strdup (html_content);

  result = g_string_new ("");

  while (*p) {
    if (p[0] == 's' && g_ascii_strncasecmp (p, "src=", 4) == 0) {
      const char *start = p + 4;
      char quote = '\0';

      while (*start && g_ascii_isspace (*start))
        start++;

      if (*start == '"' || *start == '\'') {
        quote = *start;
        start++;
      }

      if (g_ascii_strncasecmp (start, "cid:", 4) == 0) {
        const char *cid_start = start + 4;
        g_autofree char *cid = NULL;
        g_autofree char *norm_cid = NULL;
        const char *end_quote;

        if (*cid_start == '<')
          cid_start++;

        if (quote) {
          end_quote = strchr (cid_start, quote);
        } else {
          end_quote = cid_start;

          while (*end_quote && !g_ascii_isspace (*end_quote) && *end_quote != '>' && *end_quote != '"' && *end_quote != '\'')
            end_quote++;
        }

        if (end_quote && end_quote > cid_start) {
          if (end_quote[-1] == '>')
            end_quote--;

          cid = g_strndup (cid_start, end_quote - cid_start);
        }

        if (cid) {
          norm_cid = normalize_content_id (cid);

          for (i = 0; i < parser->inline_parts->len; i++) {
            StampMimeInlinePart *part = g_ptr_array_index (parser->inline_parts, i);

            if (part->content_id && norm_cid) {
              g_autofree char *norm_part_cid = normalize_content_id (part->content_id);

              if (g_strcmp0 (norm_part_cid, norm_cid) == 0) {
                gsize data_size;
                const guint8 *data = g_bytes_get_data (part->data, &data_size);
                g_autofree char *base64 = g_base64_encode (data, data_size);
                g_autofree char *data_uri = g_strdup_printf ("data:%s;base64,%s", part->content_type, base64);

                g_string_append_len (result, p, start - p);
                g_string_append (result, data_uri);

                p = end_quote;

                if (*p == '>')
                  p++;

                goto done_src;
              }
            }
          }
        }
      }

      g_string_append_len (result, p, start - p);
      p = start;
      continue;
    }

    g_string_append_c (result, *p);
    p++;

done_src:
  }

  return g_string_free_and_steal (result);
}
