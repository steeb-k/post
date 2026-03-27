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

#include "stamp-composer.h"

#include "stamp-account.h"
#include "stamp-attachment-button.h"
#include "stamp-contact-completion.h"
#include "stamp-mail-view.h"
#include "stamp-session.h"
#include "stamp-signature.h"
#include "stamp-tag.h"
#include "stamp-webview.h"
#include "stamp-window.h"

#include <camel/camel.h>
#include <glib/gi18n.h>
#include <libebook-contacts/libebook-contacts.h>
#include <libedataserverui4/libedataserverui4.h>
#include <libebook/libebook.h>
#include <libedata-book/libedata-book.h>

struct _StampComposer {
  AdwApplicationWindow parent_instance;

  GtkWidget *webview_bin;
  StampWebView *webview;
  GtkWidget *from;
  GtkWidget *to;
  GtkWidget *cc;
  GtkWidget *bcc;
  GtkWidget *toggle;
  GtkWidget *send_button;
  GtkWidget *subject;
  GActionMap *action_map;
  GtkWidget *window_title;
  GtkWidget *attachment_revealer;
  GtkWidget *attachment_box;
  GtkWidget *request_disposition;
  GtkWidget *pgp_sign;
  GtkWidget *pgp_encrypt;
  GtkWidget *smime_sign;
  GtkWidget *smime_encrypt;
  GtkWidget *security_menu;
  StampComposerType type;
  CamelMimeMessage *orig_message;
  char *draft_uid;

  GList *attachments;
  StampAccount *account;
  gboolean is_dirty;

  GCancellable *cancellable;
  gboolean discard_draft;
  gint autosave_source_id;
};

G_DEFINE_FINAL_TYPE (StampComposer, stamp_composer, ADW_TYPE_APPLICATION_WINDOW)

enum {
  PROP_0,
  PROP_ACCOUNT,
  LAST_PROP
};

static void
on_query_command (GObject      *source,
                  const char   *command,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  StampComposer *self = user_data;
  g_autoptr (GError) error = NULL;
  gboolean ret;

  ret = stamp_web_view_query_command_state_finish (source, res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Could not query command state: %s", error->message);
    return;
  }

  g_action_group_change_action_state (G_ACTION_GROUP (self->action_map), command, g_variant_new_string (ret ? command : ""));
}

static void
on_query_bold_command (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  on_query_command (source, "bold", res, user_data);
}

static void
on_query_italic_command (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  on_query_command (source, "italic", res, user_data);
}

static void
on_query_underline_command (GObject      *source,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  on_query_command (source, "underline", res, user_data);
}

static void
on_query_strikethrough_command (GObject      *source,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  on_query_command (source, "strikethrough", res, user_data);
}

static void
update_actions (StampComposer *self)
{
  stamp_web_view_query_command_state (self->webview, "bold", self->cancellable, on_query_bold_command, self);
  stamp_web_view_query_command_state (self->webview, "italic", self->cancellable, on_query_italic_command, self);
  stamp_web_view_query_command_state (self->webview, "underline", self->cancellable, on_query_underline_command, self);
  stamp_web_view_query_command_state (self->webview, "strikethrough", self->cancellable, on_query_strikethrough_command, self);
}

static void
on_edit_activate (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);
  const char *command = g_variant_get_string (parameter, NULL);

  webkit_web_view_execute_editing_command (WEBKIT_WEB_VIEW (self->webview), command);
  update_actions (self);
}

