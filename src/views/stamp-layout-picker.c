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

/*
 * The three little pictures of a window that stand in for the mail
 * layouts. Two places offer the choice -- the primary menu, which
 * changes the layout on screen, and preferences, which changes the
 * default -- so the tiles live here and each caller names the action
 * they should drive.
 */

#include "stamp-layout-picker.h"

#include <glib/gi18n.h>

#include "stamp-settings.h"

struct _StampLayoutPicker {
  AdwBin parent_instance;

  AdwWrapBox *wrap;
  GtkWidget *column;
  GtkToggleButton *side_by_side;
  GtkToggleButton *stacked;
  GtkToggleButton *dense;

  GtkOrientation orientation;
  gchar *action_name;
};

enum {
  PROP_0,
  PROP_ACTION_NAME,
  PROP_ORIENTATION,
  N_PROPS
};

enum {
  SELECTED,
  N_SIGNALS
};

static GParamSpec *properties[N_PROPS] = { NULL, };
static guint signals[N_SIGNALS] = { 0, };

G_DEFINE_FINAL_TYPE_WITH_CODE (StampLayoutPicker, stamp_layout_picker, ADW_TYPE_BIN,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_ORIENTABLE, NULL));

static GtkToggleButton *
tile (StampLayoutPicker *self,
      StampMailLayout    layout)
{
  switch (layout) {
    case STAMP_MAIL_LAYOUT_STACKED:
      return self->stacked;

    case STAMP_MAIL_LAYOUT_DENSE:
      return self->dense;

    case STAMP_MAIL_LAYOUT_SIDE_BY_SIDE:
    default:
      return self->side_by_side;
  }
}

/*
 * Each tile drives the same action with its own layout as the target, so
 * which one reads as chosen follows the action's state without any help
 * from here.
 */
static void
apply_action_name (StampLayoutPicker *self)
{
  const StampMailLayout layouts[] = {
    STAMP_MAIL_LAYOUT_SIDE_BY_SIDE,
    STAMP_MAIL_LAYOUT_STACKED,
    STAMP_MAIL_LAYOUT_DENSE,
  };

  for (guint i = 0; i < G_N_ELEMENTS (layouts); i++) {
    GtkActionable *button = GTK_ACTIONABLE (tile (self, layouts[i]));
    g_autofree gchar *detailed = NULL;

    if (self->action_name == NULL) {
      gtk_actionable_set_action_name (button, NULL);
      continue;
    }

    detailed = g_strdup_printf ("%s::%s",
                                self->action_name,
                                stamp_mail_layout_to_nick (layouts[i]));

    gtk_actionable_set_detailed_action_name (button, detailed);
  }
}

/*
 * Laid out as a row, the tiles are in a wrap box, which is what lets a
 * phone-narrow preferences page fold them onto a second line instead of
 * growing wider than the window. Laid out as a column they move to a
 * plain box, because a wrap box asked for its width at a height too
 * short for one column answers with the width of two or three columns --
 * and a popover menu measures every page at the height of whichever page
 * is showing, so a wrapping column would squeeze the menu it lives in.
 */
static void
set_orientation (StampLayoutPicker *self,
                 GtkOrientation     orientation)
{
  GtkWidget *tiles[] = {
    GTK_WIDGET (self->side_by_side),
    GTK_WIDGET (self->stacked),
    GTK_WIDGET (self->dense),
  };
  GtkWidget *from;
  GtkWidget *to;

  if (self->orientation == orientation)
    return;

  self->orientation = orientation;

  if (orientation == GTK_ORIENTATION_VERTICAL) {
    from = GTK_WIDGET (self->wrap);
    to = self->column;
  } else {
    from = self->column;
    to = GTK_WIDGET (self->wrap);
  }

  for (guint i = 0; i < G_N_ELEMENTS (tiles); i++) {
    g_object_ref (tiles[i]);

    if (ADW_IS_WRAP_BOX (from))
      adw_wrap_box_remove (ADW_WRAP_BOX (from), tiles[i]);
    else
      gtk_box_remove (GTK_BOX (from), tiles[i]);

    if (ADW_IS_WRAP_BOX (to))
      adw_wrap_box_append (ADW_WRAP_BOX (to), tiles[i]);
    else
      gtk_box_append (GTK_BOX (to), tiles[i]);

    g_object_unref (tiles[i]);
  }

  adw_bin_set_child (ADW_BIN (self), to);
}

