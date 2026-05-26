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

#include "stamp-message-list-item.h"

#include "stamp-attachment-button.h"
#include "stamp-message-list.h"
#include "stamp-message-header.h"
#include "stamp-session.h"
#include "stamp-settings.h"
#include "stamp-webview.h"
#include "stamp-mime-parser.h"

#include <camel/camel.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <libecal/libecal.h>
#include <nss.h>

#define LIBICAL_GLIB_UNSTABLE_API 1
#include <libical-glib/libical-glib.h>

#include <webkit/webkit.h>

struct _StampMessageListItem {
  GtkListBoxRow parent_instance;

  GtkWidget *box;
  GtkWidget *header;
  GtkWidget *secondary_revealer;
  GtkWidget *vcard_banner;
  GtkWidget *blocked_images_revealer;
  GtkWidget *error_banner;
  GtkWidget *blocked_images_banner;
  GtkWidget *signature_banner;
  GtkWidget *encryption_banner;
  GtkWidget *stack;
  GtkWidget *attachment_flow_box;
  GtkWidget *disposition_banner;

  StampWebView *web_view;
  StampAccount *account;

  StampMimeParser *parser;

  const CamelMessageInfo *message_info;
  char *calendar_content;
  char *message_content;
  char *signature_details;
  CamelCipherValiditySign signature_status;
  char *disposition_notification_to;
  gboolean message_is_html;
  gboolean expanded;
  gboolean message_loaded;
  GCancellable *cancellable;
  CamelMimeMessage *message;
  ICalComponent *calendar;

  guint progress_handle;
  gboolean loading_done;
  GSimpleActionGroup *actions;
  StampComposerType type;
};

G_DEFINE_FINAL_TYPE (StampMessageListItem, stamp_message_list_item, GTK_TYPE_LIST_BOX_ROW);

enum {
  PROP_0,
  PROP_ACCOUNT,
  PROP_MESSAGE_INFO,
  PROP_EXPANDED,
  LAST_PROP
};

static GParamSpec *properties[LAST_PROP];