static CamelMimeMessage *
build_message (StampComposer *self,
               const char    *body_html)
{
  CamelStream *stream_mem = camel_stream_mem_new_with_buffer (body_html, strlen (body_html));
  CamelStream *stream_filter = camel_stream_filter_new (stream_mem);
  CamelDataWrapper *html = camel_data_wrapper_new ();
  CamelMimePart *part;
  CamelMultipart *body;
  CamelMimeMessage *message;

  camel_data_wrapper_construct_from_stream_sync (html, stream_filter, self->cancellable, NULL);
  camel_data_wrapper_set_mime_type (html, "text/html; charset=utf-8");

  part = camel_mime_part_new ();
  camel_medium_set_content (CAMEL_MEDIUM (part), CAMEL_DATA_WRAPPER (html));
  camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);

  body = camel_multipart_new ();
  camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (body), "multipart/alternative");
  camel_multipart_set_boundary (body, NULL);
  camel_multipart_add_part (body, part);

  for (GList *attachment = self->attachments; attachment && attachment->data; attachment = g_list_next (attachment)) {
    StampAttachmentButton *button = STAMP_ATTACHMENT_BUTTON (attachment->data);

    part = stamp_attachment_button_get_mime_part (button);
    camel_multipart_add_part (body, part);
  }

  message = camel_mime_message_new ();
  camel_mime_message_set_subject (message, gtk_editable_get_text (GTK_EDITABLE (self->subject)));
  camel_mime_message_set_date (message, CAMEL_MESSAGE_DATE_CURRENT, 0);

  if (self->type == STAMP_COMPOSER_REPLY || self->type == STAMP_COMPOSER_REPLY_ALL) {
    const char *msgid = camel_mime_message_get_message_id (self->orig_message);
    const char *refs = camel_medium_get_header (CAMEL_MEDIUM (self->orig_message), "References");
    CamelDataWrapper *content;

    content = camel_medium_get_content (CAMEL_MEDIUM (self->orig_message));
    if (CAMEL_IS_MULTIPART (content)) {
      CamelMultipart *mp = CAMEL_MULTIPART (content);
      int n = camel_multipart_get_number (mp);

      for (int i = 0; i < n; i++) {
        CamelMimePart *multi_part = camel_multipart_get_part (mp, i);
        CamelContentType *ctype = camel_mime_part_get_content_type (multi_part);

        if (ctype) {
          if (g_strcmp0 (ctype->type, "multipart") == 0 && g_strcmp0 (ctype->subtype, "related") == 0) {
            int nr;
            nr = camel_multipart_get_number (CAMEL_MULTIPART (camel_medium_get_content (CAMEL_MEDIUM (multi_part))));
            for (int j = 0; j < nr; j++) {
              CamelMimePart *img = camel_multipart_get_part (CAMEL_MULTIPART (camel_medium_get_content (CAMEL_MEDIUM (multi_part))), j);

              if (camel_mime_part_get_content_id (img))
                camel_multipart_add_part (body, img);
            }
          } else if (g_strcmp0 (ctype->type, "image") == 0) {
            camel_multipart_add_part (body, multi_part);
          }
        }
      }
    }

    if (msgid)
      camel_medium_set_header (CAMEL_MEDIUM (message), "In-Reply-To", msgid);

    if (refs && msgid) {
      g_autofree char *newrefs = g_strdup_printf ("%s %s", refs, msgid);

      camel_medium_set_header (CAMEL_MEDIUM (message), "References", newrefs);
    }
  }

  camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (body));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->request_disposition))) {
    CamelInternetAddress *address = stamp_account_get_address (self->account);
    const char *name;
    const char *email;

    camel_internet_address_get (address, 0, &name, &email);
    camel_medium_set_header (CAMEL_MEDIUM (message), "Disposition-Notification-To", email);
  }

  return message;
}

static CamelInternetAddress *
build_sender (CamelMimeMessage *message,
              const char       *name,
              const char       *mail)
{
  CamelInternetAddress *sender = camel_internet_address_new ();

  camel_internet_address_add (sender, name, mail);
  camel_mime_message_set_from (message, sender);

  return sender;
}

static CamelInternetAddress *
build_recipients (StampComposer    *self,
                  CamelMimeMessage *message)
{
  CamelInternetAddress *recipients = camel_internet_address_new ();
  CamelInternetAddress *cc_recipients;
  GList *tags;

  tags = stamp_contact_completion_get_tags (STAMP_CONTACT_COMPLETION (self->to));
  for (GList *iter = tags; iter && iter->data; iter = g_list_next (iter)) {
    StampTag *tag = STAMP_TAG (iter->data);
    CamelInternetAddress *to_addresses = camel_internet_address_new ();

    camel_internet_address_add (to_addresses, stamp_tag_get_label (tag), stamp_tag_get_mail (tag));
    camel_address_cat (CAMEL_ADDRESS (recipients), CAMEL_ADDRESS (to_addresses));
  }

  camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, recipients);

  cc_recipients = camel_internet_address_new ();
  tags = stamp_contact_completion_get_tags (STAMP_CONTACT_COMPLETION (self->cc));
  for (GList *iter = tags; iter && iter->data; iter = g_list_next (iter)) {
    StampTag *tag = STAMP_TAG (iter->data);
    CamelInternetAddress *to_addresses = camel_internet_address_new ();

    camel_internet_address_add (to_addresses, stamp_tag_get_label (tag), stamp_tag_get_mail (tag));
    camel_address_cat (CAMEL_ADDRESS (cc_recipients), CAMEL_ADDRESS (to_addresses));
  }

  if (camel_address_length (CAMEL_ADDRESS (cc_recipients)) > 0) {
    camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_CC, cc_recipients);
  }

  return recipients;
}

static void
on_send_mail (GObject      *account,
              GAsyncResult *res,
              gpointer      user_data)
{
  StampComposer *self = user_data;
  StampWindow *window = STAMP_WINDOW (stamp_get_main_window ());
  StampMailView *mail_view = stamp_window_get_mail_view (window);
  g_autoptr (GError) error = NULL;

  if (!stamp_account_send_mail_finish (STAMP_ACCOUNT (account), res, &error)) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to send mail: %s", error->message);
    return;
  }

  self->is_dirty = FALSE;
  stamp_mail_view_show_toast (mail_view, _("Mail sent"));
  gtk_window_destroy (GTK_WINDOW (self));
}

