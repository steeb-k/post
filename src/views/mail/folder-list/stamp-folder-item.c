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

#include "stamp-folder-item.h"

#include "stamp-item.h"
#include "stamp-settings.h"

#include <glib/gi18n.h>
#include <gst/gst.h>

struct _StampFolderItem {
  StampItem parent_instance;

  CamelFolderInfo *folder_info;
  CamelFolder *folder;
  GCancellable *cancellable;

  gint unread;
  char *notification;
};

G_DEFINE_FINAL_TYPE (StampFolderItem, stamp_folder_item, STAMP_TYPE_ITEM);

enum {
  PROP_0,
  PROP_FOLDER_INFO,
  PROP_UNREAD,
  LAST_PROP
};

static GParamSpec *properties[LAST_PROP];

enum {
  FOLDER_ITEM_ADDED,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

static const char *
get_icon (CamelFolderInfo *info)
{
  switch (info->flags & CAMEL_FOLDER_TYPE_MASK) {
    case CAMEL_FOLDER_TYPE_INBOX:
      return "inbox-symbolic";
    case CAMEL_FOLDER_TYPE_OUTBOX:
      return "outbox-symbolic";
    case CAMEL_FOLDER_TYPE_TRASH:
      return "user-trash-symbolic";
    case CAMEL_FOLDER_TYPE_JUNK:
      return "junk-symbolic";
    case CAMEL_FOLDER_TYPE_SENT:
      return "sent-symbolic";
    case CAMEL_FOLDER_TYPE_ARCHIVE:
      return "archive-symbolic";
    case CAMEL_FOLDER_TYPE_DRAFTS:
      return "drafts-symbolic";
    default:
      return "folder-symbolic";
  }

  return "folder-symbolic";
}

static StampFolderItem *
stamp_folder_item_find_item (GListStore *store,
                             const char *full_name)
{
  int num = g_list_model_get_n_items (G_LIST_MODEL (store));

  for (int idx = 0; idx < num; idx++) {
    g_autoptr (StampFolderItem) item = g_list_model_get_item (G_LIST_MODEL (store), idx);
    GListStore *child_store;

    if (g_strcmp0 (stamp_folder_item_get_full_name (item), full_name) == 0)
      return g_steal_pointer (&item);

    child_store = stamp_item_get_list_store (STAMP_ITEM (item));
    if (child_store) {
      g_clear_object (&item);

      item = stamp_folder_item_find_item (child_store, full_name);
      if (item)
        return g_steal_pointer (&item);
    }
  }

  return NULL;
}

void
stamp_folder_item_set_folder_info (StampFolderItem *self,
                                   CamelFolderInfo *info)
{
  CamelFolderSummary *summary;

  if (self->folder_info == info)
    return;

  if (self->folder_info)
    g_boxed_free (camel_folder_info_get_type (), self->folder_info);

  self->folder_info = g_boxed_copy (camel_folder_info_get_type (), info);

  stamp_item_set_name (STAMP_ITEM (self), self->folder_info->display_name);

  if (info->child) {
    CamelFolderInfo *current_folder_info = self->folder_info->child;
    GListStore *child_store = stamp_item_get_list_store (STAMP_ITEM (self));
    StampFolderItem *child_item;

    if (!child_store)
      return;

    child_item = stamp_folder_item_find_item (child_store, current_folder_info->full_name);
    while (current_folder_info) {
      stamp_folder_item_set_folder_info (child_item, current_folder_info);
      current_folder_info = current_folder_info->next;
    }
  }

  if (self->folder) {
    summary = camel_folder_get_folder_summary (self->folder);
    self->unread = camel_folder_summary_get_unread_count (summary);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNREAD]);
  }
}