static void
open_message (StampMessageListItem *self,
              CamelMimeMessage     *message)
{
  StampMimeBody *body;
  const GList *signatures;
  const GList *encryptions;
  StampMimeCalendar *calendar;
  GList *list_unsubscribe;
  const char *address = camel_medium_get_header (CAMEL_MEDIUM (message), "Disposition-Notification-To");
  const char *auth_as = camel_medium_get_header (CAMEL_MEDIUM (message), "X-MS-Exchange-Organization-AuthAs");
  const char *sender = camel_medium_get_header (CAMEL_MEDIUM (message), "Sender");
  GList *attachments;
  StampMessageList *message_list = STAMP_MESSAGE_LIST (gtk_widget_get_ancestor (GTK_WIDGET (self), STAMP_TYPE_MESSAGE_LIST));

  g_clear_object (&self->parser);

  if (!address)
    address = camel_medium_get_header (CAMEL_MEDIUM (message), "Return-Receipt-To");

  if (address) {
    while (camel_mime_is_lwsp (*address))
      address++;
  }

  if (address && *address) {
    g_autofree char *tmp = NULL;

    self->disposition_notification_to = g_strdup (address);
    tmp = g_strdup_printf (_("Sender %s wants a read receipt"), self->disposition_notification_to);
    adw_banner_set_title (ADW_BANNER (self->disposition_banner), tmp);

    adw_banner_set_revealed (ADW_BANNER (self->disposition_banner), TRUE);
  }

  if (auth_as) {
    if (g_strcmp0 (auth_as, "Internal") == 0) {
      stamp_web_view_load_images (self->web_view);
      stamp_message_header_set_internal (STAMP_MESSAGE_HEADER (self->header), TRUE);
    } else {
      stamp_message_header_set_extern (STAMP_MESSAGE_HEADER (self->header), TRUE);
    }
  }

  stamp_message_header_set_sender (STAMP_MESSAGE_HEADER (self->header), sender);

  self->parser = stamp_mime_parser_new (CAMEL_SESSION (stamp_session_get_default ()));
  stamp_mime_parser_parse (self->parser, message, self->cancellable, NULL);

  signatures = stamp_mime_parser_get_signatures (self->parser);
  encryptions = stamp_mime_parser_get_encryptions (self->parser);
  if (signatures) {
    StampMimeSignature *signature = signatures->data;

    if (signature->status != STAMP_MIME_SIGNATURE_NONE) {
      g_autofree char *tmp = NULL;

      if (signature->description) {
        g_autoptr (GString) str = g_string_new ("");

        adw_banner_set_title (ADW_BANNER (self->signature_banner), signature->description);
        adw_banner_set_revealed (ADW_BANNER (self->signature_banner), TRUE);

        for (GList *iter = signature->signers; iter && iter->data; iter = g_list_next (iter)) {
          StampMimeSignerInfo *info = iter->data;

          g_string_append_printf (str, "%s <%s>\n", info->name, info->email);
        }

        self->signature_details = g_strdup (str->str);
      }
    }
  }

  if (encryptions) {
    StampMimeEncryption *encryption = encryptions->data;

    if (encryption->status != STAMP_MIME_ENCRYPTION_VALID) {
      adw_banner_set_title (ADW_BANNER (self->encryption_banner), _("Valid Encryption"));
      adw_banner_set_revealed (ADW_BANNER (self->encryption_banner), TRUE);
    }
  }

  calendar = stamp_mime_parser_get_invitations (self->parser);
  if (calendar) {
    ICalTime *time;

    self->calendar = g_object_ref (calendar->ical);
    time = i_cal_component_get_dtstart (self->calendar);
    if (time) {
      g_autofree char *tmp = g_strdup_printf ("%s: %d.%d.%d %.2d:%.2d",
                                              i_cal_component_get_summary (self->calendar),
                                              i_cal_time_get_day (time),
                                              i_cal_time_get_month (time),
                                              i_cal_time_get_year (time),
                                              i_cal_time_get_hour (time),
                                              i_cal_time_get_minute (time));
      adw_banner_set_title (ADW_BANNER (self->vcard_banner), tmp);
      adw_banner_set_revealed (ADW_BANNER (self->vcard_banner), TRUE);

      if (g_strcmp0 (calendar->method, "REPLY") == 0)
        adw_banner_set_button_label (ADW_BANNER (self->vcard_banner), NULL);
    }
  }

  list_unsubscribe = stamp_mime_parser_get_list_unsubscribe (self->parser);
  if (list_unsubscribe) {
    StampMimeListUnsubscribe *unsubscribe = list_unsubscribe->data;

    stamp_message_list_set_unsubscribe (message_list, camel_message_info_get_from (self->message_info), unsubscribe->uri, self->message);
  }

  body = stamp_mime_parser_get_body (self->parser);
  if (body && body->text) {
    self->message_content = g_strdup (body->text);
    self->message_is_html = body->is_html;

    if (self->message_is_html && body->is_html) {
      /*char *html_with_images = stamp_mime_parser_embed_inline_images (parser, self->message_content);
      if (html_with_images) {
        g_clear_pointer (&self->message_content, g_free);
        self->message_content = html_with_images;
      }*/
    }
  }

  attachments = stamp_mime_parser_get_attachments (self->parser);
  for (GList *iter = attachments; iter && iter->data; iter = g_list_next (iter)) {
    StampMimeAttachment *att = iter->data;
    GtkWidget *button = stamp_attachment_button_new_from_data (att->filename, att->mime_type, att->size, att->data);

    adw_wrap_box_append (ADW_WRAP_BOX (self->attachment_flow_box), button);
  }
  gtk_widget_set_visible (self->attachment_flow_box, attachments != NULL);

  if (!self->message_content) {
    self->loading_done = TRUE;
    g_clear_handle_id (&self->progress_handle, g_source_remove);

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "webview");
  } else {
    if (self->message_is_html) {
      stamp_webview_load_html (self->web_view, self->message_content);
    } else {
      stamp_webview_load_plain_text (self->web_view, self->message_content);
    }
  }
}

