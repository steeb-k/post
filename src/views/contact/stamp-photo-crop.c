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

#include "stamp-photo-crop.h"

#include <glib/gi18n.h>

#define STAMP_CROP_VIEWPORT 280
#define STAMP_CROP_MAX_ZOOM 8.0

/*
 * The crop area: one square of the picture, drawn under a circular clip
 * because that is the shape an avatar is ever seen in. What gets stored
 * is the square around that circle, so nothing the user framed is lost
 * to a client that shows contact photos square.
 */

#define STAMP_TYPE_CROP_AREA (stamp_crop_area_get_type ())
G_DECLARE_FINAL_TYPE (StampCropArea, stamp_crop_area, STAMP, CROP_AREA, GtkWidget);

struct _StampCropArea {
  GtkWidget parent_instance;

  GdkTexture *texture;

  /* The point of the picture sitting at the middle of the circle, in
   * fractions of the picture, and how far in it is zoomed. */
  double centre_x;
  double centre_y;
  double zoom;

  double drag_centre_x;
  double drag_centre_y;
};

G_DEFINE_FINAL_TYPE (StampCropArea, stamp_crop_area, GTK_TYPE_WIDGET);

/* Scale at which the picture exactly covers the circle. */
static double
cover_scale (StampCropArea *self,
             double         side)
{
  double width = gdk_texture_get_width (self->texture);
  double height = gdk_texture_get_height (self->texture);

  return side / MIN (width, height);
}

static double
viewport_side (StampCropArea *self)
{
  return MIN (gtk_widget_get_width (GTK_WIDGET (self)),
              gtk_widget_get_height (GTK_WIDGET (self)));
}

/* Keep the picture covering the circle: panning stops where an edge
 * would otherwise come into view. */
static void
clamp_centre (StampCropArea *self)
{
  double side = viewport_side (self);
  double scale;
  double drawn_width;
  double drawn_height;
  double margin_x;
  double margin_y;

  if (!self->texture || side <= 0)
    return;

  scale = cover_scale (self, side) * self->zoom;
  drawn_width = gdk_texture_get_width (self->texture) * scale;
  drawn_height = gdk_texture_get_height (self->texture) * scale;

  margin_x = (side / 2.0) / drawn_width;
  margin_y = (side / 2.0) / drawn_height;

  self->centre_x = (margin_x * 2.0 >= 1.0) ? 0.5 : CLAMP (self->centre_x, margin_x, 1.0 - margin_x);
  self->centre_y = (margin_y * 2.0 >= 1.0) ? 0.5 : CLAMP (self->centre_y, margin_y, 1.0 - margin_y);
}

static void
stamp_crop_area_measure (GtkWidget      *widget,
                         GtkOrientation  orientation,
                         gint            for_size,
                         gint           *minimum,
                         gint           *natural,
                         gint           *minimum_baseline,
                         gint           *natural_baseline)
{
  *minimum = STAMP_CROP_VIEWPORT;
  *natural = STAMP_CROP_VIEWPORT;
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

static void
stamp_crop_area_snapshot (GtkWidget   *widget,
                          GtkSnapshot *snapshot)
{
  StampCropArea *self = STAMP_CROP_AREA (widget);
  double side = viewport_side (self);
  double offset_x;
  double offset_y;
  double scale;
  double drawn_width;
  double drawn_height;
  graphene_rect_t bounds;
  GskRoundedRect clip;

  if (!self->texture || side <= 0)
    return;

  offset_x = (gtk_widget_get_width (widget) - side) / 2.0;
  offset_y = (gtk_widget_get_height (widget) - side) / 2.0;

  bounds = GRAPHENE_RECT_INIT (offset_x, offset_y, side, side);
  gsk_rounded_rect_init_from_rect (&clip, &bounds, side / 2.0);

  scale = cover_scale (self, side) * self->zoom;
  drawn_width = gdk_texture_get_width (self->texture) * scale;
  drawn_height = gdk_texture_get_height (self->texture) * scale;

  gtk_snapshot_push_rounded_clip (snapshot, &clip);
  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot,
                          &GRAPHENE_POINT_INIT (offset_x + side / 2.0 - self->centre_x * drawn_width,
                                                offset_y + side / 2.0 - self->centre_y * drawn_height));
  gtk_snapshot_scale (snapshot, scale, scale);
  gdk_paintable_snapshot (GDK_PAINTABLE (self->texture), snapshot,
                          gdk_texture_get_width (self->texture),
                          gdk_texture_get_height (self->texture));
  gtk_snapshot_restore (snapshot);
  gtk_snapshot_pop (snapshot);
}

