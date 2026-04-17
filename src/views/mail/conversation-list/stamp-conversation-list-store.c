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

#include "stamp-conversation-list-store.h"

#include "stamp-conversation-item.h"

struct _StampConversationListStore {
  GObject parent_instance;

  GPtrArray *data;
};

static void stamp_conversation_list_store_list_model_iface_init (GListModelInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (StampConversationListStore, stamp_conversation_list_store, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, stamp_conversation_list_store_list_model_iface_init));

static void
stamp_conversation_list_store_finalize (GObject *object)
{
  StampConversationListStore *self = STAMP_CONVERSATION_LIST_STORE (object);

  g_ptr_array_free (self->data, TRUE);

  G_OBJECT_CLASS (stamp_conversation_list_store_parent_class)->finalize (object);
}

void
stamp_conversation_list_store_class_init (StampConversationListStoreClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = stamp_conversation_list_store_finalize;
}

void
stamp_conversation_list_store_init (StampConversationListStore *self)
{
  self->data = g_ptr_array_new_full (0, g_object_unref);
}

static GType
stamp_conversation_list_store_get_item_type (GListModel *model)
{
  return STAMP_TYPE_CONVERSATION_ITEM;
}

static guint
stamp_conversation_list_store_get_n_items (GListModel *model)
{
  StampConversationListStore *self = STAMP_CONVERSATION_LIST_STORE (model);

  return self->data->len;
}

static gpointer
stamp_conversation_list_store_get_item (GListModel *model,
                                        guint       index)
{
  StampConversationListStore *self = STAMP_CONVERSATION_LIST_STORE (model);
  gint n_items = self->data->len;

  if (index >= n_items)
    return NULL;

  return g_object_ref (g_ptr_array_index (self->data, index));
}

static void
stamp_conversation_list_store_list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = stamp_conversation_list_store_get_item_type;
  iface->get_n_items = stamp_conversation_list_store_get_n_items;
  iface->get_item = stamp_conversation_list_store_get_item;
}

void
stamp_conversation_list_store_remove_all (StampConversationListStore *self)
{
  guint removed;

  if (!self)
    return;

  removed = self->data->len;
  for (int idx = self->data->len - 1; idx >= 0; idx--) {
    g_ptr_array_remove_index (self->data, idx);
  }

  g_list_model_items_changed (G_LIST_MODEL (self), 0, removed, 0);
}

StampConversationListStore *
stamp_conversation_list_store_new (void)
{
  return g_object_new (STAMP_TYPE_CONVERSATION_LIST_STORE, NULL);
}

void
stamp_conversation_list_store_add (StampConversationListStore *self,
                                   StampConversationItem      *item)
{
  guint pos = self->data->len;
  g_ptr_array_add (self->data, g_object_ref (item));
  g_list_model_items_changed (G_LIST_MODEL (self), pos, 0, 1);
}
