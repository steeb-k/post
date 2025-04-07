/* stamp-mail-row.c
 *
 * Copyright 2024 Jan-Michael Brummer
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

#include "stamp-mail-message-list-item.h"
#include "stamp-mail-message-header.h"

#include "stamp-webview.h"

#include <webkit/webkit.h>

#include <camel/camel.h>

struct _StampMailMessageListItem {
  GtkListBoxRow parent_instance;

  GtkWidget *box;
  GtkWidget *header;

  const CamelMessageInfo *message_info;
  char *message_content;
  gboolean message_is_html;
  StampWebView *web_view;
  gboolean expanded;
  GtkWidget *secondary_revealer;
  gboolean message_loaded;
  GCancellable *cancellable;
  GtkWidget *stack;
};

G_DEFINE_FINAL_TYPE (StampMailMessageListItem, stamp_mail_message_list_item, GTK_TYPE_LIST_BOX_ROW)

enum {
  PROP_0,
  PROP_MESSAGE_INFO,
  PROP_EXPANDED,
  LAST_PROP
};

static char *
convert_to_utf8 (GMemoryOutputStream *os,
                 const char          *encoding)
{
  gsize num_bytes = g_memory_output_stream_get_size (os);
  gpointer bytes = g_memory_output_stream_steal_data (os);
  g_autoptr (GError) error = NULL;
  char *utf8 = NULL;

  if (!bytes)
    return NULL;

  if (encoding) {
    const char *iconv_encoding = camel_iconv_charset_name (encoding);

    g_print ("%s: %s %s\n", G_STRFUNC, encoding, iconv_encoding);
    if (iconv_encoding) {
      utf8 = g_convert (bytes, num_bytes, "UTF-8", iconv_encoding, NULL, NULL, &error);
      if (error) {
        g_warning ("Could not convert data: %s", error->message);
      }
    }
  }

  g_print ("utf8 %p\n", utf8);
  g_print ("utf8 valid  %d\n", g_utf8_validate (utf8, -1, NULL));
  if (!utf8 || !g_utf8_validate (utf8, -1, NULL)) {
    g_print ("No utf8 or validate failed...\n");
    utf8 = g_convert (bytes, num_bytes, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
  }

  return utf8;
}

static void
handle_text_mime (StampMailMessageListItem *self,
                  CamelDataWrapper         *part)
{
  CamelContentType *field = camel_data_wrapper_get_mime_type_field (part);

  if (!self->message_content || (!self->message_is_html && g_strcmp0 (field->subtype, "html") == 0)) {
      GOutputStream *os = g_memory_output_stream_new_resizable ();

      camel_data_wrapper_decode_to_output_stream_sync (part, G_OUTPUT_STREAM (os), NULL, NULL);
      g_output_stream_close (G_OUTPUT_STREAM (os), NULL, NULL);

      self->message_content = g_strdup (convert_to_utf8 (G_MEMORY_OUTPUT_STREAM (os), camel_content_type_param (field, "charset")));
    /* g_print ("Got: %s\n", self->message_content); */

      if (g_strcmp0 (field->subtype, "html") == 0)
        self->message_is_html = TRUE;
  }
}

static void
handle_inline_mime (StampMailMessageListItem *self,
                    CamelMimePart         *part)
{
  GByteArray *byte_array = g_byte_array_new ();
  CamelStream *os = camel_stream_mem_new ();
  GBytes *bytes;
  GInputStream *inline_stream;
  CamelDataWrapper *content;
  char *data;
  gsize size;


  if (!camel_mime_part_get_content_id (part))
    return;

  camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (os), byte_array);

  content = camel_medium_get_content (CAMEL_MEDIUM (part));
  camel_data_wrapper_decode_to_stream_sync (content, os, NULL, NULL);
  bytes = g_byte_array_free_to_bytes (byte_array);

  data = (char*)g_bytes_get_data (bytes, &size);
  g_print("%s: %s\n", G_STRFUNC, data);
  g_print ("%c%c%c%c  - %ld\n",
           data[0],
           data[1],
           data[2],
           data[3],
           size);

  inline_stream = g_memory_input_stream_new_from_bytes (bytes);

  stamp_webview_add_internal_resource (self->web_view, camel_mime_part_get_content_id (part), inline_stream);
}