static void
on_drag_begin (GtkGestureDrag *gesture,
               double          start_x,
               double          start_y,
               gpointer        user_data)
{
  StampCropArea *self = STAMP_CROP_AREA (user_data);

  self->drag_centre_x = self->centre_x;
  self->drag_centre_y = self->centre_y;
}

static void
on_drag_update (GtkGestureDrag *gesture,
                double          offset_x,
                double          offset_y,
                gpointer        user_data)
{
  StampCropArea *self = STAMP_CROP_AREA (user_data);
  double side = viewport_side (self);
  double scale;

  if (!self->texture || side <= 0)
    return;

  scale = cover_scale (self, side) * self->zoom;

  self->centre_x = self->drag_centre_x - offset_x / (gdk_texture_get_width (self->texture) * scale);
  self->centre_y = self->drag_centre_y - offset_y / (gdk_texture_get_height (self->texture) * scale);

  clamp_centre (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
stamp_crop_area_set_zoom (StampCropArea *self,
                          double         zoom)
{
  self->zoom = CLAMP (zoom, 1.0, STAMP_CROP_MAX_ZOOM);
  clamp_centre (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static gboolean
on_scroll (GtkEventControllerScroll *controller,
           double                    delta_x,
           double                    delta_y,
           gpointer                  user_data)
{
  StampCropArea *self = STAMP_CROP_AREA (user_data);

  stamp_crop_area_set_zoom (self, self->zoom * (delta_y < 0 ? 1.1 : 1.0 / 1.1));

  return GDK_EVENT_STOP;
}

static void
stamp_crop_area_dispose (GObject *object)
{
  StampCropArea *self = STAMP_CROP_AREA (object);

  g_clear_object (&self->texture);

  G_OBJECT_CLASS (stamp_crop_area_parent_class)->dispose (object);
}

static void
stamp_crop_area_class_init (StampCropAreaClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = stamp_crop_area_dispose;

  widget_class->measure = stamp_crop_area_measure;
  widget_class->snapshot = stamp_crop_area_snapshot;
}

static void
stamp_crop_area_init (StampCropArea *self)
{
  GtkGesture *drag = gtk_gesture_drag_new ();
  GtkEventController *scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);

  self->centre_x = 0.5;
  self->centre_y = 0.5;
  self->zoom = 1.0;

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "grab");

  g_signal_connect (drag, "drag-begin", G_CALLBACK (on_drag_begin), self);
  g_signal_connect (drag, "drag-update", G_CALLBACK (on_drag_update), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drag));

  g_signal_connect (scroll, "scroll", G_CALLBACK (on_scroll), self);
  gtk_widget_add_controller (GTK_WIDGET (self), scroll);
}

static void
stamp_crop_area_set_texture (StampCropArea *self,
                             GdkTexture    *texture)
{
  g_set_object (&self->texture, texture);
  self->centre_x = 0.5;
  self->centre_y = 0.5;
  self->zoom = 1.0;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

/*
 * Render just the framed square, at the size that goes into the vCard.
 * Drawing it through the renderer rather than resampling by hand keeps
 * the result identical to the preview the user was looking at.
 */
static GdkTexture *
stamp_crop_area_render (StampCropArea *self,
                        gint           size)
{
  g_autoptr (GskRenderNode) node = NULL;
  GtkSnapshot *snapshot;
  GtkNative *native;
  GskRenderer *renderer;
  graphene_rect_t viewport;
  double side = viewport_side (self);
  double scale;
  double side_in_picture;
  double origin_x;
  double origin_y;
  double factor;

  if (!self->texture || side <= 0)
    return NULL;

  native = gtk_widget_get_native (GTK_WIDGET (self));
  renderer = native ? gtk_native_get_renderer (native) : NULL;
  if (!renderer)
    return NULL;

  scale = cover_scale (self, side) * self->zoom;
  side_in_picture = side / scale;
  origin_x = self->centre_x * gdk_texture_get_width (self->texture) - side_in_picture / 2.0;
  origin_y = self->centre_y * gdk_texture_get_height (self->texture) - side_in_picture / 2.0;
  factor = size / side_in_picture;

  snapshot = gtk_snapshot_new ();
  gtk_snapshot_scale (snapshot, factor, factor);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (-origin_x, -origin_y));
  gdk_paintable_snapshot (GDK_PAINTABLE (self->texture), snapshot,
                          gdk_texture_get_width (self->texture),
                          gdk_texture_get_height (self->texture));

  node = gtk_snapshot_free_to_node (snapshot);
  if (!node)
    return NULL;

  viewport = GRAPHENE_RECT_INIT (0, 0, size, size);

  return gsk_renderer_render_texture (renderer, node, &viewport);
}

/*
 * The dialog around it.
 */

struct _StampPhotoCrop {
  AdwDialog parent_instance;

  GtkWidget *crop_container;
  GtkWidget *zoom_scale;
  GtkWidget *select_button;

  StampCropArea *area;

  StampPhotoCropReady callback;
  gpointer user_data;
};

G_DEFINE_FINAL_TYPE (StampPhotoCrop, stamp_photo_crop, ADW_TYPE_DIALOG);

/* The size a contact photo is stored at: large enough for the biggest
 * avatar the app draws, small enough that a book full of them still
 * syncs in reasonable time. */
#define STAMP_PHOTO_SIZE 256

static void
on_cancel_clicked (GtkButton *button,
                   gpointer   user_data)
{
  adw_dialog_close (ADW_DIALOG (user_data));
}

static void
on_select_clicked (GtkButton *button,
                   gpointer   user_data)
{
  StampPhotoCrop *self = STAMP_PHOTO_CROP (user_data);
  g_autoptr (GdkTexture) texture = stamp_crop_area_render (self->area, STAMP_PHOTO_SIZE);

  if (self->callback)
    self->callback (texture, self->user_data);

  adw_dialog_close (ADW_DIALOG (self));
}

static void
on_zoom_changed (GtkRange *range,
                 gpointer  user_data)
{
  StampPhotoCrop *self = STAMP_PHOTO_CROP (user_data);

  stamp_crop_area_set_zoom (self->area, gtk_range_get_value (range));
}

static void
stamp_photo_crop_dispose (GObject *object)
{
  StampPhotoCrop *self = STAMP_PHOTO_CROP (object);

  self->callback = NULL;

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_PHOTO_CROP);

  G_OBJECT_CLASS (stamp_photo_crop_parent_class)->dispose (object);
}

