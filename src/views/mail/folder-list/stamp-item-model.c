/* stamp-item-model.c
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

#include "stamp-item-model.h"

/* typedef struct { */
/*   char *account_id; */
/*   char *icon_name; */
/*   char *name; */
/*   GListStore *list; */
/* } StampItemModelPrivate; */

G_DEFINE_INTERFACE (StampItemModel, stamp_item_model, G_TYPE_OBJECT)

void
stamp_item_model_default_init (StampItemModelInterface *iface)
{
}

void
stamp_item_model_init (StampItemModel *self)
{
}

StampItemModel *
stamp_item_model_new (void)
{
  return g_object_new (STAMP_TYPE_ITEM_MODEL, NULL);
}

void
stamp_item_model_set_icon_name (StampItemModel *self,
                                const char     *icon_name)
{
  StampItemModelInterface *iface;

  g_assert (STAMP_IS_ITEM_MODEL (self));

  iface = STAMP_ITEM_MODEL_GET_IFACE (self);
  g_assert (iface->set_icon_name);

  iface->set_icon_name (self, icon_name);
}

const char *
stamp_item_model_get_icon_name (StampItemModel *self)
{
  StampItemModelInterface *iface;

  g_assert (STAMP_IS_ITEM_MODEL (self));

  iface = STAMP_ITEM_MODEL_GET_IFACE (self);

  g_assert (iface->get_icon_name);

  return iface->get_icon_name (self);
}

void
stamp_item_model_set_name (StampItemModel *self,
                           const char     *name)
{
  StampItemModelInterface *iface;


  g_assert (STAMP_IS_ITEM_MODEL (self));

  iface = STAMP_ITEM_MODEL_GET_IFACE (self);

  g_assert (iface->set_name);

  return iface->set_name (self, name);
}

const char *
stamp_item_model_get_name (StampItemModel *self)
{
  StampItemModelInterface *iface;

  g_assert (STAMP_IS_ITEM_MODEL (self));

  iface = STAMP_ITEM_MODEL_GET_IFACE (self);

  g_assert (iface->get_icon_name);

  return iface->get_name (self);
}

GListStore *
stamp_item_model_get_folder_list (StampItemModel *self)
{
  StampItemModelInterface *iface;

  g_assert (STAMP_IS_ITEM_MODEL (self));

  iface = STAMP_ITEM_MODEL_GET_IFACE (self);
  g_assert (iface->get_folder_list);

  return iface->get_folder_list (self);
}

/* void */
/* stamp_item_model_set_list_type (StampItemModel *self, */
/*                                 GType           type) */
/* { */
/*   StampItemModelPrivate *priv = stamp_item_model_get_instance_private (self); */

/*   priv->list = g_list_store_new (type); */
/* } */

/* void */
/* stamp_item_model_remove_all (StampItemModel *self) */
/* { */
/*   StampItemModelPrivate *priv = stamp_item_model_get_instance_private (self); */

/*   g_list_store_remove_all (priv->list); */
/* } */

/* void */
/* stamp_item_model_append (StampItemModel       *self, */
/*                          StampItemModel *folder_item) */
/* { */
/*   StampItemModelPrivate *priv = stamp_item_model_get_instance_private (self); */

/*   g_list_store_append (priv->list, folder_item); */
/* } */

/* gpointer */
/* stamp_item_model_get_item (StampItemModel *self, */
/*                            int             pos) */
/* { */
/*   StampItemModelPrivate *priv = stamp_item_model_get_instance_private (self); */

/*   return g_list_model_get_item (G_LIST_MODEL (priv->list), pos); */
/* } */

const char *
stamp_item_model_get_account_uid (StampItemModel *self)
{
  StampItemModelInterface *iface;

  g_assert (STAMP_IS_ITEM_MODEL (self));

  iface = STAMP_ITEM_MODEL_GET_IFACE (self);
  g_assert (iface->get_account_uid);

  return iface->get_account_uid (self);
}
