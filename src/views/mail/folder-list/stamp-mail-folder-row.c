/* stamp-mail-folder-row.c
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

#include "stamp-mail-folder-row.h"

#include "stamp-folder-item-model.h"

typedef struct {
  GtkWidget *image;
  GtkWidget *inscription;
  GtkWidget *unread;
} StampMailFolderRowPrivate;

#define GET_PRIVATE(o) stamp_mail_folder_row_get_instance_private (o)

G_DEFINE_TYPE_WITH_PRIVATE (StampMailFolderRow, stamp_mail_folder_row, GTK_TYPE_BOX)

static void
stamp_mail_folder_row_init (StampMailFolderRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
stamp_mail_folder_row_class_init (StampMailFolderRowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/folder-list/stamp-mail-folder-row.ui");
  gtk_widget_class_bind_template_child_private (widget_class, StampMailFolderRow, image);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailFolderRow, inscription);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailFolderRow, unread);
}

GtkWidget *
stamp_mail_folder_row_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_FOLDER_ROW, NULL);
}

void
stamp_mail_folder_row_bind (StampMailFolderRow *self,
                            StampItemModel     *item_model)
{
  StampMailFolderRowPrivate *priv = GET_PRIVATE (self);

  if (stamp_item_model_get_icon_name (item_model)) {
    gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), stamp_item_model_get_icon_name (item_model));
    gtk_widget_set_visible (priv->image, TRUE);
  } else {
    gtk_widget_set_visible (priv->image, FALSE);
  }
  gtk_inscription_set_text (GTK_INSCRIPTION (priv->inscription), stamp_item_model_get_name (item_model));
  gtk_widget_set_tooltip_text (GTK_WIDGET (self), stamp_item_model_get_name (item_model));

  if (STAMP_IS_FOLDER_ITEM_MODEL (item_model)) {
    CamelFolderInfo *folder_info = stamp_folder_item_model_get_folder_info (STAMP_FOLDER_ITEM_MODEL (item_model));
    gint unread = folder_info->unread;
    g_autofree char *label = g_strdup_printf ("%u", unread);

    gtk_label_set_text (GTK_LABEL (priv->unread), label);
    gtk_widget_set_visible (priv->unread, unread > 0);
  } else {
    gtk_widget_set_visible (priv->unread, FALSE);
  }
}

