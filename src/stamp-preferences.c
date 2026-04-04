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

#include "stamp-preferences.h"

#include <glib/gi18n.h>
#include <libportal-gtk4/portal-gtk4.h>

#include "stamp-account.h"
#include "stamp-preferences-signatures.h"
#include "stamp-session.h"
#include "stamp-settings.h"
#include "stamp-webview.h"

struct _StampPreferences {
  AdwPreferencesDialog parent_instance;

  AdwSwitchRow *background_notifications;
  AdwSwitchRow *autostart;
  AdwSwitchRow *always_show_images;
  AdwSwitchRow *bimi_images;
  AdwSpinRow *mark_read;
  AdwSwitchRow *play_incoming_sound;
  AdwSpinRow *refresh_interval;
  AdwSwitchRow *important_first;

  gboolean autostart_failed;

  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (StampPreferences, stamp_preferences, ADW_TYPE_PREFERENCES_DIALOG)

static void
on_signature_row_activated (GtkWidget *button,
                            gpointer   user_data)
{
  StampPreferences *self = STAMP_PREFERENCES (user_data);
  AdwNavigationPage *page = g_object_new (STAMP_TYPE_PREFERENCES_SIGNATURES, NULL);

  adw_preferences_dialog_push_subpage (ADW_PREFERENCES_DIALOG (self), page);
}

static void
stamp_preferences_dispose (GObject *object)
{
  StampPreferences *self = STAMP_PREFERENCES (object);

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (stamp_preferences_parent_class)->dispose (object);
}

void
stamp_preferences_class_init (StampPreferencesClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = stamp_preferences_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/stamp-preferences.ui");

  gtk_widget_class_bind_template_child (widget_class, StampPreferences, background_notifications);
  gtk_widget_class_bind_template_child (widget_class, StampPreferences, autostart);
  gtk_widget_class_bind_template_child (widget_class, StampPreferences, always_show_images);
  gtk_widget_class_bind_template_child (widget_class, StampPreferences, bimi_images);
  gtk_widget_class_bind_template_child (widget_class, StampPreferences, mark_read);
  gtk_widget_class_bind_template_child (widget_class, StampPreferences, play_incoming_sound);
  gtk_widget_class_bind_template_child (widget_class, StampPreferences, refresh_interval);
  gtk_widget_class_bind_template_child (widget_class, StampPreferences, important_first);

  gtk_widget_class_bind_template_callback (widget_class, on_signature_row_activated);
}

static void
on_request_background (GObject      *source_object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  XdpPortal *portal = XDP_PORTAL (source_object);
  g_autoptr (GError) error = NULL;

  if (!xdp_portal_request_background_finish (portal, res, &error)) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("%s: Could not request background: %s", G_STRFUNC, error->message);
    return;
  }
}

static void
on_autostart (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
  StampPreferences *self = STAMP_PREFERENCES (user_data);
  g_autoptr (XdpPortal) portal = xdp_portal_new ();
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ()));
  g_autoptr (XdpParent) parent_window = xdp_parent_new_gtk (window);
  g_autoptr (GPtrArray) commandline = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (commandline, g_strdup ("stamp"));
  g_ptr_array_add (commandline, g_strdup ("--hidden"));

  xdp_portal_request_background (portal, parent_window, _("Notifications"), commandline, XDP_BACKGROUND_FLAG_AUTOSTART, self->cancellable, on_request_background, self);
}

static void
on_bimi_images (GtkWidget  *row,
                GParamSpec *pspec,
                gpointer    user_data)
{
  StampPreferences *self = STAMP_PREFERENCES (user_data);
  gboolean enabled = adw_switch_row_get_active (ADW_SWITCH_ROW (row));
  g_print ("%s: changed to %d\n", G_STRFUNC, enabled);

  if (enabled) {
    StampSession *session = stamp_session_get_default ();
    GList *accounts = stamp_session_get_accounts (session);

    /* Remove negative photo cache */
    for (GList *iter = accounts; iter && iter->data; iter = g_list_next (iter)) {
      StampAccount *account = STAMP_ACCOUNT (iter->data);

      stamp_account_clear_negative_photo_cache (account);
    }

  }
}

void
stamp_preferences_init (StampPreferences *self)
{
  g_type_ensure (STAMP_TYPE_WEBVIEW);

  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();

  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_BACKGROUND_NOTIFICATIONS, self->background_notifications, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_BACKGROUND_AUTOSTART, self->autostart, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_ALWAYS_SHOW_IMAGES, self->always_show_images, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_PLAY_INCOMING_SOUND, self->play_incoming_sound, "active", G_SETTINGS_BIND_DEFAULT);
  g_signal_connect_object (self->autostart, "notify::active", G_CALLBACK (on_autostart), self, 0);
  g_settings_bind (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_LOAD_BIMI_IMAGES, self->bimi_images, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_IMPORTANT_FIRST, self->important_first, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_MARK_READ_TIMEOUT, self->mark_read, "value", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_REFRESH_INTERVAL, self->refresh_interval, "value", G_SETTINGS_BIND_DEFAULT);

  g_signal_connect_object (self->bimi_images, "notify::active", G_CALLBACK (on_bimi_images), self, 0);
}

GtkWidget *
stamp_preferences_new (void)
{
  return g_object_new (STAMP_TYPE_PREFERENCES, NULL);
}