static void
stamp_photo_crop_class_init (StampPhotoCropClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = stamp_photo_crop_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/contact/stamp-photo-crop.ui");

  gtk_widget_class_bind_template_child (widget_class, StampPhotoCrop, crop_container);
  gtk_widget_class_bind_template_child (widget_class, StampPhotoCrop, zoom_scale);
  gtk_widget_class_bind_template_child (widget_class, StampPhotoCrop, select_button);

  gtk_widget_class_bind_template_callback (widget_class, on_cancel_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_select_clicked);
}

static void
stamp_photo_crop_init (StampPhotoCrop *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->area = g_object_new (STAMP_TYPE_CROP_AREA, NULL);
  gtk_box_append (GTK_BOX (self->crop_container), GTK_WIDGET (self->area));

  g_signal_connect (self->zoom_scale, "value-changed", G_CALLBACK (on_zoom_changed), self);
}

void
stamp_photo_crop_present (GtkWidget           *parent,
                          GFile               *file,
                          StampPhotoCropReady  callback,
                          gpointer             user_data)
{
  StampPhotoCrop *self;
  g_autoptr (GdkTexture) texture = NULL;
  g_autoptr (GError) error = NULL;

  texture = gdk_texture_new_from_file (file, &error);
  if (!texture) {
    AdwDialog *alert = adw_alert_dialog_new (_("Could Not Open Picture"), error->message);

    adw_alert_dialog_add_response (ADW_ALERT_DIALOG (alert), "close", _("_Close"));
    adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "close");
    adw_dialog_present (alert, parent);

    if (callback)
      callback (NULL, user_data);

    return;
  }

  self = g_object_new (STAMP_TYPE_PHOTO_CROP, NULL);
  self->callback = callback;
  self->user_data = user_data;

  stamp_crop_area_set_texture (self->area, texture);
  gtk_range_set_value (GTK_RANGE (self->zoom_scale), 1.0);

  adw_dialog_present (ADW_DIALOG (self), parent);
}
