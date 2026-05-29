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

#include "stamp-item.h"

#include <glib.h>

#include "stamp-account.h"

typedef struct {
  gchar *name;
  gchar *icon_name;
  gchar *account_uid;

  StampAccount *account;
  GListStore *list_store;
  gboolean loading;
  GError *error;
} StampItemPrivate;

G_DEFINE_TYPE_WITH_CODE (StampItem, stamp_item, G_TYPE_OBJECT, G_ADD_PRIVATE (StampItem));

typedef enum {
  PROP_ACCOUNT = 1,
  PROP_LOADING,
  PROP_NAME,
  PROP_ERROR,
} StampItemProps;

static GParamSpec *properties[PROP_ERROR + 1];

void
stamp_item_init (StampItem *self)
{
}

static void
stamp_item_dispose (GObject *object)
{
  StampItem *self = STAMP_ITEM (object);
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  g_clear_pointer (&priv->name, g_free);
  g_clear_pointer (&priv->icon_name, g_free);
  g_clear_pointer (&priv->account_uid, g_free);
  g_clear_object (&priv->account);

  if (priv->list_store)
    g_clear_object (&priv->list_store);

  g_clear_object (&priv->error);

  G_OBJECT_CLASS (stamp_item_parent_class)->dispose (object);
}

static void
stamp_item_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  StampItem *self = STAMP_ITEM (object);
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  switch ((StampItemProps) property_id) {
    case PROP_NAME:
      g_set_str (&priv->name, g_value_get_string (value));
      break;
    case PROP_ACCOUNT:
      g_clear_object (&priv->account);

      priv->account = g_value_get_object (value);
      if (priv->account)
        g_object_ref (priv->account);
      break;
    case PROP_LOADING:
      priv->loading = g_value_get_boolean (value);
      break;
    case PROP_ERROR:
      priv->error = g_value_get_pointer (value);
      break;
  }
}

static void
stamp_item_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  StampItem *self = STAMP_ITEM (object);
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  switch ((StampItemProps) property_id) {
    case PROP_ACCOUNT:
      g_value_set_object (value, priv->account);
      break;
    case PROP_LOADING:
      g_value_set_boolean (value, priv->loading);
      break;
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_ERROR:
      g_value_set_pointer (value, priv->error);
      break;
  }
}

void
stamp_item_class_init (StampItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_item_dispose;
  object_class->set_property = stamp_item_set_property;
  object_class->get_property = stamp_item_get_property;

  properties[PROP_ACCOUNT] =
    g_param_spec_object ("account",
                         NULL, NULL,
                         STAMP_TYPE_ACCOUNT,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  properties[PROP_LOADING] =
    g_param_spec_boolean ("loading",
                          NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_NAME] =
    g_param_spec_string ("name",
                         NULL, NULL,
                         "",
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_ERROR] =
    g_param_spec_pointer ("error",
                          NULL, NULL,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);
}

void
stamp_item_set_icon_name (StampItem  *self,
                          const gchar *icon_name)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  if (g_strcmp0 (priv->icon_name, icon_name) != 0) {
    g_set_str (&priv->icon_name, icon_name);
  }
}

const gchar *
stamp_item_get_icon_name (StampItem *self)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  return priv->icon_name;
}

void
stamp_item_set_name (StampItem  *self,
                     const gchar *name)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  if (g_strcmp0 (priv->name, name) != 0) {
    g_set_str (&priv->name, name);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
  }
}

const gchar *
stamp_item_get_name (StampItem *self)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  return priv->name;
}

GListStore *
stamp_item_get_list_store (StampItem *self)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  return priv->list_store;
}

const gchar *
stamp_item_get_account_uid (StampItem *self)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  return priv->account_uid;
}

void
stamp_item_set_list_store_type (StampItem *self,
                                GType      type)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  priv->list_store = g_list_store_new (type);
}

StampAccount *
stamp_item_get_account (StampItem *self)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  return priv->account;
}

void
stamp_item_set_loading (StampItem *self,
                        gboolean   loading)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  priv->loading = loading;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOADING]);
}

void
stamp_item_set_error (StampItem *self,
                      GError    *error)
{
  StampItemPrivate *priv = stamp_item_get_instance_private (self);

  priv->error = error;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ERROR]);
}