static void
parse_mime_content (StampMailMessageListItem *self,
                    CamelDataWrapper         *mime_content)
{
  if (CAMEL_IS_MULTIPART (mime_content)) {
    CamelMultipart *content = CAMEL_MULTIPART (mime_content);

    for (guint idx = 0; idx < camel_multipart_get_number (content); idx++) {
      CamelMimePart *part = camel_multipart_get_part (content, idx);
      CamelContentType *field = camel_data_wrapper_get_mime_type_field (CAMEL_DATA_WRAPPER (part));

      g_print ("** %s **\n", camel_mime_part_get_disposition (part));
      if (g_strcmp0 (camel_mime_part_get_disposition (part), "inline") == 0) {
        handle_inline_mime (self, part);
      } else if (g_strcmp0 (camel_mime_part_get_disposition (part), "attachment") == 0) {
      }

      g_print ("// %s //\n", field->type);
      if (g_strcmp0 (field->type, "text") == 0) {
        handle_text_mime (self, camel_medium_get_content (CAMEL_MEDIUM (part)));
      } else if (g_strcmp0 (field->type, "multipart") == 0) {
        parse_mime_content (self, camel_medium_get_content (CAMEL_MEDIUM (part)));
      }
    }
  } else {
    handle_text_mime (self, mime_content);
  }
}

static void
open_message (StampMailMessageListItem *self,
              CamelMimeMessage         *message)
{
  parse_mime_content (self, camel_medium_get_content (CAMEL_MEDIUM (message)));

  if (self->message_is_html) {
    /* webkit_web_view_load_html (self->web_view, self->message_content, NULL); */
    g_print ("%s: webview %p\n", G_STRFUNC, self->web_view);
    g_print ("%d\n", WEBKIT_IS_WEB_VIEW (self->web_view));

    stamp_webview_load_html (self->web_view, self->message_content);
  } else {
    stamp_webview_load_plain_text (self->web_view, self->message_content);
  }
}
static void
on_get_message (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
  StampMailMessageListItem *self = STAMP_MAIL_MESSAGE_LIST_ITEM (user_data);
  g_autoptr (GError) error = NULL;
  CamelFolder *folder = CAMEL_FOLDER (source);
  CamelMimeMessage *message = camel_folder_get_message_finish (folder, res, &error);

  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "webview");
  if (error) {
    g_warning ("Could not get message: %s", error->message);
    return;
  }

  open_message (self, message);
}

static void
start_get_message (StampMailMessageListItem *self,
                   GAsyncReadyCallback       callback,
                   gpointer                  user_data)
{
  CamelFolderSummary *summary;
  CamelFolder *folder;
  /* CamelMimeMessage *message; */

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  self->cancellable = g_cancellable_new ();

  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "progress");

  g_object_get (G_OBJECT (self->message_info), "summary", &summary, NULL);
  g_print ("%s: %p\n", G_STRFUNC, summary);

  folder = camel_folder_summary_get_folder (summary);
  g_print ("%s: --> %p\n", G_STRFUNC, folder);

  camel_folder_get_message (folder, camel_message_info_get_uid (self->message_info), G_PRIORITY_DEFAULT, self->cancellable, on_get_message, self);
}

void
stamp_mail_message_list_item_set_expanded (StampMailMessageListItem *self,
                            gboolean      expanded)
{
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->secondary_revealer), expanded);

  self->expanded = expanded;

  if (expanded) {
    if (!self->message_loaded) {
      start_get_message (self, NULL, NULL);
      self->message_loaded = TRUE;
    }
  }
}