static void
on_get_message (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
  StampMessageListItem *self;
  g_autoptr (GError) error = NULL;
  CamelFolder *folder;
  CamelMimeMessage *message;

  folder = CAMEL_FOLDER (source);
  message = camel_folder_get_message_finish (folder, res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      self = STAMP_MESSAGE_LIST_ITEM (user_data);

      g_warning ("Could not get message: %s", error->message);
      self->loading_done = TRUE;
      g_clear_handle_id (&self->progress_handle, g_source_remove);
    }
    return;
  }

  self = STAMP_MESSAGE_LIST_ITEM (user_data);
  if (!self->message_loaded)
    return;

  if ((camel_folder_get_flags (folder) & CAMEL_FOLDER_IS_JUNK) == 0 &&
      (camel_folder_get_flags (folder) & CAMEL_FOLDER_IS_TRASH) == 0 &&
      g_settings_get_boolean (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_ALWAYS_SHOW_IMAGES))
    stamp_web_view_load_images (self->web_view);

  g_clear_object (&self->message);

  if (message)
    self->message = g_object_ref (message);

  open_message (self, message);
}

static gboolean
on_progress (gpointer user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  if (!self->loading_done)
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "progress");

  self->progress_handle = 0;
  return G_SOURCE_REMOVE;
}

static void
start_get_message (StampMessageListItem *self,
                   GAsyncReadyCallback   callback,
                   gpointer              user_data)
{
  CamelFolderSummary *summary;
  CamelFolder *folder;

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  self->cancellable = g_cancellable_new ();

  self->loading_done = FALSE;
  self->progress_handle = g_timeout_add (500, on_progress, self);

  g_object_get (G_OBJECT (self->message_info), "summary", &summary, NULL);

  folder = camel_folder_summary_get_folder (summary);

  camel_folder_get_message (folder, camel_message_info_get_uid (self->message_info), G_PRIORITY_DEFAULT, self->cancellable, on_get_message, self);
}

void
stamp_message_list_item_set_expanded (StampMessageListItem *self,
                                      gboolean              expanded)
{
  gtk_widget_set_visible (self->secondary_revealer, TRUE);
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->secondary_revealer), expanded);

  self->expanded = expanded;
  stamp_message_header_set_collapsed (STAMP_MESSAGE_HEADER (self->header), expanded);

  if (expanded) {
    if (!self->message_loaded) {
      start_get_message (self, NULL, NULL);
      self->message_loaded = TRUE;
    }

    gtk_widget_set_visible (GTK_WIDGET (self->stack), TRUE);
    gtk_widget_add_css_class (GTK_WIDGET (self), "expanded");
  } else {
    gtk_widget_set_visible (GTK_WIDGET (self->stack), FALSE);
    gtk_widget_remove_css_class (GTK_WIDGET (self), "expanded");
  }
}