static void
on_tile_clicked (GtkButton *button,
                 gpointer   user_data)
{
  g_signal_emit (STAMP_LAYOUT_PICKER (user_data), signals[SELECTED], 0);
}

static void
stamp_layout_picker_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  StampLayoutPicker *self = STAMP_LAYOUT_PICKER (object);

  switch (prop_id) {
    case PROP_ACTION_NAME:
      g_value_set_string (value, self->action_name);
      break;

    case PROP_ORIENTATION:
      g_value_set_enum (value, self->orientation);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
stamp_layout_picker_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  StampLayoutPicker *self = STAMP_LAYOUT_PICKER (object);

  switch (prop_id) {
    case PROP_ACTION_NAME:
      if (g_set_str (&self->action_name, g_value_get_string (value))) {
        apply_action_name (self);
        g_object_notify_by_pspec (object, properties[PROP_ACTION_NAME]);
      }
      break;

    case PROP_ORIENTATION:
      set_orientation (self, g_value_get_enum (value));
      g_object_notify (object, "orientation");
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
stamp_layout_picker_finalize (GObject *object)
{
  StampLayoutPicker *self = STAMP_LAYOUT_PICKER (object);

  g_clear_object (&self->wrap);
  g_clear_object (&self->column);
  g_clear_pointer (&self->action_name, g_free);

  G_OBJECT_CLASS (stamp_layout_picker_parent_class)->finalize (object);
}

static void
stamp_layout_picker_class_init (StampLayoutPickerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = stamp_layout_picker_get_property;
  object_class->set_property = stamp_layout_picker_set_property;
  object_class->finalize = stamp_layout_picker_finalize;

  /**
   * StampLayoutPicker:action-name:
   *
   * The stateful string action the tiles drive, such as
   * `win.mail-layout`. Each tile passes its own layout nick as the
   * target, and shows itself chosen while the state matches.
   */
  properties[PROP_ACTION_NAME] =
    g_param_spec_string ("action-name", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_property (object_class, PROP_ACTION_NAME, properties[PROP_ACTION_NAME]);

  /* Horizontal is a row of tiles; vertical stacks them into a column. */
  g_object_class_override_property (object_class, PROP_ORIENTATION, "orientation");

  /**
   * StampLayoutPicker::selected:
   *
   * A tile was clicked. The action has already been told; this is for
   * callers that need to tidy up after the click, such as a popover
   * that should close itself.
   */
  signals[SELECTED] = g_signal_new ("selected",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_FIRST,
                                    0, NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/stamp-layout-picker.ui");

  gtk_widget_class_bind_template_child (widget_class, StampLayoutPicker, wrap);
  gtk_widget_class_bind_template_child (widget_class, StampLayoutPicker, side_by_side);
  gtk_widget_class_bind_template_child (widget_class, StampLayoutPicker, stacked);
  gtk_widget_class_bind_template_child (widget_class, StampLayoutPicker, dense);
}

static void
stamp_layout_picker_init (StampLayoutPicker *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->orientation = GTK_ORIENTATION_HORIZONTAL;

  /* Both containers outlive being swapped out of the bin */
  g_object_ref (self->wrap);
  self->column = g_object_ref_sink (gtk_box_new (GTK_ORIENTATION_VERTICAL, 12));
  gtk_widget_set_halign (self->column, GTK_ALIGN_CENTER);

  g_signal_connect (self->side_by_side, "clicked", G_CALLBACK (on_tile_clicked), self);
  g_signal_connect (self->stacked, "clicked", G_CALLBACK (on_tile_clicked), self);
  g_signal_connect (self->dense, "clicked", G_CALLBACK (on_tile_clicked), self);
}

GtkWidget *
stamp_layout_picker_new (const gchar *action_name)
{
  return g_object_new (STAMP_TYPE_LAYOUT_PICKER, "action-name", action_name, NULL);
}