static void
apply_crypto (CamelSession *session,
              CamelMimeMessage *mime_message,
              CamelInternetAddress *recipient,
              gboolean pgp_sign,
              gboolean pgp_encrypt,
              gboolean smime_sign,
              gboolean smime_encrypt,
              GCancellable *cancellable)
{
  g_autoptr (GError) error = NULL;

  if (pgp_sign || pgp_encrypt) {
    CamelCipherContext *cipher;
    CamelMimePart *ipart;
    CamelMimePart *opart;
    GPtrArray *recipients_list;

    cipher = camel_gpg_context_new (session);

    ipart = CAMEL_MIME_PART (mime_message);
    opart = camel_mime_part_new ();

    if (pgp_sign) {
      gboolean success = camel_cipher_context_sign_sync (cipher, NULL, CAMEL_CIPHER_HASH_SHA256,
                                                         ipart, opart,
                                                         cancellable, &error);
      if (!success || error) {
        if (error) {
          g_warning ("PGP signing failed: %s", error->message);
          g_error_free (error);
          error = NULL;
        }
      } else {
        g_object_unref (mime_message);
        mime_message = CAMEL_MIME_MESSAGE (opart);
        ipart = CAMEL_MIME_PART (mime_message);
        opart = camel_mime_part_new ();
      }
    }

    if (pgp_encrypt && !error) {
      gboolean success;

      recipients_list = g_ptr_array_new ();
      for (int i = 0; i < camel_address_length (CAMEL_ADDRESS (recipient)); i++) {
        const char *r_name, *r_mail;
        if (camel_internet_address_get (recipient, i, &r_name, &r_mail) && r_mail) {
          g_ptr_array_add (recipients_list, g_strdup (r_mail));
        }
      }

      success = camel_cipher_context_encrypt_sync (cipher, NULL, recipients_list,
                                                   ipart, opart,
                                                   cancellable, &error);
      g_ptr_array_free (recipients_list, TRUE);

      if (!success || error) {
        if (error) {
          g_warning ("PGP encryption failed: %s", error->message);
          g_error_free (error);
          error = NULL;
        }
      } else {
        g_object_unref (mime_message);
        mime_message = CAMEL_MIME_MESSAGE (opart);
      }
    } else {
      g_object_unref (opart);
    }

    g_object_unref (cipher);
  }

  if (smime_sign || smime_encrypt) {
    CamelCipherContext *cipher;
    CamelMimePart *ipart;
    CamelMimePart *opart;
    GPtrArray *recipients_list;

    cipher = camel_smime_context_new (session);

    ipart = CAMEL_MIME_PART (mime_message);
    opart = camel_mime_part_new ();

    if (smime_sign) {
      gboolean success = camel_cipher_context_sign_sync (cipher, NULL, CAMEL_CIPHER_HASH_SHA256,
                                                         ipart, opart,
                                                         cancellable, &error);
      if (!success || error) {
        if (error) {
          g_warning ("S/MIME signing failed: %s", error->message);
          g_error_free (error);
          error = NULL;
        }
      } else {
        g_object_unref (mime_message);
        mime_message = CAMEL_MIME_MESSAGE (opart);
        ipart = CAMEL_MIME_PART (mime_message);
        opart = camel_mime_part_new ();
      }
    }

    if (smime_encrypt && !error) {
      gboolean success;

      recipients_list = g_ptr_array_new ();
      for (int i = 0; i < camel_address_length (CAMEL_ADDRESS (recipient)); i++) {
        const char *r_name, *r_mail;
        if (camel_internet_address_get (recipient, i, &r_name, &r_mail) && r_mail) {
          g_ptr_array_add (recipients_list, g_strdup (r_mail));
        }
      }

      success = camel_cipher_context_encrypt_sync (cipher, NULL, recipients_list,
                                                   ipart, opart,
                                                   cancellable, &error);
      g_ptr_array_free (recipients_list, TRUE);

      if (!success || error) {
        if (error) {
          g_warning ("S/MIME encryption failed: %s", error->message);
          g_error_free (error);
          error = NULL;
        }
      } else {
        g_object_unref (mime_message);
        mime_message = CAMEL_MIME_MESSAGE (opart);
      }
    } else {
      g_object_unref (opart);
    }

    g_object_unref (cipher);
  }
}

static void
on_get_body_html (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  StampComposer *self = user_data;
  StampWebView *web_view = STAMP_WEB_VIEW (source_object);
  g_autoptr (GError) error = NULL;
  g_autofree char *body = stamp_webview_get_body_html_finish (web_view, res, &error);
  CamelMimeMessage *mime_message;
  CamelInternetAddress *sender;
  CamelInternetAddress *recipient;
  const char *name;
  const char *mail;
  CamelInternetAddress *addresses;
  gboolean do_pgp_sign;
  gboolean do_pgp_encrypt;
  gboolean do_smime_sign;
  gboolean do_smime_encrypt;

  if (!body) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to get HTML content for mail: %s", error->message);
    return;
  }

  mime_message = build_message (self, body);

  addresses = stamp_account_get_address (self->account);

  camel_internet_address_get (addresses, 0, &name, &mail);
  sender = build_sender (mime_message, name, mail);
  recipient = build_recipients (self, mime_message);

  if (self->security_menu && !self->pgp_sign) {
    GtkWidget *popover = GTK_WIDGET (gtk_menu_button_get_popover (GTK_MENU_BUTTON (self->security_menu)));
    if (popover) {
      GtkWidget *box = gtk_widget_get_first_child (popover);
      while (box && !GTK_IS_BOX (box))
        box = gtk_widget_get_next_sibling (box);

      if (box) {
        GtkWidget *child = gtk_widget_get_first_child (box);
        while (child && !self->pgp_sign) {
          if (GTK_IS_BOX (child)) {
            GtkWidget *toggle = gtk_widget_get_first_child (child);
            while (toggle && !self->pgp_sign) {
              const char *widget_name = gtk_widget_get_name (toggle);
              if (GTK_IS_TOGGLE_BUTTON (toggle)) {
                if (g_strcmp0 (widget_name, "pgp_sign") == 0)
                  self->pgp_sign = toggle;
                else if (g_strcmp0 (widget_name, "pgp_encrypt") == 0)
                  self->pgp_encrypt = toggle;
                else if (g_strcmp0 (widget_name, "smime_sign") == 0)
                  self->smime_sign = toggle;
                else if (g_strcmp0 (widget_name, "smime_encrypt") == 0)
                  self->smime_encrypt = toggle;
              }
              toggle = gtk_widget_get_next_sibling (toggle);
            }
          }
          child = gtk_widget_get_next_sibling (child);
        }
      }
    }
  }

  do_pgp_sign = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->pgp_sign));
  do_pgp_encrypt = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->pgp_encrypt));
  do_smime_sign = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->smime_sign));
  do_smime_encrypt = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->smime_encrypt));

  if (do_pgp_sign || do_pgp_encrypt || do_smime_sign || do_smime_encrypt) {
    CamelSession *session = CAMEL_SESSION (stamp_session_get_default ());

    apply_crypto (session, mime_message, recipient, do_pgp_sign, do_pgp_encrypt, do_smime_sign, do_smime_encrypt, self->cancellable);
  }

  stamp_account_send_mail (self->account, mime_message, sender, recipient,
                           do_pgp_sign || do_smime_sign, do_pgp_encrypt || do_smime_encrypt, self->cancellable, on_send_mail, self);
}