static void
stamp_message_list_item_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      g_value_set_object (value, self->account);
      break;
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
stamp_message_list_item_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      self->account = g_value_get_object (value);
      stamp_message_header_set_account (STAMP_MESSAGE_HEADER (self->header), self->account);
      break;
    case PROP_MESSAGE_INFO:
      self->message_info = g_value_get_object (value);
      break;
    case PROP_EXPANDED:
      stamp_message_list_item_set_expanded (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
on_header_clicked (GObject  *object,
                   gint      n_press,
                   gdouble   x,
                   gdouble   y,
                   gpointer  user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  stamp_message_list_item_set_expanded (self, !self->expanded);
}

static void
on_mouse_target_changed (WebKitWebView       *web_view,
                         WebKitHitTestResult *hit_test_result,
                         guint                modifiers,
                         gpointer             user_data)
{
  StampMessageList *message_list = STAMP_MESSAGE_LIST (gtk_widget_get_ancestor (GTK_WIDGET (user_data), STAMP_TYPE_MESSAGE_LIST));

  if (webkit_hit_test_result_context_is_link (hit_test_result)) {
    stamp_message_list_hovering_over_link (message_list, webkit_hit_test_result_get_link_title (hit_test_result), webkit_hit_test_result_get_link_uri (hit_test_result));
  } else {
    stamp_message_list_hovering_over_link (message_list, NULL, NULL);
  }
}

static void
on_image_load_blocked (StampWebView *web_view,
                       gpointer      user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  adw_banner_set_revealed (ADW_BANNER (self->blocked_images_banner), TRUE);
}

static void
on_show_images (AdwBanner *banner,
                gpointer   user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  stamp_web_view_load_images (self->web_view);
  adw_banner_set_revealed (banner, FALSE);
}

static void
on_show_signatures (AdwBanner *banner,
                    gpointer   user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  AdwDialog *dialog = adw_alert_dialog_new (_("Signature Details"), self->signature_details);
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));

  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", _("Close"));
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "close");

  adw_dialog_present (dialog, GTK_WIDGET (window));
}

static void
stamp_message_list_item_send_rsvp (StampMessageListItem  *self,
                                   ICalParameterPartstat  stat)
{
  ICalProperty *prop;
  ICalComponent *event;
  GSList *components = NULL;
  GCancellable *cancellable = g_cancellable_new ();
  g_autoptr (EClient) client = NULL;
  g_autoptr (GError) local_error = NULL;
  ESourceRegistry *registry = e_source_registry_new_sync (cancellable, &local_error);
  StampMailService *service = stamp_account_get_mail_service (self->account);
  const gchar *collection_uid = e_source_get_parent (stamp_mail_service_get_source (service));
  GList *sources = e_source_registry_list_sources (registry, E_SOURCE_EXTENSION_CALENDAR);
  ESource *source;
  CamelInternetAddress *address = stamp_account_get_address (self->account);
  const char *name;
  const char *email;
  gboolean stat_set = FALSE;

  camel_internet_address_get (address, 0, &name, &email);

  for (GList *l = sources; l; l = l->next) {
    ESource *src = l->data;

    if (g_strcmp0 (e_source_get_parent (src), collection_uid) == 0 /* && g_strcmp0 (e_source_get_display_name (src), "Kalender") == 0*/) {
      source = g_object_ref (src);
      break;
    }
  }

  event = i_cal_component_get_first_component (self->calendar, I_CAL_VEVENT_COMPONENT);

  for (prop = i_cal_component_get_first_property (event, I_CAL_ATTENDEE_PROPERTY); prop; prop = i_cal_component_get_next_property (event, I_CAL_ATTENDEE_PROPERTY)) {
    const char *attendee = i_cal_property_get_attendee (prop);
    const char *attendee_email;

    if (g_str_has_prefix (attendee, "mailto:"))
      attendee_email = attendee + 7;
    else
      attendee_email = attendee;

    if (g_strcmp0 (attendee_email, email) == 0) {
      ICalParameter *p = i_cal_parameter_new_partstat (stat);

      i_cal_property_set_parameter (prop, p);
      stat_set = TRUE;
      break;
    }
  }

  if (!stat_set) {
    g_autofree char *mailto = g_strdup_printf ("mailto:%s", email);
    ICalProperty *attendee = i_cal_property_new_attendee (mailto);

    i_cal_property_add_parameter (attendee, i_cal_parameter_new_partstat (stat));
    i_cal_component_add_property (event, attendee);
  }

  client = e_cal_client_connect_sync (source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 30, cancellable, &local_error);
  if (!client) {
    g_warning ("%s: Could not create ECalClient: %s", G_STRFUNC, local_error->message);
    return;
  }

  i_cal_component_set_method (self->calendar, I_CAL_METHOD_REPLY);
  components = g_slist_append (components, event);
  e_cal_client_receive_objects_sync (E_CAL_CLIENT (client), self->calendar, E_CAL_OPERATION_FLAG_NONE, cancellable, &local_error);
  if (local_error) {
    g_warning ("%s: Could not receive event: %s", G_STRFUNC, local_error->message);
    return;
  }
}

