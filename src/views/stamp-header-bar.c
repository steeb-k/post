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

#include "stamp-profile-button.h"

struct _StampHeaderBar {
  AdwBin parent_instance;
};

G_DEFINE_FINAL_TYPE (StampHeaderBar, stamp_header_bar, ADW_TYPE_BIN);

static void
stamp_header_bar_init (StampHeaderBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
stamp_header_bar_class_init (StampHeaderBarClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (STAMP_TYPE_PROFILE_BUTTON);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/stamp-header-bar.ui");
}