static void
stamp_composer_set_account (StampComposer *self,
                            StampAccount  *account)
{
  GListModel *model = gtk_drop_down_get_model (GTK_DROP_DOWN (self->from));

  if (!model)
    return;

  for (guint idx = 0; idx < g_list_model_get_n_items (model); idx++) {
    StampAccount *acc = STAMP_ACCOUNT (g_list_model_get_item (model, idx));

    if (acc == account) {
      gtk_drop_down_set_selected (GTK_DROP_DOWN (self->from), idx);
      self->account = account;
      stamp_contact_completion_set_account (STAMP_CONTACT_COMPLETION (self->to), self->account);
      stamp_contact_completion_set_account (STAMP_CONTACT_COMPLETION (self->cc), self->account);
      stamp_contact_completion_set_account (STAMP_CONTACT_COMPLETION (self->bcc), self->account);
    }
  }
}

static void
on_send_activated (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);

  gtk_widget_set_sensitive (self->send_button, FALSE);

  if (strlen (gtk_editable_get_text (GTK_EDITABLE (self->subject))) == 0) {
    AdwDialog *dialog = adw_alert_dialog_new ("Send without subject?", "This message has an empty subject field. The recipient may be unable to infer its scope or importance.");

    adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                    "cancel", ("_Cancel"),
                                    "send-anyway", ("Send Anyway"),
                                    NULL);

    adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                              "send-anyway",
                                              ADW_RESPONSE_DESTRUCTIVE);

    adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
    adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

    adw_dialog_present (dialog, GTK_WIDGET (self));
  } else {
    stamp_webview_get_body_html (self->webview, NULL, on_get_body_html, self);
  }
}

static void
on_remove_format_activated (GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);

  stamp_web_view_execute_editor_command (self->webview, "removeformat", "");
  stamp_web_view_execute_editor_command (self->webview, "unlink", "");
}

static void
on_insert_signature_activated (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);
  StampSession *session = stamp_session_get_default ();
  GList *signatures = stamp_session_get_signatures (session);
  StampSignature *signature;

  if (!signatures)
    return;

  signature = signatures->data;
  stamp_web_view_execute_editor_command (self->webview, "insertHTML", stamp_signature_get_content (signature));
}

static void
on_attachment_added (GObject      *obj,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG (obj);
  g_autoptr (GError) error = NULL;
  g_autoptr (GFile) file = NULL;
  GtkWidget *button;

  file = gtk_file_dialog_open_finish (dialog, res, &error);
  if (error) {
    g_warning ("%s: Could not get attachment file: %s", G_STRFUNC, error->message);
    return;
  }

  button = stamp_attachment_button_new_from_file (file);
  self->attachments = g_list_append (self->attachments, button);

  adw_wrap_box_append (ADW_WRAP_BOX (self->attachment_box), button);
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->attachment_revealer), TRUE);
}

static void
on_add_attachment_activated (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);
  GtkFileDialog *dialog = gtk_file_dialog_new ();

  gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL, on_attachment_added, self);
}

static GActionEntry composer_entries[] = {
  { "bold", on_edit_activate, "s", "''", NULL},
  { "italic", on_edit_activate, "s", "''", NULL},
  { "underline", on_edit_activate, "s", "''", NULL},
  { "strikethrough", on_edit_activate, "s", "''", NULL},
  { "send", on_send_activated },
  { "remove-format", on_remove_format_activated },
  { "insert-signature", on_insert_signature_activated },
  { "add-attachment", on_add_attachment_activated },
};