static void
on_rsvp_response (GtkWidget *dialog,
                  char      *response,
                  gpointer   user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  ICalParameterPartstat stat = I_CAL_PARTSTAT_NONE;

  if (g_strcmp0 (response, "accept") == 0) {
    stat = I_CAL_PARTSTAT_ACCEPTED;
  } else if (g_strcmp0 (response, "tentative") == 0) {
    stat = I_CAL_PARTSTAT_TENTATIVE;
  } else if (g_strcmp0 (response, "decline") == 0) {
    stat = I_CAL_PARTSTAT_DECLINED;
  }

  if (stat != I_CAL_PARTSTAT_NONE)
    stamp_message_list_item_send_rsvp (self, stat);
}

static void
on_rsvp (AdwBanner *banner,
         gpointer   user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  AdwDialog *dialog;
  const char *sender = camel_message_info_get_from (self->message_info);
  g_autofree char *body = g_strdup_printf (_("%s wants to know whether you can join this meeting"), sender);

  dialog = adw_alert_dialog_new (_("RVSP"), body);

  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "cancel", _("Cancel"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "decline", _("Decline"));
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "decline", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "tentative", _("Tentative"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "accept", _("Accept"));
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "accept", ADW_RESPONSE_SUGGESTED);

  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
  g_signal_connect (dialog, "response", G_CALLBACK (on_rsvp_response), self);

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
on_loaded (GtkWidget *web_view,
           gboolean   loaded,
           gpointer   user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  if (loaded) {
    self->loading_done = TRUE;
    g_clear_handle_id (&self->progress_handle, g_source_remove);
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "webview");
  }
}

static void
stamp_message_list_item_dispose (GObject *object)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (object);

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  g_clear_handle_id (&self->progress_handle, g_source_remove);

  g_clear_pointer (&self->message_content, g_free);
  g_clear_pointer (&self->signature_details, g_free);
  g_clear_pointer (&self->disposition_notification_to, g_free);

  g_clear_object (&self->message);
  g_clear_object (&self->calendar);

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_MESSAGE_LIST_ITEM);

  G_OBJECT_CLASS (stamp_message_list_item_parent_class)->dispose (object);
}

static void
on_send_disposition (AdwBanner *banner,
                     gpointer   user_data)
{
}

void
stamp_message_list_item_class_init (StampMessageListItemClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (STAMP_TYPE_WEBVIEW);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/message-list/stamp-message-list-item.ui");

  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, header);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, stack);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, error_banner);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, blocked_images_banner);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, disposition_banner);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, signature_banner);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, encryption_banner);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, vcard_banner);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, web_view);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, secondary_revealer);
  gtk_widget_class_bind_template_child (widget_class, StampMessageListItem, attachment_flow_box);

  gtk_widget_class_bind_template_callback (widget_class, on_header_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_show_images);
  gtk_widget_class_bind_template_callback (widget_class, on_show_signatures);
  gtk_widget_class_bind_template_callback (widget_class, on_rsvp);
  gtk_widget_class_bind_template_callback (widget_class, on_image_load_blocked);
  gtk_widget_class_bind_template_callback (widget_class, on_loaded);
  gtk_widget_class_bind_template_callback (widget_class, on_mouse_target_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_send_disposition);

  gobject_class->dispose = stamp_message_list_item_dispose;

  gobject_class->get_property = stamp_message_list_item_get_property;
  gobject_class->set_property = stamp_message_list_item_set_property;

  properties[PROP_ACCOUNT] =
    g_param_spec_object ("account",
                         NULL,
                         NULL,
                         STAMP_TYPE_ACCOUNT,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_MESSAGE_INFO] =
    g_param_spec_object ("message-info",
                         NULL,
                         NULL,
                         CAMEL_TYPE_MESSAGE_INFO,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_EXPANDED] =
    g_param_spec_boolean ("expanded",
                          NULL,
                          NULL,
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, LAST_PROP, properties);
}

