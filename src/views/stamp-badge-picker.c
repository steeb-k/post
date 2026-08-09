/*
 * Copyright 2026 steeb-k
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

#define G_LOG_DOMAIN "stamp-badge-picker"

#include "config.h"

#include <glib/gi18n.h>

#include "stamp-badge.h"
#include "stamp-badge-picker.h"
#include "stamp-profile-button.h"

struct _StampBadgePicker {
  AdwDialog parent_instance;

  GtkFlowBox *grid;

  StampProfile *profile;
};

G_DEFINE_TYPE (StampBadgePicker, stamp_badge_picker, ADW_TYPE_DIALOG)

enum {
  SIGNAL_SELECTED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

static void
on_badge_clicked (GtkButton *button,
                  gpointer   user_data)
{
  StampBadgePicker *self = STAMP_BADGE_PICKER (user_data);
  const gchar *id = g_object_get_data (G_OBJECT (button), "badge-id");

  g_signal_emit (self, signals[SIGNAL_SELECTED], 0, id);

  adw_dialog_close (ADW_DIALOG (self));
}

/*
 * Every badge, drawn the way it would appear: on this profile's colour,
 * at the size the switcher uses it. The one the profile wears carries a
 * checked button around it rather than a mark of its own, so that the
 * badge itself is never obscured.
 */
static void
build_grid (StampBadgePicker *self)
{
  const gchar *chosen = stamp_profile_get_badge (self->profile);
  const gchar *color = stamp_profile_get_color (self->profile);
  const gchar *initial = stamp_profile_get_initial (self->profile);
  guint n_badges = 0;
  const StampBadge *all = stamp_badge_get_all (&n_badges);

  for (guint i = 0; i < n_badges; i++) {
    const StampBadge *badge = &all[i];
    GtkWidget *button = gtk_toggle_button_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *avatar = adw_avatar_new (48, NULL, FALSE);
    GtkWidget *label = gtk_label_new (_(badge->label));

    stamp_profile_button_style_avatar_parts (ADW_AVATAR (avatar), color, initial, badge->id);

    gtk_widget_add_css_class (label, "caption");
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 12);

    gtk_box_append (GTK_BOX (box), avatar);
    gtk_box_append (GTK_BOX (box), label);
    gtk_widget_set_margin_top (box, 6);
    gtk_widget_set_margin_bottom (box, 6);

    gtk_button_set_child (GTK_BUTTON (button), box);
    gtk_widget_add_css_class (button, "flat");
    gtk_widget_set_tooltip_text (button, _(badge->label));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                  g_strcmp0 (badge->id, chosen ? chosen : "") == 0);

    g_object_set_data (G_OBJECT (button), "badge-id", (gpointer)badge->id);
    g_signal_connect (button, "clicked", G_CALLBACK (on_badge_clicked), self);

    gtk_flow_box_append (self->grid, button);
  }
}

static void
stamp_badge_picker_dispose (GObject *object)
{
  StampBadgePicker *self = STAMP_BADGE_PICKER (object);

  g_clear_object (&self->profile);

  gtk_widget_dispose_template (GTK_WIDGET (object), STAMP_TYPE_BADGE_PICKER);

  G_OBJECT_CLASS (stamp_badge_picker_parent_class)->dispose (object);
}

static void
stamp_badge_picker_class_init (StampBadgePickerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = stamp_badge_picker_dispose;

  signals[SIGNAL_SELECTED] = g_signal_new ("selected",
                                           G_TYPE_FROM_CLASS (klass),
                                           G_SIGNAL_RUN_LAST,
                                           0, NULL, NULL, NULL,
                                           G_TYPE_NONE, 1, G_TYPE_STRING);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/stamp-badge-picker.ui");
  gtk_widget_class_bind_template_child (widget_class, StampBadgePicker, grid);
}

static void
stamp_badge_picker_init (StampBadgePicker *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
stamp_badge_picker_new (StampProfile *profile)
{
  StampBadgePicker *self;

  g_return_val_if_fail (STAMP_IS_PROFILE (profile), NULL);

  self = g_object_new (STAMP_TYPE_BADGE_PICKER, NULL);
  self->profile = g_object_ref (profile);

  build_grid (self);

  return GTK_WIDGET (self);
}