static void
load_from_combobox (StampComposer *self)
{
  GListStore *store;
  GList *accounts;

  accounts = stamp_session_get_accounts (stamp_session_get_default ());

  store = g_list_store_new (STAMP_TYPE_ACCOUNT);

  for (GList *iter = accounts; iter && iter->data; iter = g_list_next (iter)) {
    StampAccount *account = STAMP_ACCOUNT (iter->data);
    StampMailService *service = stamp_account_get_mail_service (account);

    if (!service || !stamp_mail_service_get_enabled (service))
      continue;

    g_list_store_append (store, account);
  }

  if (!self->account && accounts)
    stamp_composer_set_account (self, accounts->data);

  gtk_drop_down_set_model (GTK_DROP_DOWN (self->from), G_LIST_MODEL (store));
}

static void
on_from_setup (GtkSignalListItemFactory *f,
               GtkListItem              *item,
               gpointer                  data)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *name;
  GtkWidget *email;

  gtk_widget_set_halign (box, GTK_ALIGN_START);
  gtk_widget_set_hexpand (box, TRUE);
  gtk_widget_set_size_request (box, 300, -1);

  name = gtk_label_new (NULL);
  gtk_widget_set_hexpand (name, TRUE);
  gtk_widget_set_halign (name, GTK_ALIGN_START);
  gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_END);
  gtk_box_append (GTK_BOX (box), name);
  g_object_set_data (G_OBJECT (box), "name", name);

  email = gtk_label_new (NULL);
  gtk_widget_set_hexpand (email, TRUE);
  gtk_label_set_ellipsize (GTK_LABEL (email), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign (email, GTK_ALIGN_START);
  gtk_widget_add_css_class (email, "dim-label");
  gtk_widget_add_css_class (email, "smaller");
  gtk_box_append (GTK_BOX (box), email);
  g_object_set_data (G_OBJECT (box), "email", email);

  gtk_list_item_set_child (item, box);
}

static void
on_from_bind (GtkSignalListItemFactory *f,
              GtkListItem              *item,
              gpointer                  user_data)
{
  StampAccount *account = STAMP_ACCOUNT (gtk_list_item_get_item (item));
  CamelInternetAddress *address = stamp_account_get_address (account);
  GtkWidget *box = gtk_list_item_get_child (item);
  GtkWidget *name = g_object_get_data (G_OBJECT (box), "name");
  GtkWidget *email = g_object_get_data (G_OBJECT (box), "email");
  const char *name_str;
  const char *email_str;

  camel_internet_address_get (address, 0, &name_str, &email_str);
  name_str = stamp_account_get_name (account);
  gtk_label_set_text (GTK_LABEL (name), name_str ? name_str : email_str);
  gtk_label_set_text (GTK_LABEL (email), email_str);
}

static void
on_setup_selected (GtkSignalListItemFactory *f,
                   GtkListItem              *item,
                   gpointer                  data)
{
  GtkWidget *name;

  name = gtk_label_new (NULL);
  gtk_list_item_set_child (item, name);
}

static void
on_bind_selected (GtkSignalListItemFactory *f,
                  GtkListItem              *item,
                  gpointer                  user_data)
{
  StampAccount *account = STAMP_ACCOUNT (gtk_list_item_get_item (item));
  CamelInternetAddress *address = stamp_account_get_address (account);
  GtkWidget *name = gtk_list_item_get_child (item);
  const char *name_str;
  const char *email_str;

  camel_internet_address_get (address, 0, &name_str, &email_str);
  name_str = stamp_account_get_name (account);
  gtk_label_set_text (GTK_LABEL (name), name_str);
}

static void
mark_dirty (StampComposer *self);

static void
on_subject_changed (GtkWidget *entry,
                    gpointer   user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);
  const char *text = gtk_editable_get_text (GTK_EDITABLE (entry));

  if (text && strlen (text) > 0) {
    adw_window_title_set_title (ADW_WINDOW_TITLE (self->window_title), text);
  } else {
    adw_window_title_set_title (ADW_WINDOW_TITLE (self->window_title), _("New Message"));
  }

  mark_dirty (self);
}

static void
on_from_selected_item (GObject    *source,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);
  StampAccount *account = STAMP_ACCOUNT (gtk_drop_down_get_selected_item (GTK_DROP_DOWN (self->from)));

  if (!account)
    return;

  stamp_composer_set_account (self, account);
}

static void
on_auto_save_get_body_html (GObject      *source_object,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  StampComposer *self = user_data;
  StampWebView *web_view = STAMP_WEB_VIEW (source_object);
  g_autoptr (GError) error = NULL;
  g_autofree char *body = stamp_webview_get_body_html_finish (web_view, res, &error);
  CamelMimeMessage *mime_message;
  CamelInternetAddress *sender;
  CamelInternetAddress *recipient;
  const char *name;
  const char *mail;
  CamelInternetAddress *addresses;

  if (!body) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to get HTML content for mail: %s", error->message);
    return;
  }

  mime_message = build_message (self, body);

  addresses = stamp_account_get_address (self->account);

  camel_internet_address_get (addresses, 0, &name, &mail);
  sender = build_sender (mime_message, name, mail);
  recipient = build_recipients (self, mime_message);

  self->draft_uid = stamp_account_save_draft (self->account, self->draft_uid, mime_message, sender, recipient);
  if (self->draft_uid) {
    g_autofree char *save_str = NULL;
    GDateTime *dt = g_date_time_new_now_local ();
    g_autofree char *time = g_date_time_format (dt, "%H:%M");

    save_str = g_strdup_printf ("Saved Draft (%s)", time);
    adw_window_title_set_subtitle (ADW_WINDOW_TITLE (self->window_title), save_str);
  }
}

