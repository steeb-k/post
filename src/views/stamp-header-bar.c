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

#include "stamp-header-bar.h"

#include "stamp-layout-picker.h"
#include "stamp-profile-button.h"

struct _StampHeaderBar {
  AdwBin parent_instance;

  GtkMenuButton *menu_button;
};

G_DEFINE_FINAL_TYPE (StampHeaderBar, stamp_header_bar, ADW_TYPE_BIN);

/*
 * Nothing closes a popover menu after a custom child is used -- that only
 * happens for menu items -- so the picker says when it has been used.
 */
static void
on_layout_selected (StampLayoutPicker *picker,
                    gpointer           user_data)
{
  GtkPopover *popover = gtk_menu_button_get_popover (STAMP_HEADER_BAR (user_data)->menu_button);

  if (popover)
    gtk_popover_popdown (popover);
}

static void
stamp_header_bar_init (StampHeaderBar *self)
{
  GtkPopover *popover;
  GtkWidget *picker;

  gtk_widget_init_template (GTK_WIDGET (self));

  popover = gtk_menu_button_get_popover (self->menu_button);

  if (!GTK_IS_POPOVER_MENU (popover)) {
    g_warning ("%s: primary menu is not a popover menu, cannot add the layout picker", G_STRFUNC);
    return;
  }

  /*
   * Stacked into a column, the tiles are narrower than the menu itself, so
   * the popover's width-homogeneous stack of pages costs nothing and the
   * menu is left at its own width. Sliding between pages of one width is
   * also the only part of the transition that does not have to animate.
   */
  picker = stamp_layout_picker_new ("win.mail-layout");
  gtk_orientable_set_orientation (GTK_ORIENTABLE (picker), GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start (picker, 6);
  gtk_widget_set_margin_end (picker, 6);
  gtk_widget_set_margin_top (picker, 6);
  gtk_widget_set_margin_bottom (picker, 6);

  g_signal_connect (picker, "selected", G_CALLBACK (on_layout_selected), self);

  if (!gtk_popover_menu_add_child (GTK_POPOVER_MENU (popover), picker, "layout"))
    g_warning ("%s: primary menu has no \"layout\" slot for the layout picker", G_STRFUNC);
}

static void
stamp_header_bar_class_init (StampHeaderBarClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (STAMP_TYPE_PROFILE_BUTTON);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/stamp-header-bar.ui");

  gtk_widget_class_bind_template_child (widget_class, StampHeaderBar, menu_button);
}