static void
on_size_request (GtkWidget  *web_view,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  int width;
  int height;
  int my_width;

  stamp_web_view_get_size (self->web_view, &width, &height);

  my_width = gtk_widget_get_width (GTK_WIDGET (self));

  if (width > my_width) {
    gdouble zoom_level = (double)my_width / (double)width;

    webkit_web_view_set_zoom_level (WEBKIT_WEB_VIEW (self->web_view), zoom_level);
    gtk_widget_set_size_request (GTK_WIDGET (self->web_view), -1, height * zoom_level);
  } else {
    webkit_web_view_set_zoom_level (WEBKIT_WEB_VIEW (self->web_view), 1);
    gtk_widget_set_size_request (GTK_WIDGET (self->web_view), -1, height);
  }
}

static void
update_actions (StampMessageListItem *self)
{
  GAction *action;
  guint32 flags = camel_message_info_get_flags (self->message_info);
  gboolean flagged = (flags & CAMEL_MESSAGE_FLAGGED) != 0;
  gboolean read = (flags & CAMEL_MESSAGE_SEEN) != 0;

  action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-unflag");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), flagged);
  action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-flag");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), !flagged);

  action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-unread");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), read);
  action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-read");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), !read);
}

static void
on_mark_read_activate (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  camel_message_info_set_flags ((CamelMessageInfo *)self->message_info, CAMEL_MESSAGE_SEEN, ~0);
  update_actions (self);
}

static void
on_mark_unread_activate (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  camel_message_info_set_flags ((CamelMessageInfo *)self->message_info, CAMEL_MESSAGE_SEEN, 0);
  update_actions (self);
}

static void
on_mark_flag_activate (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  camel_message_info_set_flags ((CamelMessageInfo *)self->message_info, CAMEL_MESSAGE_FLAGGED, ~0);
  update_actions (self);
}

static void
on_mark_unflag_activate (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  camel_message_info_set_flags ((CamelMessageInfo *)self->message_info, CAMEL_MESSAGE_FLAGGED, 0);
  update_actions (self);
}

static void
on_print (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  stamp_message_list_item_print (self);
}

static void
on_view_source_activate (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  stamp_message_list_item_view_source (self);
}

static void
on_message_body (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  GtkWidget *composer;
  g_autoptr (GError) error = NULL;
  char *body;

  body = stamp_message_list_item_get_message_body_html_finish (self, res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Could not get message body html: %s", error->message);
    return;
  }

  if (!body) {
    g_warning ("Could not get message body html: empty response");
    return;
  }

  composer = stamp_composer_new_with_quote (self->type,
                                            stamp_message_list_item_get_uid (self),
                                            self->account,
                                            stamp_message_list_item_get_web_view (self),
                                            stamp_message_list_item_get_message_info (self),
                                            stamp_message_list_item_get_message (self),
                                            body);
  gtk_window_present (GTK_WINDOW (composer));
}

static void
stamp_message_list_item_compose (StampMessageListItem *self,
                                 StampComposerType     type)
{
  if (type == STAMP_COMPOSER_NEW) {
    GtkWidget *composer;

    composer = stamp_composer_new (self->account);
    gtk_window_present (GTK_WINDOW (composer));
    return;
  }

  self->type = type;
  stamp_message_list_item_get_message_body_html (self, NULL, on_message_body, self);
}

static void
on_reply (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  stamp_message_list_item_compose (self, STAMP_COMPOSER_REPLY);
}

static void
on_reply_all (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  stamp_message_list_item_compose (self, STAMP_COMPOSER_REPLY_ALL);
}

static void
on_forward (GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  stamp_message_list_item_compose (self, STAMP_COMPOSER_FORWARD);
}