static void
stamp_mail_message_list_item_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  StampMailMessageListItem *self = STAMP_MAIL_MESSAGE_LIST_ITEM (object);

  switch (property_id) {
    case PROP_EXPANDED:
      g_value_set_boolean (value, gtk_revealer_get_reveal_child (GTK_REVEALER (self->secondary_revealer)));
      break;
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_mail_message_list_item_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  StampMailMessageListItem *self = STAMP_MAIL_MESSAGE_LIST_ITEM (object);

  switch (property_id) {
    case PROP_MESSAGE_INFO:
      self->message_info = g_value_get_object (value);
      break;
    case PROP_EXPANDED:
      stamp_mail_message_list_item_set_expanded (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_mail_message_list_item_constructed (GObject *object)
{
}

static void
stamp_message_list_item_dispose (GObject *object)
{
  StampMailMessageListItem *self = STAMP_MAIL_MESSAGE_LIST_ITEM (object);

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  G_OBJECT_CLASS (stamp_mail_message_list_item_parent_class)->dispose (object);
}

void
stamp_mail_message_list_item_class_init (StampMailMessageListItemClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  /* GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass); */

  gobject_class->dispose = stamp_message_list_item_dispose;

  gobject_class->get_property = stamp_mail_message_list_item_get_property;
  gobject_class->set_property = stamp_mail_message_list_item_set_property;
  gobject_class->constructed = stamp_mail_message_list_item_constructed;

  /* widget_class->expanded */

  g_object_class_install_property (gobject_class, PROP_MESSAGE_INFO,
                                   g_param_spec_object ("message-info",
                                                        NULL,
                                                        NULL,
                                                        CAMEL_TYPE_MESSAGE_INFO,
                                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
on_header_clicked (GObject  *object,
                   gint n_press,
  gdouble x,
  gdouble y,
                   gpointer  user_data)
{
  StampMailMessageListItem *self = STAMP_MAIL_MESSAGE_LIST_ITEM (user_data);

  stamp_mail_message_list_item_set_expanded (self, !self->expanded);
}

void
stamp_mail_message_list_item_init (StampMailMessageListItem *self)
{
  GtkGesture *header_gesture = gtk_gesture_click_new ();
  g_type_ensure (WEBKIT_TYPE_WEB_VIEW);

  gtk_widget_add_css_class (GTK_WIDGET (self), "card");
  gtk_widget_set_margin_top (GTK_WIDGET (self), 6);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self), 6);
  /* StampMailMessageListItem *self = STAMP_MAIL_MESSAGE_LIST_ITEM (object); */

  self->box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (self), self->box);

  /* gtk_box_append (GTK_BOX (self->box), gtk_label_new (camel_message_info_get_subject (self->message_info))); */
  self->header = stamp_mail_message_header_new ();
  g_signal_connect (header_gesture, "released", G_CALLBACK (on_header_clicked), self);
  gtk_widget_add_controller (self->header, GTK_EVENT_CONTROLLER (header_gesture));
  gtk_box_append (GTK_BOX (self->box), self->header);

  self->secondary_revealer = gtk_revealer_new ();

  self->web_view = stamp_webview_new ();

  self->stack = gtk_stack_new ();
  gtk_stack_add_named (GTK_STACK (self->stack), adw_spinner_new (), "progress");
  gtk_stack_add_named (GTK_STACK (self->stack), GTK_WIDGET (self->web_view), "webview");
  gtk_revealer_set_child (GTK_REVEALER (self->secondary_revealer), GTK_WIDGET (self->stack));

  gtk_widget_set_size_request (GTK_WIDGET (self->web_view), -1, 300);
  g_print ("%s: webview %p\n", G_STRFUNC, self->web_view);
  gtk_box_append (GTK_BOX (self->box), GTK_WIDGET (self->secondary_revealer));

  self->message_loaded = FALSE;
}

GtkWidget *
stamp_mail_message_list_item_new (const CamelMessageInfo *message)
{
  StampMailMessageListItem *ret = g_object_new (STAMP_TYPE_MAIL_MESSAGE_LIST_ITEM,
                                                "message-info", message,
                                                NULL);

  stamp_mail_message_header_set_mail (STAMP_MAIL_MESSAGE_HEADER (ret->header), ret->message_info);

  return GTK_WIDGET (ret);
}

