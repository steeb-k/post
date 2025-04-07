/* stamp-account-item-model.c
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

#include "stamp-account.h"
#include "stamp-account-item-model.h"
/* #include "stamp-account-mail.h" */
#include "stamp-folder-item-model.h"

#include <camel/camel.h>

struct _StampAccountItemModel {
  GObject parent_instance;

  char *name;
  char *icon_name;
  StampAccount *account;
  GListStore *list_store;
  GCancellable *cancellable;
};

static void stamp_account_item_model_item_model_init (StampItemModelInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (StampAccountItemModel, stamp_account_item_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (STAMP_TYPE_ITEM_MODEL, stamp_account_item_model_item_model_init))

enum {
  PROP_0,
  PROP_ACCOUNT,
  LAST_PROP
};

static GParamSpec *obj_properties[LAST_PROP];

static void
stamp_account_item_model_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  StampAccountItemModel *self = STAMP_ACCOUNT_ITEM_MODEL (object);

  switch (property_id) {
    case PROP_ACCOUNT:
      self->account = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_account_item_model_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
show_info (StampAccountItemModel *self,
           CamelFolderInfo       *folder_info)
{
  g_list_store_remove_all (self->list_store);

  while (folder_info) {
    StampFolderItemModel *folder_item = stamp_folder_item_model_new (self->account);

    stamp_folder_item_set_folder_info (folder_item, folder_info);

    g_list_store_append (self->list_store, STAMP_ITEM_MODEL (folder_item));
    folder_info = folder_info->next;
  }
}

static void on_loaded (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  StampAccountItemModel *self = STAMP_ACCOUNT_ITEM_MODEL (user_data);
  CamelOfflineStore *offline_store = CAMEL_OFFLINE_STORE (source);
  CamelFolderInfo *folder_info;
  g_autoptr (GError) error = NULL;

  folder_info = camel_store_get_folder_info_finish (CAMEL_STORE (offline_store), res, &error);
  if (error) {
    g_print ("Error getting folder: %s", error->message);
    return;
  }

  show_info (self, folder_info);
}

static void
stamp_account_item_model_load (StampAccountItemModel *self,
                               GCancellable          *cancellable,
                               GAsyncReadyCallback    callback,
                               gpointer               user_data)
{
  CamelService *service = stamp_account_get_service (self->account);
  CamelOfflineStore *offline_store = CAMEL_OFFLINE_STORE (service);

  camel_store_get_folder_info (CAMEL_STORE (offline_store), NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, G_PRIORITY_DEFAULT, cancellable, callback, user_data);
  /* g_autoptr (GTask) task = NULL; */

  /* g_return_if_fail (self); */

  /* g_print ("%s: ENTER\n", G_STRFUNC); */
  /* task = g_task_new (G_OBJECT (self), NULL, callback, user_data); */
  /* g_task_run_in_thread (task, load); */
  /* g_print ("%s: EXIT\n", G_STRFUNC); */
}

static void
on_folder_changed (CamelOfflineStore *store,
                   gpointer           user_data)
{

}

static void
on_network_changed (GNetworkMonitor *monitor,
                    gboolean         state,
                    gpointer         user_data)
{

}

static void
stamp_account_item_model_constructed (GObject *object)
{
  StampAccountItemModel *self = STAMP_ACCOUNT_ITEM_MODEL (object);
  CamelService *service = stamp_account_get_service (self->account);
  CamelOfflineStore *offline_store = CAMEL_OFFLINE_STORE (service);
  GNetworkMonitor *network_monitor = g_network_monitor_get_default ();

  self->icon_name = NULL;//g_strdup ("avatar-default");
  self->name = g_strdup (camel_service_get_display_name (service));

  g_print ("%s: %s\n", G_STRFUNC, camel_service_get_display_name (service));
  g_print ("%s: %s\n", G_STRFUNC, camel_service_get_uid (service));

  self->list_store = g_list_store_new (STAMP_TYPE_FOLDER_ITEM_MODEL);

  self->cancellable = g_cancellable_new ();

  // Register callbacks for folder changes...
  g_signal_connect (offline_store, "folder-created", G_CALLBACK (on_folder_changed), self);
  g_signal_connect (offline_store, "folder-deleted", G_CALLBACK (on_folder_changed), self);
  g_signal_connect (offline_store, "folder-info-stale", G_CALLBACK (on_folder_changed), self);
  g_signal_connect (offline_store, "folder-renamed", G_CALLBACK (on_folder_changed), self);

  // NetworkMonitor
  g_signal_connect (network_monitor, "network-changed", G_CALLBACK (on_network_changed), self);

  stamp_account_item_model_load (self, NULL, on_loaded, self);;
}

void
stamp_account_item_model_class_init (StampAccountItemModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = stamp_account_item_model_constructed;
  object_class->set_property = stamp_account_item_model_set_property;
  object_class->get_property = stamp_account_item_model_get_property;

  obj_properties[PROP_ACCOUNT] =
    g_param_spec_object ("account",
                      NULL, NULL,
                      STAMP_TYPE_ACCOUNT,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, obj_properties);
}

void
stamp_account_item_model_init (StampAccountItemModel *self)
{
}

StampAccountItemModel *
stamp_account_item_model_new (StampAccount *account)
{
  return g_object_new (STAMP_TYPE_ACCOUNT_ITEM_MODEL, "account", account, NULL);
}

StampAccount *
stamp_account_item_model_get_account (StampAccountItemModel *self)
{
  return self->account;
}

GListStore *
stamp_account_item_model_get_folder_list (StampItemModel *item_model)
{
  StampAccountItemModel *self = STAMP_ACCOUNT_ITEM_MODEL (item_model);

  return self->list_store;
}

static const char *
stamp_account_item_model_get_name (StampItemModel *item_model)
{
  StampAccountItemModel *self = STAMP_ACCOUNT_ITEM_MODEL (item_model);

  return self->name;
}

static const char *
stamp_account_item_model_get_icon_name (StampItemModel *item_model)
{
  StampAccountItemModel *self = STAMP_ACCOUNT_ITEM_MODEL (item_model);

  return self->icon_name;
}

static void
stamp_account_item_model_item_model_init (StampItemModelInterface *iface)
{
  iface->get_name = stamp_account_item_model_get_name;
  iface->get_icon_name = stamp_account_item_model_get_icon_name;
  iface->get_folder_list = stamp_account_item_model_get_folder_list;
}