static gboolean
autosave_timeout_cb (gpointer user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);

  self->autosave_source_id = 0;
  stamp_webview_get_body_html (self->webview, self->cancellable, on_auto_save_get_body_html, self);

  return G_SOURCE_REMOVE;
}

static void
mark_dirty (StampComposer *self)
{
  self->is_dirty = TRUE;

  if (self->autosave_source_id)
    g_source_remove (self->autosave_source_id);

  self->autosave_source_id = g_timeout_add_seconds (10, autosave_timeout_cb, self);
}

static void
on_dirty_message (WebKitUserContentManager *manager,
                  WebKitUserMessage        *message,
                  gpointer                  user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);
  mark_dirty (self);
}

static void
on_load_changed (WebKitWebView   *view,
                 WebKitLoadEvent  load_event,
                 gpointer         user_data)
{
  if (load_event != WEBKIT_LOAD_FINISHED)
    return;

  webkit_web_view_evaluate_javascript (view, "document.body.addEventListener('input', () => {"
                                       "    window.webkit.messageHandlers.dirty.postMessage('');"
                                       "});",
                                       -1, NULL, NULL, NULL, NULL, NULL);
}

static void
stamp_composer_init (StampComposer *self)
{
  GBytes *template;
  GtkListItemFactory *factory;
  GtkListItemFactory *selected_factory;
  WebKitUserContentManager *manager;
  g_autofree char *tmp = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->type = STAMP_COMPOSER_NEW;
  self->cancellable = g_cancellable_new ();
  self->webview = stamp_webview_new ();
  stamp_webview_set_editable (self->webview);
  gtk_widget_set_focusable (GTK_WIDGET (self->webview), TRUE);
  adw_bin_set_child (ADW_BIN (self->webview_bin), GTK_WIDGET (self->webview));

  template = g_resources_lookup_data ("/org/tabos/stamp/blank-message-template.html", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
  tmp = g_strdup_printf (g_bytes_get_data (template, NULL), "");
#pragma GCC diagnostic pop

  webkit_web_view_load_html (WEBKIT_WEB_VIEW (self->webview), tmp, NULL);
  /* g_signal_connect_object (self->webview, "selection-changed", G_CALLBACK (on_selection_changed), self, 0); */

  self->action_map = G_ACTION_MAP (self);
  g_action_map_add_action_entries (self->action_map,
                                   composer_entries, G_N_ELEMENTS (composer_entries),
                                   self);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (on_setup_selected), self);
  g_signal_connect (factory, "bind", G_CALLBACK (on_bind_selected), self);

  gtk_drop_down_set_factory (GTK_DROP_DOWN (self->from), factory);

  selected_factory = gtk_signal_list_item_factory_new ();
  g_signal_connect_object (selected_factory, "setup", G_CALLBACK (on_from_setup), self, 0);
  g_signal_connect_object (selected_factory, "bind", G_CALLBACK (on_from_bind), self, 0);
  gtk_drop_down_set_list_factory (GTK_DROP_DOWN (self->from), selected_factory);

  g_signal_connect_object (self->from, "notify::selected-item", G_CALLBACK (on_from_selected_item), self, 0);
  load_from_combobox (self);

  g_object_bind_property (self->to, "has-entries", self->send_button, "sensitive", G_BINDING_DEFAULT);
  gtk_widget_grab_focus (self->to);

  manager = webkit_web_view_get_user_content_manager (WEBKIT_WEB_VIEW (self->webview));
  webkit_user_content_manager_register_script_message_handler (manager, "dirty", NULL);
  g_signal_connect_object (manager, "script-message-received::dirty", G_CALLBACK (on_dirty_message), self, 0);

  g_signal_connect (self->webview, "load-changed", G_CALLBACK (on_load_changed), self);

  /* self->auto_save_draft_handler = g_timeout_add_seconds (3, auto_save_draft, self); */

  self->cancellable = g_cancellable_new ();
}

static void
on_close_button_clicked (GtkWidget *button,
                         gpointer   user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);

  gtk_window_close (GTK_WINDOW (self));
}

static void
on_draft_get_body_html (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  StampComposer *self = user_data;
  StampWebView *web_view = STAMP_WEB_VIEW (source_object);
  g_autoptr (GError) error = NULL;
  g_autofree char *body = stamp_webview_get_body_html_finish (web_view, res, &error);
  CamelMimeMessage *mime_message;
  CamelInternetAddress *sender;
  CamelInternetAddress *recipient;
  const char *name;
  const char *mail;
  CamelInternetAddress *addresses;

  if (!body) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to get HTML content for mail: %s", error->message);
    return;
  }

  mime_message = build_message (self, body);

  addresses = stamp_account_get_address (self->account);

  camel_internet_address_get (addresses, 0, &name, &mail);
  sender = build_sender (mime_message, name, mail);
  recipient = build_recipients (self, mime_message);

  if (stamp_account_save_draft (self->account, self->draft_uid, mime_message, sender, recipient)) {
    gtk_window_destroy (GTK_WINDOW (self));
  }
}

