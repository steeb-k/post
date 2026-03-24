/*
 * Copyright 2024-2026 Jan-Michael Brummer
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

#include "stamp-window.h"

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
#include <libportal-gtk4/portal-gtk4.h>

#include "stamp-account.h"
#include "stamp-calendar-view.h"
#include "stamp-contact-view.h"
#include "stamp-mail-view.h"
#include "stamp-session.h"
#include "stamp-settings.h"

struct _StampWindow {
  AdwApplicationWindow parent_instance;

  AdwViewStack *app_view_stack;
  AdwViewStack *main_view_stack;
  GtkWidget *mail_view;
  GtkWidget *contact_view;
  GtkWidget *calendar_view;

  gint current_width;
  gint current_height;
  guint has_default_size : 1;
  guint is_maximized : 1;
};

G_DEFINE_FINAL_TYPE (StampWindow, stamp_window, ADW_TYPE_APPLICATION_WINDOW)

/* TODO: Does not work in Flatpak... */
static void
on_open_settings_clicked (GtkWidget     *listbox,
                          GtkListBoxRow *row,
                          StampWindow   *self)
{
  const char *flatpak_id = getenv ("FLATPAK_ID");

  if (flatpak_id) {
    g_spawn_command_line_async ("flatpak-spawn --host gnome-control-center online-accounts", NULL);
  } else {
    g_autoptr (GError) error = NULL;
    GDesktopAppInfo *app_info = g_desktop_app_info_new ("gnome-online-accounts-panel.desktop");
    GdkAppLaunchContext *context;

    context = gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (self)));
    g_app_info_launch (G_APP_INFO (app_info), NULL, G_APP_LAUNCH_CONTEXT (context), &error);
  }
}

static void
on_account_changed (GObject      *object,
                    StampAccount *account,
                    gpointer      user_data)
{
  StampWindow *self = STAMP_WINDOW (user_data);
  StampSession *session = STAMP_SESSION (object);
  GList *accounts = stamp_session_get_accounts (session);

  if (accounts == NULL) {
    adw_view_stack_set_visible_child_name (self->app_view_stack, "welcome");
  } else {
    adw_view_stack_set_visible_child_name (self->app_view_stack, "main");
  }
}

static void
stamp_window_dispose (GObject *object)
{
  StampWindow *self = STAMP_WINDOW (object);
  StampSession *session = NULL;
  const char *view;

  view = adw_view_stack_get_visible_child_name (self->main_view_stack);
  if (view)
    g_settings_set (STAMP_SETTINGS, "view", "s", view);

  stamp_mail_view_setup (STAMP_MAIL_VIEW (self->mail_view), NULL);
  stamp_calendar_view_setup (STAMP_CALENDAR_VIEW (self->calendar_view), NULL);

  session = stamp_session_get_default ();

  g_signal_handlers_disconnect_by_data (session, self);

  if (self->mail_view) {
    g_signal_handlers_disconnect_by_data (self->mail_view, self);
  }

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_WINDOW);

  G_OBJECT_CLASS (stamp_window_parent_class)->dispose (object);
}

static void
stamp_window_class_init (StampWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_window_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/stamp-window.ui");

  gtk_widget_class_bind_template_child (widget_class, StampWindow, app_view_stack);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, main_view_stack);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, mail_view);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, contact_view);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, calendar_view);

  gtk_widget_class_bind_template_callback (widget_class, on_open_settings_clicked);
}

static void
stamp_window_init (StampWindow *self)
{
  StampSession *session = NULL;
  g_autofree char *view = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  session = stamp_session_get_default ();
  g_settings_bind (STAMP_SETTINGS, "background-notifications", self, "hide-on-close", G_SETTINGS_BIND_DEFAULT);

  g_signal_connect_object (session, "account-added", G_CALLBACK (on_account_changed), self, 0);
  g_signal_connect_object (session, "account-removed", G_CALLBACK (on_account_changed), self, 0);
  g_settings_bind (STAMP_SETTINGS, "window-width", self, "default-width", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, "window-height", self, "default-height", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, "window-maximized", self, "maximized", G_SETTINGS_BIND_DEFAULT);

  stamp_mail_view_setup (STAMP_MAIL_VIEW (self->mail_view), self->main_view_stack);
  stamp_calendar_view_setup (STAMP_CALENDAR_VIEW (self->calendar_view), self->main_view_stack);

  g_settings_get (STAMP_SETTINGS, "view", "s", &view);
  adw_view_stack_set_visible_child_name (self->main_view_stack, view);
}

void
stamp_window_search_contact (StampWindow *self,
                             const char  *mail)
{
  stamp_mail_view_search_contact (STAMP_MAIL_VIEW (self->mail_view), mail);
}

void
stamp_window_show_contact (StampWindow *self,
                           const char  *mail)
{
  stamp_contact_view_show_contact (STAMP_CONTACT_VIEW (self->contact_view), mail);
  adw_view_stack_set_visible_child_name (self->main_view_stack, "contacts");
}

GtkWidget *
stamp_window_get_mail_view (StampWindow *self)
{
  return self->mail_view;
}

GtkWindow *
stamp_get_main_window (void)
{
  GApplication *app = g_application_get_default ();

  return gtk_application_get_active_window (GTK_APPLICATION (app));
}