static void
stamp_folder_item_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  StampFolderItem *self = STAMP_FOLDER_ITEM (object);

  switch (property_id) {
    case PROP_FOLDER_INFO:
      stamp_folder_item_set_folder_info (self, g_value_get_boxed (value));
      break;
    case PROP_UNREAD:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_folder_item_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  StampFolderItem *self = STAMP_FOLDER_ITEM (object);

  switch (property_id) {
    case PROP_UNREAD:
      g_value_set_uint (value, self->unread);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
play_incoming_sound (void)
{
  g_autoptr (GstElement) player = NULL;

  player = gst_element_factory_make ("playbin", NULL);
  g_object_set (player, "uri", "resource:///org/tabos/stamp/sounds/incoming.wav", NULL);
  gst_element_set_state (player, GST_STATE_PLAYING);
}

static void
on_folder_item_folder_changed (CamelFolder           *folder,
                               CamelFolderChangeInfo *changes,
                               gpointer               user_data)
{
  StampFolderItem *self = STAMP_FOLDER_ITEM (user_data);
  g_autolist (CamelMessageInfo) unseen_message_infos = NULL;
  GList *sender_names = NULL;
  gint unseen_message_infos_length;
  CamelFolderSummary *summary;
  CamelFolder *trash_folder = stamp_account_get_mail_trash_folder (stamp_item_get_account (STAMP_ITEM (self)));
  CamelFolder *draft_folder = stamp_account_get_mail_drafts_folder (stamp_item_get_account (STAMP_ITEM (self)));

  summary = camel_folder_get_folder_summary (folder);
  self->unread = camel_folder_summary_get_unread_count (summary);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNREAD]);

  if (changes->uid_added) {
    GPtrArray *added = changes->uid_added;

    if (self->notification) {
      g_application_withdraw_notification (g_application_get_default (), self->notification);
      g_clear_pointer (&self->notification, g_free);
    }

    for (int idx = 0; idx < added->len; idx++) {
      CamelMessageInfo *message_info;
      char *uid = (char *)added->pdata[idx];
      guint32 flags;

      message_info = camel_folder_summary_get (summary, uid);
      flags = camel_message_info_get_flags (message_info);

      if (!(flags & CAMEL_MESSAGE_SEEN)) {
        CamelInternetAddress *address = camel_internet_address_new ();
        const char *sender_address;
        const char *sender_name;

        camel_address_unformat (CAMEL_ADDRESS (address), camel_message_info_get_from (message_info));
        camel_internet_address_get (address, 0, &sender_name, &sender_address);

        if (!sender_name)
          sender_name = sender_address;

        sender_names = g_list_append (sender_names, g_strdup (sender_name));
        unseen_message_infos = g_list_append (unseen_message_infos, message_info);
      }
    }

    unseen_message_infos_length = g_list_length (unseen_message_infos);
    if (folder != trash_folder && folder != draft_folder && unseen_message_infos_length && unseen_message_infos && unseen_message_infos->data) {
      CamelMessageInfo *unseen_message_info;
      GNotification *notification;
      g_autofree char *title = NULL;

      unseen_message_info = CAMEL_MESSAGE_INFO (unseen_message_infos->data);

      if (unseen_message_infos_length == 1)
        title = g_strdup_printf ("%s to %s", (char *)sender_names->data, stamp_account_get_name (stamp_item_get_account (STAMP_ITEM (self))));
      else
        title = g_strdup_printf ("%d new message", unseen_message_infos_length);

      notification = g_notification_new (title);
      g_notification_set_body (notification, camel_message_info_get_subject (unseen_message_info));
      g_notification_add_button_with_target (notification, _("Show"), "app.show-message", "s", camel_message_info_get_uid (unseen_message_info));
      g_notification_set_default_action_and_target (notification, "app.show-message", "s", camel_message_info_get_uid (unseen_message_info));

      self->notification = g_strdup (camel_message_info_get_uid (unseen_message_info));
      g_application_send_notification (g_application_get_default (), self->notification, notification);

      if (g_settings_get_boolean (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_PLAY_INCOMING_SOUND))
        play_incoming_sound ();
    }
  }

  g_clear_list (&sender_names, g_free);
}

static void
on_get_folder (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
  StampFolderItem *self = STAMP_FOLDER_ITEM (user_data);
  CamelStore *store = CAMEL_STORE (source);
  CamelFolderSummary *summary;
  g_autoptr (GError) error = NULL;

  stamp_item_set_loading (STAMP_ITEM (self), FALSE);
  self->folder = camel_store_get_folder_finish (store, res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Could not load folder: %s", error->message);
    return;
  }

  /* Add notification handler for non junk folders */
  if (!(camel_folder_get_flags (self->folder) & CAMEL_FOLDER_IS_JUNK))
    g_signal_connect_object (self->folder, "changed", G_CALLBACK (on_folder_item_folder_changed), self, 0);

  summary = camel_folder_get_folder_summary (self->folder);
  self->unread = camel_folder_summary_get_unread_count (summary);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNREAD]);
}

