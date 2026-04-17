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

#include "stamp-account.h"
#include "stamp-calendar-view.h"
#include "stamp-contact-view.h"
#include "stamp-helper.h"
#include "stamp-mail-view.h"
#include "stamp-session.h"
#include "stamp-settings.h"

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
#include <libportal-gtk4/portal-gtk4.h>

struct _StampWindow {
  AdwApplicationWindow parent_instance;

  AdwViewStack *app_view_stack;
  AdwViewStack *main_view_stack;
  StampMailView *mail_view;
  StampContactView *contact_view;
  StampCalendarView *calendar_view;

  gint current_width;
  gint current_height;
  guint has_default_size : 1;
  guint is_maximized : 1;
};

G_DEFINE_FINAL_TYPE (StampWindow, stamp_window, ADW_TYPE_APPLICATION_WINDOW);

static void
on_open_settings_clicked (GtkWidget     *listbox,
                          GtkListBoxRow *row,
                          StampWindow   *self)
{
  stamp_launch_goa ();
}

static void
on_account_changed (GObject      *object,
                    StampAccount *account,
                    gpointer      user_data)
{
  StampWindow *self = STAMP_WINDOW (user_data);
  StampSession *session = STAMP_SESSION (object);
  GList *accounts = stamp_session_get_accounts (session);

  if (!accounts) {
    adw_view_stack_set_visible_child_name (self->app_view_stack, "welcome");
  } else {
    adw_view_stack_set_visible_child_name (self->app_view_stack, "main");
  }
}

static void
stamp_window_dispose (GObject *object)
{
  StampWindow *self = STAMP_WINDOW (object);
  StampSession *session = stamp_session_get_default ();
  const char *view;

  view = adw_view_stack_get_visible_child_name (self->main_view_stack);
  if (view)
    g_settings_set (STAMP_SETTINGS, STAMP_PREFS_VIEW, "s", view);

  g_signal_handlers_disconnect_by_data (session, self);

  if (self->mail_view)
    g_signal_handlers_disconnect_by_data (self->mail_view, self);

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
  StampSession *session = stamp_session_get_default ();
  g_autofree char *view = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (session, "account-added", G_CALLBACK (on_account_changed), self, 0);
  g_signal_connect_object (session, "account-removed", G_CALLBACK (on_account_changed), self, 0);

  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_BACKGROUND_NOTIFICATIONS, self, "hide-on-close", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_WINDOW_WIDTH, self, "default-width", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_WINDOW_HEIGHT, self, "default-height", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_WINDOW_MAXIMIZED, self, "maximized", G_SETTINGS_BIND_DEFAULT);
  g_settings_get (STAMP_SETTINGS, STAMP_PREFS_VIEW, "s", &view);

  if (view)
    adw_view_stack_set_visible_child_name (self->main_view_stack, view);
}

void
stamp_window_search_contact (StampWindow *self,
                             const char  *mail)
{
  stamp_mail_view_search_contact (self->mail_view, mail);
}

void
stamp_window_show_contact (StampWindow *self,
                           const char  *mail)
{
  stamp_contact_view_show_contact (self->contact_view, mail);
  adw_view_stack_set_visible_child_name (self->main_view_stack, "contacts");
}

StampMailView *
stamp_window_get_mail_view (StampWindow *self)
{
  return self->mail_view;
}

/* FIXME: Wrong window selection */
StampWindow *
stamp_get_main_window (void)
{
  GApplication *app = g_application_get_default ();

  return STAMP_WINDOW (gtk_application_get_active_window (GTK_APPLICATION (app)));
}

void
stamp_window_show_mail_view (StampWindow *self)
{
  adw_view_stack_set_visible_child_name (self->main_view_stack, "mail");
}
