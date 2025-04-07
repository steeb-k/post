/* stamp-folder-item-model.c
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
#include "stamp-folder-item-model.h"

struct _StampFolderItemModel {
  GObject parent_instance;

  char *name;
  char *icon_name;
  char *account_uid;
  gint unread;
  GListStore *list_store;
  StampAccount *account;
  CamelFolderInfo *folder_info;
  /* GCancellable *cancellable; */
};

static void stamp_folder_item_model_item_model_init (StampItemModelInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (StampFolderItemModel, stamp_folder_item_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (STAMP_TYPE_ITEM_MODEL, stamp_folder_item_model_item_model_init))

enum {
  PROP_0,
  PROP_ACCOUNT,
  LAST_PROP
};

static GParamSpec *obj_properties[LAST_PROP];

static void
stamp_folder_item_model_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  StampFolderItemModel *self = STAMP_FOLDER_ITEM_MODEL (object);

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
stamp_folder_item_model_get_property (GObject    *object,
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

void
stamp_folder_item_model_constructed (GObject *object)
{
}

void
stamp_folder_item_model_class_init (StampFolderItemModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = stamp_folder_item_model_constructed;
  object_class->set_property = stamp_folder_item_model_set_property;
  object_class->get_property = stamp_folder_item_model_get_property;

  obj_properties[PROP_ACCOUNT] =
    g_param_spec_object ("account",
                      NULL, NULL,
                      STAMP_TYPE_ACCOUNT,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, obj_properties);
}

void
stamp_folder_item_model_init (StampFolderItemModel *self)
{
}

StampFolderItemModel *
stamp_folder_item_model_new (StampAccount *account)
{
  return g_object_new (STAMP_TYPE_FOLDER_ITEM_MODEL,
                       "account", account,
                       NULL);
}

static const char *
get_icon (guint32 flags)
{
  switch (flags & CAMEL_FOLDER_TYPE_MASK) {
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

void
stamp_folder_item_set_folder_info (StampFolderItemModel *self,
                                   CamelFolderInfo      *folder_info)
{
  self->folder_info = folder_info;

  self->name = g_strdup (self->folder_info->display_name);


  self->unread = self->folder_info->unread;

  self->icon_name = g_strdup (get_icon(self->folder_info->flags));

  if (folder_info->child) {
    CamelFolderInfo *current_folder_info = folder_info->child;

    g_print ("Add childs\n");
    while (current_folder_info) {
      StampFolderItemModel *folder_item = stamp_folder_item_model_new (self->account);
      stamp_folder_item_set_folder_info (folder_item, current_folder_info);

      if (!self->list_store)
        self->list_store = g_list_store_new (STAMP_TYPE_FOLDER_ITEM_MODEL);

      g_list_store_append (self->list_store, STAMP_ITEM_MODEL (folder_item));

      current_folder_info = current_folder_info->next;
    }
  }

  /* if (stamp_mail_folder_get_child_count (self->folder) != 0) { */
  /*   GListStore *childs; */

  /*   stamp_item_model_set_list_type (STAMP_ITEM_MODEL (self), STAMP_TYPE_FOLDER_ITEM_MODEL); */

  /*   childs = stamp_mail_folder_get_child_model (self->folder); */
  /*   for (int i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (childs)); i++) { */
  /*     StampFolderItemModel *folder_item = stamp_folder_item_model_new (g_list_model_get_item (G_LIST_MODEL (childs), i), self->account); */
  /*     stamp_item_model_append (STAMP_ITEM_MODEL (self), STAMP_ITEM_MODEL (folder_item)); */
  /*   } */
  /* } */

}

/* StampMailFolder * */
/* stamp_folder_item_model_get_folder (StampFolderItemModel *self) */
/* { */
/*   return self->folder; */
/* } */

StampAccount *
stamp_folder_item_model_get_account (StampFolderItemModel *self)
{
  return self->account;
}

GListStore *
stamp_folder_item_model_get_folder_list (StampItemModel *item_model)
{
  StampFolderItemModel *self = STAMP_FOLDER_ITEM_MODEL (item_model);

  return self->list_store;
}

static const char *
stamp_folder_item_model_get_name (StampItemModel *item_model)
{
  StampFolderItemModel *self = STAMP_FOLDER_ITEM_MODEL (item_model);

  return self->name;
}

static const char *
stamp_folder_item_model_get_icon_name (StampItemModel *item_model)
{
  StampFolderItemModel *self = STAMP_FOLDER_ITEM_MODEL (item_model);

  return self->icon_name;
}

static const char *
stamp_folder_item_model_get_account_uid (StampItemModel *item_model)
{
  StampFolderItemModel *self = STAMP_FOLDER_ITEM_MODEL (item_model);

  return self->account_uid;
}

static void
stamp_folder_item_model_item_model_init (StampItemModelInterface *iface)
{
  iface->get_name = stamp_folder_item_model_get_name;
  iface->get_icon_name = stamp_folder_item_model_get_icon_name;
  iface->get_account_uid = stamp_folder_item_model_get_account_uid;

  iface->get_folder_list = stamp_folder_item_model_get_folder_list;
}

CamelFolderInfo *
stamp_folder_item_model_get_folder_info (StampFolderItemModel *self)
{
  return self->folder_info;
}