static void
stamp_folder_item_constructed (GObject *object)
{
  StampFolderItem *self = STAMP_FOLDER_ITEM (object);
  StampAccount *account;
  g_autoptr (GError) error = NULL;
  StampMailService *mail_service;

  G_OBJECT_CLASS (stamp_folder_item_parent_class)->constructed (object);

  stamp_item_set_name (STAMP_ITEM (self), self->folder_info->display_name);
  stamp_item_set_icon_name (STAMP_ITEM (self), get_icon (self->folder_info));

  account = stamp_item_get_account (STAMP_ITEM (self));

  if (self->folder_info->child) {
    CamelFolderInfo *current_folder_info = self->folder_info->child;
    GListStore *list_store;

    g_debug ("%s: Adding children", G_STRFUNC);
    stamp_item_set_list_store_type (STAMP_ITEM (self), STAMP_TYPE_FOLDER_ITEM);
    list_store = stamp_item_get_list_store (STAMP_ITEM (self));

    while (current_folder_info) {
      g_autoptr (StampFolderItem) folder_item = stamp_folder_item_new (account, current_folder_info);
      g_list_store_append (list_store, STAMP_ITEM (folder_item));

      current_folder_info = current_folder_info->next;
    }
  }

  if (self->folder_info->flags & CAMEL_FOLDER_NOSELECT)
    return;

  mail_service = stamp_account_get_mail_service (account);

  stamp_item_set_loading (STAMP_ITEM (self), TRUE);
  g_debug ("%s: Loading folder %s", G_STRFUNC, self->folder_info->full_name);
  camel_store_get_folder (CAMEL_STORE (stamp_mail_service_get_service (mail_service)),
                          self->folder_info->full_name,
                          CAMEL_STORE_FOLDER_NONE,
                          G_PRIORITY_DEFAULT,
                          self->cancellable,
                          on_get_folder,
                          self);
}

static void
stamp_folder_item_dispose (GObject *object)
{
  StampFolderItem *self = STAMP_FOLDER_ITEM (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_signal_handlers_disconnect_by_func (self->folder,
                                        G_CALLBACK (on_folder_item_folder_changed),
                                        self);
  g_clear_object (&self->folder);

  g_boxed_free (camel_folder_info_get_type (), self->folder_info);

  g_clear_pointer (&self->notification, g_free);

  G_OBJECT_CLASS (stamp_folder_item_parent_class)->dispose (object);
}

void
stamp_folder_item_class_init (StampFolderItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = stamp_folder_item_constructed;
  object_class->set_property = stamp_folder_item_set_property;
  object_class->get_property = stamp_folder_item_get_property;
  object_class->dispose = stamp_folder_item_dispose;

  properties[PROP_FOLDER_INFO] =
    g_param_spec_boxed ("folder-info",
                        NULL, NULL,
                        camel_folder_info_get_type (),
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  properties[PROP_UNREAD] =
    g_param_spec_uint ("unread",
                       NULL, NULL,
                       0,
                       G_MAXUINT,
                       0,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, properties);

  signals[FOLDER_ITEM_ADDED] = g_signal_new ("folder-item-added", G_OBJECT_CLASS_TYPE (klass),
                                             G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                             0, NULL, NULL, NULL,
                                             G_TYPE_NONE,
                                             0);
}

void
stamp_folder_item_init (StampFolderItem *self)
{
}

StampFolderItem *
stamp_folder_item_new (StampAccount    *account,
                       CamelFolderInfo *folder_info)
{
  return g_object_new (STAMP_TYPE_FOLDER_ITEM,
                       "account", account,
                       "folder-info", folder_info,
                       NULL);
}

const char *
stamp_folder_item_get_full_name (StampFolderItem *self)
{
  return self->folder_info->full_name;
}

gint
stamp_folder_item_get_flags (StampFolderItem *self)
{
  return self->folder_info->flags;
}

void
stamp_folder_item_disconnect (StampFolderItem *self)
{
  GListStore *list_store = stamp_item_get_list_store (STAMP_ITEM (self));
  guint list_len;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  if (self->folder) {
    g_signal_handlers_disconnect_by_func (self->folder,
                                          G_CALLBACK (on_folder_item_folder_changed),
                                          self);
  }

  if (!list_store)
    return;

  list_len = g_list_model_get_n_items (G_LIST_MODEL (list_store));
  for (guint idx = 0; idx < list_len; idx++) {
    g_autoptr (StampFolderItem) item = g_list_model_get_item (G_LIST_MODEL (list_store), idx);

    stamp_folder_item_disconnect (item);
  }
}

CamelFolder *
stamp_folder_item_get_folder (StampFolderItem *self)
{
  return self->folder;
}