static const GActionEntry actions[] = {
  { "reply", on_reply},
  { "reply-all", on_reply_all},
  { "forward", on_forward},
  { "mark-read", on_mark_read_activate},
  { "mark-unread", on_mark_unread_activate},
  { "mark-flag", on_mark_flag_activate},
  { "mark-unflag", on_mark_unflag_activate},
  { "print", on_print},
  { "view-source", on_view_source_activate},
};

static GInputStream *
on_cid_request (WebKitURISchemeRequest *request,
                gpointer                user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);
  g_autofree char *path = g_uri_unescape_string (webkit_uri_scheme_request_get_path (request), NULL);
  GList *attachments;

  attachments = stamp_mime_parser_get_inline_images (self->parser);
  for (GList *iter = attachments; iter && iter->data; iter = g_list_next (iter)) {
    StampMimeAttachment *att = iter->data;

    if (g_strcmp0 (att->content_id, path) == 0) {
      GInputStream *stream = g_memory_input_stream_new_from_bytes (att->data);
      return stream;
    }
  }

  /* Mail clients are stupid and often declare inline images as attachments… */
  attachments = stamp_mime_parser_get_attachments (self->parser);
  for (GList *iter = attachments; iter && iter->data; iter = g_list_next (iter)) {
    StampMimeAttachment *att = iter->data;

    if (g_strcmp0 (att->content_id, path) == 0) {
      GInputStream *stream = g_memory_input_stream_new_from_bytes (att->data);
      return stream;
    }
  }

  return NULL;
}

void
stamp_message_list_item_init (StampMessageListItem *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->message = NULL;
  self->message_loaded = FALSE;

  self->cancellable = g_cancellable_new ();

  g_signal_connect_object (self->web_view, "notify::size-request", G_CALLBACK (on_size_request), self, 0);

  self->actions = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->actions), actions, G_N_ELEMENTS (actions), self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "message-list-item", G_ACTION_GROUP (self->actions));

  stamp_webview_set_cid_handler (self->web_view, on_cid_request, self);
}

GtkWidget *
stamp_message_list_item_new (StampAccount          *account,
                             CamelFolderThreadNode *thread_node)
{
  const CamelMessageInfo *message = camel_folder_thread_node_get_item (thread_node);
  StampMessageListItem *ret = g_object_new (STAMP_TYPE_MESSAGE_LIST_ITEM,
                                            "account", account,
                                            "message-info", message,
                                            NULL);

  stamp_message_header_set_mail (STAMP_MESSAGE_HEADER (ret->header), thread_node);
  update_actions (ret);

  return GTK_WIDGET (ret);
}

const CamelMessageInfo *
stamp_message_list_item_get_message_info (StampMessageListItem *self)
{
  return self->message_info;
}

void
stamp_message_list_item_get_message_body_html (StampMessageListItem *self,
                                               GCancellable         *cancellable,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data)
{
  stamp_webview_get_body_html (self->web_view, cancellable, callback, user_data);
}

char *
stamp_message_list_item_get_message_body_html_finish (StampMessageListItem  *self,
                                                      GAsyncResult          *res,
                                                      GError               **error)
{
  return stamp_webview_get_body_html_finish (self->web_view, res, NULL, error);
}

CamelMimeMessage *
stamp_message_list_item_get_message (StampMessageListItem *self)
{
  return self->message;
}

guint64
stamp_message_list_item_get_timestamp (StampMessageListItem *self)
{
  return camel_message_info_get_date_received (self->message_info);
}

