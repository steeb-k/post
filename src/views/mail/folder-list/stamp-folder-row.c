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

#include "stamp-folder-row.h"

#include <adwaita.h>

#include "gcal-utils.h"

#include "stamp-account-item.h"
#include "stamp-folder-color.h"
#include "stamp-folder-item.h"
#include "stamp-palette.h"

/* Smaller than the 16px a symbolic icon fills, so a coloured folder
 * reads as a dot the eye can skim down rather than as another icon
 * competing with the ones above it. */
#define COLOR_DOT_SIZE 12

struct _StampFolderRow {
  GtkBox parent_instance;

  /* Referenced, not borrowed: a colour can change while the list is
   * rebuilding, and repainting then must not reach into an item the
   * model has already let go of. */
  StampItem *item;

  GtkImage *image;
  GtkInscription *inscription;
  GtkLabel *unread;
  AdwSpinner *spinner;
  GtkImage *error;
};

G_DEFINE_FINAL_TYPE (StampFolderRow, stamp_folder_row, GTK_TYPE_BOX);

static void apply_icon (StampFolderRow *self);
static void on_colors_changed (StampFolderColors *colors,
                               const gchar       *account_uid,
                               const gchar       *full_name,
                               gpointer           user_data);

static void
stamp_folder_row_dispose (GObject *object)
{
  StampFolderRow *self = STAMP_FOLDER_ROW (object);

  g_clear_object (&self->item);

  G_OBJECT_CLASS (stamp_folder_row_parent_class)->dispose (object);
}

static void
stamp_folder_row_init (StampFolderRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (stamp_folder_colors_get_default (), "changed",
                           G_CALLBACK (on_colors_changed), self, G_CONNECT_DEFAULT);
}

static void
stamp_folder_row_class_init (StampFolderRowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_folder_row_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/mail/folder-list/stamp-folder-row.ui");

  gtk_widget_class_bind_template_child (widget_class, StampFolderRow, image);
  gtk_widget_class_bind_template_child (widget_class, StampFolderRow, inscription);
  gtk_widget_class_bind_template_child (widget_class, StampFolderRow, unread);
  gtk_widget_class_bind_template_child (widget_class, StampFolderRow, spinner);
  gtk_widget_class_bind_template_child (widget_class, StampFolderRow, error);
}

GtkWidget *
stamp_folder_row_new (void)
{
  return g_object_new (STAMP_TYPE_FOLDER_ROW, NULL);
}

static gboolean
transform_unread_to (GBinding     *binding,
                     const GValue *from_value,
                     GValue       *to_value,
                     gpointer      user_data)
{
  StampFolderRow *self = STAMP_FOLDER_ROW (user_data);
  guint unread = g_value_get_uint (from_value);

  gtk_widget_set_visible (GTK_WIDGET (self->unread), unread > 0);
  if (unread) {
    g_autofree char *label = g_strdup_printf ("%u", unread);

    g_value_set_string (to_value, label);
  }

  return TRUE;
}

static gboolean
transform_error_to (GBinding     *binding,
                    const GValue *from_value,
                    GValue       *to_value,
                    gpointer      user_data)
{
  StampFolderRow *self = STAMP_FOLDER_ROW (user_data);
  GError *error = g_value_get_pointer (from_value);

  if (error)
    gtk_widget_set_tooltip_text (GTK_WIDGET (self->error), error->message);

  g_value_set_boolean (to_value, error != NULL);

  return TRUE;
}

/*
 * The colour the user gave this folder, or NULL. Account rows never
 * have one, and neither does a folder nobody has coloured.
 */
static const gchar *
row_color (StampFolderRow *self)
{
  StampAccount *account;

  if (!self->item || !STAMP_IS_FOLDER_ITEM (self->item))
    return NULL;

  account = stamp_item_get_account (self->item);
  if (!account)
    return NULL;

  return stamp_folder_color_lookup (stamp_account_get_uid (account),
                                    stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (self->item)));
}

static void
apply_icon (StampFolderRow *self)
{
  const gchar *color_id = row_color (self);
  const StampPaletteColor *color = stamp_palette_lookup (color_id);

  if (color) {
    GdkRGBA rgba;

    if (gdk_rgba_parse (&rgba, color->hex)) {
      g_autoptr (GdkPaintable) paintable = get_circle_paintable_from_color (&rgba, COLOR_DOT_SIZE);

      gtk_image_set_from_paintable (self->image, paintable);
      gtk_widget_set_visible (GTK_WIDGET (self->image), TRUE);
      return;
    }
  }

  {
    const gchar *icon_name = self->item ? stamp_item_get_icon_name (self->item) : NULL;

    gtk_image_set_from_icon_name (self->image, icon_name);
    gtk_widget_set_visible (GTK_WIDGET (self->image), icon_name != NULL);
  }
}

static void
on_colors_changed (StampFolderColors *colors,
                   const gchar       *account_uid,
                   const gchar       *full_name,
                   gpointer           user_data)
{
  apply_icon (STAMP_FOLDER_ROW (user_data));
}

void
stamp_folder_row_bind (StampFolderRow *self,
                       StampItem      *item)
{
  g_set_object (&self->item, item);

  apply_icon (self);

  g_object_bind_property (item, "name", self->inscription, "text", G_BINDING_SYNC_CREATE);
  g_object_bind_property (item, "name", self->inscription, "tooltip-text", G_BINDING_SYNC_CREATE);
  g_object_bind_property (item, "loading", self->spinner, "visible", G_BINDING_SYNC_CREATE);
  g_object_bind_property_full (item, "error", self->error, "visible", G_BINDING_SYNC_CREATE, transform_error_to, NULL, g_object_ref (self), g_object_unref);

  if (STAMP_IS_ACCOUNT_ITEM (item)) {
    gtk_widget_add_css_class (GTK_WIDGET (self), "bold");
    gtk_widget_set_visible (GTK_WIDGET (self->unread), FALSE);
  } else {
    g_object_bind_property_full (item, "unread", self->unread, "label", G_BINDING_SYNC_CREATE, transform_unread_to, NULL, g_object_ref (self), g_object_unref);
    gtk_widget_remove_css_class (GTK_WIDGET (self), "bold");
  }
}