static void
on_draft_response (AdwAlertDialog *dialog,
                   gchar          *response,
                   gpointer        user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);

  if (g_strcmp0 (response, "save-draft") == 0) {
    if (!self->is_dirty)
      stamp_webview_get_body_html (self->webview, self->cancellable, on_draft_get_body_html, self);
    else
      gtk_window_destroy (GTK_WINDOW (self));
  } else if (g_strcmp0 (response, "close") == 0) {
    stamp_account_remove_draft (self->account, self->draft_uid);
    gtk_window_destroy (GTK_WINDOW (self));
  }
}

static gboolean
on_close_request (GtkWindow *source,
                  gpointer   user_data)
{
  StampComposer *self = STAMP_COMPOSER (user_data);
  AdwDialog *dialog;

  g_clear_handle_id (&self->autosave_source_id, g_source_remove);

  if (!self->is_dirty)
    return FALSE;

  dialog = adw_alert_dialog_new (_("Changes detected"), _("You have modified this mail and closing this window will lead to loss of those changes. How do you want to proceed?"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", _("Close"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "save-draft", _("Save Draft"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "continue", _("Continue Editing"));
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "continue", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "close", ADW_RESPONSE_DESTRUCTIVE);

  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "save-draft");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "save-draft");

  g_signal_connect (dialog, "response", G_CALLBACK (on_draft_response), self);
  adw_dialog_present (dialog, GTK_WIDGET (source));

  return TRUE;
}