void
stamp_message_list_item_print (StampMessageListItem *self)
{
  GtkPrintSettings *settings = gtk_print_settings_new ();
  WebKitPrintOperation *operation;
  const char *subject = camel_message_info_get_subject (self->message_info);
  g_autofree char *filename = g_strdup (_("E-Mail Message"));
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));

  if (subject && strlen (subject) > 0) {
    GRegex *regex = g_regex_new ("[[:space:][:cntrl:]/]+", 0, 0, NULL);
    g_autofree char *new_string = NULL;

    new_string = g_regex_replace (regex, subject, (gssize) - 1, 0, " ", 0, NULL);
    if (strlen (new_string) < 64) {
      g_set_str (&filename, new_string);
    } else {
      filename = g_strdup_printf ("%60s", new_string);
    }
  }

  gtk_print_settings_set (settings, GTK_PRINT_SETTINGS_OUTPUT_BASENAME, filename);
  operation = webkit_print_operation_new (WEBKIT_WEB_VIEW (self->web_view));

  webkit_print_operation_set_print_settings (operation, settings);
  webkit_print_operation_run_dialog (operation, GTK_WINDOW (root));
}

const char *
stamp_message_list_item_get_uid (StampMessageListItem *self)
{
  return camel_message_info_get_uid (self->message_info);
}

#define VIEW_SOURCE_TMP_DIR "/tmp/stamp-view-source"

static void
cleanup_view_source_dir (void)
{
  GDir *dir = g_dir_open (VIEW_SOURCE_TMP_DIR, 0, NULL);
  const char *name;

  if (!dir)
    return;

  while ((name = g_dir_read_name (dir)) != NULL) {
    g_autofree char *child = g_build_filename (VIEW_SOURCE_TMP_DIR, name, NULL);
    g_remove (child);
  }

  g_dir_close (dir);
  g_remove (VIEW_SOURCE_TMP_DIR);
}

void
stamp_message_list_item_view_source (StampMessageListItem *self)
{
  static gboolean initialized = FALSE;
  g_autoptr (GError) error = NULL;
  g_autofree char *path = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (CamelStream) stream = NULL;
  g_autofree char *tmpl = NULL;
  GByteArray *array = NULL;
  int fd;

  if (!initialized) {
    initialized = TRUE;

    cleanup_view_source_dir ();

    if (g_mkdir (VIEW_SOURCE_TMP_DIR, 0700) != 0) {
      g_warning ("%s: Could not create temp directory %s", G_STRFUNC, VIEW_SOURCE_TMP_DIR);
      return;
    }

    atexit (cleanup_view_source_dir);
  }

  tmpl = g_build_filename (VIEW_SOURCE_TMP_DIR, "stamp-XXXXXX", NULL);
  fd = g_mkstemp (tmpl);
  if (fd == -1) {
    g_warning ("%s: Temp file failed: %s", G_STRFUNC, g_strerror (errno));
    return;
  }

  path = g_strdup_printf ("%s.txt", tmpl);
  g_rename (tmpl, path);

  array = g_byte_array_new ();
  stream = camel_stream_mem_new_with_byte_array (array);
  camel_data_wrapper_write_to_stream_sync (CAMEL_DATA_WRAPPER (self->message), stream, self->cancellable, &error);

  write (fd, (char *)array->data, array->len);
  close (fd);

  file = g_file_new_for_path (path);
  if (!g_app_info_launch_default_for_uri (g_file_get_uri (file), NULL, &error))
    g_warning ("%s: Launch failed: %s", G_STRFUNC, error->message);
}

static void
on_found_text (WebKitFindController *controller,
               guint                 match_count,
               gpointer              user_data)
{
  StampMessageListItem *self = STAMP_MESSAGE_LIST_ITEM (user_data);

  stamp_message_list_item_set_expanded (self, TRUE);
}

void
stamp_message_list_item_search (StampMessageListItem *self,
                                const char           *search_text)
{
  WebKitFindController *controller = webkit_web_view_get_find_controller (WEBKIT_WEB_VIEW (self->web_view));

  g_signal_connect (controller, "found-text", G_CALLBACK (on_found_text), self);
  webkit_find_controller_search (controller, search_text, WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE, G_MAXUINT);
}

StampWebView *
stamp_message_list_item_get_web_view (StampMessageListItem *self)
{
  return self->web_view;
}
