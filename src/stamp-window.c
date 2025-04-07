/* stamp-window.c
 *
 * Copyright 2025 Jan-Michael Brummer
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

#include "config.h"

#include <gtk/gtk.h>

#include "stamp-account.h"
#include "stamp-window.h"
#include "stamp-session.h"
#include "stamp-settings.h"

#include <gio/gdesktopappinfo.h>

struct _StampWindow
{
  AdwApplicationWindow  parent_instance;

  /* Template widgets */
  AdwViewStack *app_view_stack;
  GtkWidget *mail_view;

  gint current_width;
  gint current_height;
  guint has_default_size : 1;
  guint is_maximized : 1;
};

G_DEFINE_FINAL_TYPE (StampWindow, stamp_window, ADW_TYPE_APPLICATION_WINDOW)

static void
on_open_settings_clicked (GtkWidget     *listbox,
                          GtkListBoxRow *row,
                          StampWindow   *self)
{
  g_autoptr (GError) error = NULL;
  GDesktopAppInfo *app_info = g_desktop_app_info_new ("gnome-online-accounts-panel.desktop");
  GdkAppLaunchContext *context;

  context = gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (self)));
  g_app_info_launch (G_APP_INFO (app_info), NULL, G_APP_LAUNCH_CONTEXT (context), &error);
}

static void
compute_size_cb (StampWindow     *self,
                 GdkToplevelSize *size)
{
  GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (self));
  GdkToplevelState state = gdk_toplevel_get_state (GDK_TOPLEVEL (surface));

  self->is_maximized = gtk_window_is_maximized (GTK_WINDOW (self));

  if (state & (GDK_TOPLEVEL_STATE_FULLSCREEN |
               GDK_TOPLEVEL_STATE_MAXIMIZED |
               GDK_TOPLEVEL_STATE_TILED |
               GDK_TOPLEVEL_STATE_TOP_TILED |
               GDK_TOPLEVEL_STATE_RIGHT_TILED |
               GDK_TOPLEVEL_STATE_BOTTOM_TILED |
               GDK_TOPLEVEL_STATE_LEFT_TILED |
               GDK_TOPLEVEL_STATE_MINIMIZED)) {
    self->current_width = gdk_surface_get_width (surface);
    self->current_height = gdk_surface_get_height (surface);
  } else {
    gtk_window_get_default_size (GTK_WINDOW (self),
                                 &self->current_width,
                                 &self->current_height);
  }
}

static void
stamp_window_class_init (StampWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/stamp-window.ui");

  gtk_widget_class_bind_template_child (widget_class, StampWindow, app_view_stack);
  gtk_widget_class_bind_template_callback (widget_class, on_open_settings_clicked);
}

static void
on_account_changed (GObject      *object,
                    StampAccount *account,
                    gpointer      user_data)
{
  StampWindow *self = STAMP_WINDOW (user_data);
  StampSession *session = STAMP_SESSION (object);
  GList *accounts = stamp_session_get_accounts (session);

  if (!accounts || g_list_length (accounts) == 0) {
    adw_view_stack_set_visible_child_name (self->app_view_stack, "welcome");
  } else {
    adw_view_stack_set_visible_child_name (self->app_view_stack, "main");
  }
}

static void
stamp_window_init (StampWindow *self)
{
  StampSession *session = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  session = stamp_session_get_default ();

  g_signal_connect (session, "account-added", G_CALLBACK (on_account_changed), self);
  /* g_signal_connect (session, "account-removed", G_CALLBACK (on_account_changed), self); */
  g_settings_bind (STAMP_SETTINGS, "window-width", self, "default-width", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, "window-height", self, "default-height", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, "window-maximized", self, "maximized", G_SETTINGS_BIND_DEFAULT);
}