static void
stamp_composer_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  StampComposer *self = STAMP_COMPOSER (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      g_value_set_object (value, self->account);
      break;
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_composer_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  StampComposer *self = STAMP_COMPOSER (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      stamp_composer_set_account (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_composer_dispose (GObject *object)
{
  StampComposer *self = STAMP_COMPOSER (object);

  g_clear_handle_id (&self->autosave_source_id, g_source_remove);
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (stamp_composer_parent_class)->dispose (object);
}

static void
stamp_composer_class_init (StampComposerClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/composer/stamp-composer.ui");

  object_class->get_property = stamp_composer_get_property;
  object_class->set_property = stamp_composer_set_property;
  object_class->dispose = stamp_composer_dispose;

  gtk_widget_class_bind_template_child (widget_class, StampComposer, from);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, to);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, cc);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, bcc);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, webview_bin);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, send_button);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, subject);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, window_title);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, attachment_revealer);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, attachment_box);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, request_disposition);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, pgp_sign);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, pgp_encrypt);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, smime_sign);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, smime_encrypt);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, security_menu);
  gtk_widget_class_bind_template_child (widget_class, StampComposer, toggle);

  gtk_widget_class_bind_template_callback (widget_class, on_close_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_close_request);
  gtk_widget_class_bind_template_callback (widget_class, on_subject_changed);

  g_object_class_install_property (object_class, PROP_ACCOUNT,
                                   g_param_spec_object ("account",
                                                        NULL,
                                                        NULL,
                                                        STAMP_TYPE_ACCOUNT,
                                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

GtkWidget *
stamp_composer_new (StampAccount *account)
{
  return g_object_new (STAMP_TYPE_COMPOSER,
                       "account", account,
                       NULL);
}

static gboolean
stamp_add_addresses_to_completion (StampComposer        *self,
                                   GtkWidget            *wrap_box,
                                   CamelInternetAddress *address)
{
  int len;

  len = camel_address_length (CAMEL_ADDRESS (address));

  for (int idx = 0; idx < len; idx++) {
    GtkWidget *tag = stamp_tag_new (self->account);
    const char *ia_name;
    const char *ia_address;
    const char *to = NULL;

    camel_internet_address_get (address, idx, &ia_name, &ia_address);
    if (g_strcmp0 (ia_name, "") != 0) {
      to = ia_name;
    } else {
      to = ia_address;
    }

    stamp_tag_set_label (STAMP_TAG (tag), to);
    stamp_tag_set_mail (STAMP_TAG (tag), ia_address);
    stamp_tag_set_show_button (STAMP_TAG (tag), TRUE);

    stamp_contact_completion_add_tag (STAMP_CONTACT_COMPLETION (wrap_box), STAMP_TAG (tag));
  }

  return len != 0;
}

static void
remove_own_address (StampComposer        *self,
                    CamelInternetAddress *address)
{
  CamelInternetAddress *own_address = stamp_account_get_address (self->account);
  const char *own_name;
  const char *own_mail;
  int len;
  int idx;

  if (!own_address)
    return;

  camel_internet_address_get (own_address, 0, &own_name, &own_mail);

  len = camel_address_length (CAMEL_ADDRESS (address));

  for (idx = 0; idx < len; idx++) {
    const char *ia_name;
    const char *ia_address;

    camel_internet_address_get (address, idx, &ia_name, &ia_address);
    if (g_strcmp0 (ia_address, own_mail) == 0) {
      break;
    }
  }

  if (idx != len) {
    camel_address_remove (CAMEL_ADDRESS (address), idx);
  }
}

static void
stamp_composer_set_quote_content (StampComposer          *self,
                                  StampComposerType       type,
                                  const char             *uid,
                                  StampWebView           *webview,
                                  const CamelMessageInfo *info,
                                  CamelMimeMessage       *message,
                                  char                   *content_to_quote)
{
  const char *subject = camel_message_info_get_subject (info);

  self->type = type;
  self->orig_message = message;

  if (subject) {
    g_autofree char *new_subject = NULL;

    if (type == STAMP_COMPOSER_REPLY || type == STAMP_COMPOSER_REPLY_ALL) {
      g_autofree char *upper_subject = g_ascii_strup (subject, -1);

      if (g_str_has_prefix (upper_subject, "RE: ")) {
        new_subject = g_strdup (subject);
      } else {
        new_subject = g_strdup_printf ("Re: %s", subject);
      }
    } else {
      new_subject = g_strdup (subject);
    }

    gtk_editable_set_text (GTK_EDITABLE (self->subject), new_subject);
  }

  if (content_to_quote) {
    g_autoptr (GString) message_content = g_string_new ("");

    if (type == STAMP_COMPOSER_DRAFT) {
      CamelInternetAddress *to = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
      CamelInternetAddress *cc = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
      CamelInternetAddress *bcc = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_BCC);

      self->draft_uid = g_strdup (uid);

      if (to)
        stamp_add_addresses_to_completion (self, self->to, to);

      if (cc)
        if (stamp_add_addresses_to_completion (self, self->cc, cc))
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->toggle), TRUE);

      if (bcc)
        if (stamp_add_addresses_to_completion (self, self->bcc, bcc))
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->toggle), TRUE);

      g_string_append (message_content, content_to_quote);
    } else {
      CamelInternetAddress *from = camel_mime_message_get_from (message);
      CamelInternetAddress *to = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
      CamelInternetAddress *cc = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
      g_autofree char *formatted_from = camel_address_format (CAMEL_ADDRESS (from));
      g_autoptr (GDateTime) when = NULL;
      g_autofree char *date_received = NULL;
      char *date_format = _("%a, %b %-e, %Y at %-l:%M %p");
      char *who;

      message_content = g_string_append (message_content, "<br/><br/>");

      when = g_date_time_new_from_unix_utc (camel_message_info_get_date_received (info));
      date_received = g_date_time_format (when, date_format);

      if (type == STAMP_COMPOSER_REPLY || type == STAMP_COMPOSER_REPLY_ALL) {
        CamelInternetAddress *reply_to = camel_mime_message_get_reply_to (message);

        if (reply_to)
          stamp_add_addresses_to_completion (self, self->to, reply_to);
        else
          stamp_add_addresses_to_completion (self, self->to, from);

        if (type == STAMP_COMPOSER_REPLY_ALL) {
          remove_own_address (self, to);
          stamp_add_addresses_to_completion (self, self->to, to);
        }

        if (cc && type == STAMP_COMPOSER_REPLY_ALL) {
          if (stamp_add_addresses_to_completion (self, self->cc, cc))
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->toggle), TRUE);
        }

        who = formatted_from;

        g_string_append_printf (message_content, _("On %s, %s wrote:"), date_received, who);
        message_content = g_string_append (message_content, "<br/>");
        g_string_append_printf (message_content, "<blockquote type=\"cite\">%s</blockquote>", content_to_quote);
      } else if (type == STAMP_COMPOSER_FORWARD) {
        g_string_append_printf (message_content, _("---------- Forwarded message ----------"));
        g_string_append_printf (message_content, "<br/><br/>");
        g_string_append_printf (message_content, _("From: %s<br/>"), formatted_from);
        g_string_append_printf (message_content, _("Subject: %s<br/>"), camel_message_info_get_subject (info));
        g_string_append_printf (message_content, _("Date: %s<br/>"), date_received);

        g_string_append_printf (message_content, "<br/><br/>");
        g_string_append_printf (message_content, "%s", content_to_quote);
      }
    }

    stamp_webview_copy_resources (webview, self->webview);
    stamp_web_view_set_body_content (self->webview, message_content->str);
  }

  gtk_widget_grab_focus (GTK_WIDGET (self->webview));
}

GtkWidget *
stamp_composer_new_with_quote (StampComposerType       type,
                               const char             *uid,
                               StampAccount           *account,
                               StampWebView           *webview,
                               const CamelMessageInfo *info,
                               CamelMimeMessage       *mime_message,
                               char                   *content_to_quote)
{
  GtkWidget *composer = g_object_new (STAMP_TYPE_COMPOSER, "account", account, NULL);

  stamp_composer_set_quote_content (STAMP_COMPOSER (composer),
                                    type,
                                    uid,
                                    webview,
                                    info,
                                    mime_message,
                                    content_to_quote);

  return composer;
}

void
stamp_composer_set_to (StampComposer *self,
                       char          *to)
{
  CamelInternetAddress *to_address = camel_internet_address_new ();

  camel_internet_address_add (to_address, "", to);

  if (to_address)
    stamp_add_addresses_to_completion (self, self->to, to_address);
}

void
stamp_composer_set_subject (StampComposer *self,
                            char          *subject)
{
  gtk_editable_set_text (GTK_EDITABLE (self->subject), subject);
}

void
stamp_composer_set_body (StampComposer *self,
                         char          *body)
{
  stamp_web_view_set_body_content (self->webview, body);

  gtk_widget_grab_focus (GTK_WIDGET (self->webview));
}
