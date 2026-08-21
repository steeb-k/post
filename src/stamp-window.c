/*
 * Copyright 2024-2026 Jan-Michael Brummer
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

#include "stamp-config.h"

#include "stamp-window.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libportal-gtk4/portal-gtk4.h>

#include "stamp-accounts.h"
#include "stamp-account.h"
#include "stamp-calendar-view.h"
#include "stamp-contact-view.h"
#include "stamp-helper.h"
#include "stamp-inbox-counter.h"
#include "stamp-mail-view.h"
#include "stamp-profile-manager.h"
#include "stamp-session.h"
#include "stamp-settings.h"
#include "stamp-today-counter.h"

struct _StampWindow {
  AdwApplicationWindow parent_instance;

  AdwViewStack *app_view_stack;
  AdwViewStack *main_view_stack;
  StampMailView *mail_view;
  StampContactView *contact_view;
  StampCalendarView *calendar_view;
  AdwViewStackPage *mail_stack_page;
  AdwViewStackPage *calendar_stack_page;
  StampTodayCounter *today_counter;
  StampInboxCounter *inbox_counter;

  GtkSizeGroup *sidebar_size_group;
  GtkSizeGroup *action_size_group;

  GSimpleAction *layout_action;

  /* A layout picked by hand while a profile was imposing one of its
   * own, which holds until that profile's turn is over. */
  gchar *manual_layout;
  gchar *active_profile_id;

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
  stamp_accounts_present (GTK_WIDGET (self));
}

static void
stamp_window_update_view (StampWindow *self)
{
  GList *accounts = stamp_session_get_accounts (stamp_session_get_default ());

  if (!accounts) {
    adw_view_stack_set_visible_child_name (self->app_view_stack, "welcome");
  } else {
    adw_view_stack_set_visible_child_name (self->app_view_stack, "main");
  }
}

static void
on_account_changed (GObject      *object,
                    StampAccount *account,
                    gpointer      user_data)
{
  stamp_window_update_view (STAMP_WINDOW (user_data));
}

static void
on_show_mail (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
  StampWindow *self = STAMP_WINDOW (user_data);

  adw_view_stack_set_visible_child_name (self->main_view_stack, "mail");
}

static void
on_show_contacts (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
  StampWindow *self = STAMP_WINDOW (user_data);

  adw_view_stack_set_visible_child_name (self->main_view_stack, "contacts");
}

static void
on_show_calendar (GSimpleAction *action G_GNUC_UNUSED,
                  GVariant *parameter   G_GNUC_UNUSED,
                  gpointer              user_data)
{
  StampWindow *self = STAMP_WINDOW (user_data);

  adw_view_stack_set_visible_child_name (self->main_view_stack, "calendar");
}

/*
 * The layout the active profile asks for, or NULL when it asks for
 * nothing -- which is most of them, since a profile only overrides the
 * layout if it was told to.
 */
static const gchar *
profile_layout (void)
{
  StampProfile *active = stamp_profile_manager_get_active (stamp_profile_manager_get_default ());

  return active ? stamp_profile_get_layout (active) : NULL;
}

/*
 * What the mail view should be wearing right now: a layout picked by
 * hand during a profile's turn, else whatever the profile asks for,
 * else the default from the preferences.
 */
static void
stamp_window_update_layout (StampWindow *self)
{
  g_autofree gchar *configured = NULL;
  const gchar *nick = self->manual_layout;

  if (!nick)
    nick = profile_layout ();

  if (!nick) {
    configured = g_settings_get_string (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_LAYOUT);
    nick = configured;
  }

  stamp_mail_view_set_layout (self->mail_view, stamp_mail_layout_from_nick (nick));

  if (self->layout_action)
    g_simple_action_set_state (self->layout_action, g_variant_new_string (nick));
}

/*
 * Picking a layout by hand while a profile is imposing one only lasts
 * as long as that profile does; with nothing to override, it is simply
 * the new default.
 */
static void
on_change_layout (GSimpleAction *action,
                  GVariant      *value,
                  gpointer       user_data)
{
  StampWindow *self = STAMP_WINDOW (user_data);
  const gchar *nick = g_variant_get_string (value, NULL);

  if (!stamp_mail_layout_nick_is_valid (nick))
    return;

  if (profile_layout ()) {
    g_set_str (&self->manual_layout, nick);
  } else {
    g_clear_pointer (&self->manual_layout, g_free);
    g_settings_set_string (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_LAYOUT, nick);
  }

  stamp_window_update_layout (self);
}

/*
 * The counters keep counting whether or not anyone is looking, so
 * turning a badge off is a matter of not showing what it knows. Zero is
 * how AdwViewStackPage spells no badge at all.
 */
static void
stamp_window_update_badges (StampWindow *self)
{
  adw_view_stack_page_set_badge_number (self->mail_stack_page,
                                        stamp_mail_badge_enabled () ? stamp_inbox_counter_get_count (self->inbox_counter) : 0);
  adw_view_stack_page_set_badge_number (self->calendar_stack_page,
                                        stamp_calendar_badge_enabled () ? stamp_today_counter_get_count (self->today_counter) : 0);
}

/*
 * A profile whose turn has ended takes its layout with it, and hands
 * the hand-picked one back to the default too.
 */
static void
on_profile_changed (StampProfileManager *manager,
                    gpointer             user_data)
{
  StampWindow *self = STAMP_WINDOW (user_data);
  StampProfile *active = stamp_profile_manager_get_active (manager);
  const gchar *id = active ? stamp_profile_get_id (active) : NULL;

  /* The manager also announces edits to the profile that is already
   * active, which should not throw away a deliberate choice. */
  if (g_strcmp0 (self->active_profile_id, id) != 0) {
    g_set_str (&self->active_profile_id, id);
    g_clear_pointer (&self->manual_layout, g_free);
  }

  stamp_window_update_layout (self);
  stamp_window_update_badges (self);
}

static void
on_layout_setting_changed (GSettings   *settings,
                           const gchar *key,
                           gpointer     user_data)
{
  stamp_window_update_layout (STAMP_WINDOW (user_data));
}

static const GActionEntry stamp_window_action_entries[] = {
  { .name = "show-mail", .activate = on_show_mail },
  { .name = "show-contacts", .activate = on_show_contacts },
  { .name = "show-calendar", .activate = on_show_calendar },
  { .name = "mail-layout", .parameter_type = "s", .state = "'side-by-side'", .change_state = on_change_layout },
};

static void
stamp_window_dispose (GObject *object)
{
  StampWindow *self = STAMP_WINDOW (object);
  StampSession *session = stamp_session_get_default ();
  const gchar *view;

  view = adw_view_stack_get_visible_child_name (self->main_view_stack);
  if (view)
    g_settings_set (STAMP_SETTINGS, STAMP_PREFS_VIEW, "s", view);

  g_signal_handlers_disconnect_by_data (session, self);

  if (self->mail_view)
    g_signal_handlers_disconnect_by_data (self->mail_view, self);

  g_clear_object (&self->sidebar_size_group);
  g_clear_object (&self->action_size_group);
  g_clear_object (&self->today_counter);
  g_clear_object (&self->inbox_counter);
  g_clear_pointer (&self->manual_layout, g_free);
  g_clear_pointer (&self->active_profile_id, g_free);

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_WINDOW);

  G_OBJECT_CLASS (stamp_window_parent_class)->dispose (object);
}

static void
stamp_window_class_init (StampWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_window_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/stamp-window.ui");

  gtk_widget_class_bind_template_child (widget_class, StampWindow, app_view_stack);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, main_view_stack);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, mail_view);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, contact_view);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, calendar_view);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, mail_stack_page);
  gtk_widget_class_bind_template_child (widget_class, StampWindow, calendar_stack_page);

  gtk_widget_class_bind_template_callback (widget_class, on_open_settings_clicked);
}

static const struct {
  const gchar *layout;
  const gchar *shortcut;
} LayoutShortcuts[] = {
  { "side-by-side", "<primary><alt>1" },
  { "stacked", "<primary><alt>2" },
  { "dense", "<primary><alt>3" },
};

static void
on_background_status (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  XdpPortal *portal = XDP_PORTAL (source_object);
  g_autoptr (GError) error = NULL;

  if (!xdp_portal_set_background_status_finish (portal, res, &error)) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("%s: Could not set background status: %s", G_STRFUNC, error->message);
    return;
  }
}

static gboolean
stamp_is_running_inside_flatpak (void)
{
  return g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS);
}

static void
stamp_window_init (StampWindow *self)
{
  StampSession *session = stamp_session_get_default ();
  g_autofree char *view = NULL;
  XdpPortal *portal = xdp_portal_new ();
  g_autoptr (XdpParent) parent_window = xdp_parent_new_gtk (GTK_WINDOW (self));
  GtkEventController *controller;

  gtk_widget_init_template (GTK_WIDGET (self));

  /* Keep the sidebars the same width in all three views, so the bottom
   * view switcher does not move around when switching between them. The
   * calendar's date chooser has the widest minimum and sets the pace. */
  self->sidebar_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
  gtk_size_group_add_widget (self->sidebar_size_group, stamp_mail_view_get_sidebar (self->mail_view));
  gtk_size_group_add_widget (self->sidebar_size_group, stamp_contact_view_get_sidebar (self->contact_view));
  gtk_size_group_add_widget (self->sidebar_size_group, stamp_calendar_view_get_sidebar (self->calendar_view));

  /* And the same for what each view makes: Compose, Event and Contact
   * are different lengths, so without this the pill grows and shrinks
   * under the pointer as you move between views. The widest label sets
   * the width for all three. */
  self->action_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
  gtk_size_group_add_widget (self->action_size_group, stamp_mail_view_get_primary_action (self->mail_view));
  gtk_size_group_add_widget (self->action_size_group, stamp_contact_view_get_primary_action (self->contact_view));
  gtk_size_group_add_widget (self->action_size_group, stamp_calendar_view_get_primary_action (self->calendar_view));

  /* Badge the Mail button with the unread mail this profile can see,
   * and the Calendar button with how much is happening today. */
  self->inbox_counter = stamp_inbox_counter_new ();
  self->today_counter = stamp_today_counter_new ();

  g_signal_connect_object (self->inbox_counter, "notify::count",
                           G_CALLBACK (stamp_window_update_badges), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->today_counter, "notify::count",
                           G_CALLBACK (stamp_window_update_badges), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (STAMP_SETTINGS_MAIL, "changed::" STAMP_PREFS_MAIL_SHOW_BADGE,
                           G_CALLBACK (stamp_window_update_badges), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (STAMP_SETTINGS_CALENDAR, "changed::" STAMP_PREFS_CALENDAR_SHOW_BADGE,
                           G_CALLBACK (stamp_window_update_badges), self, G_CONNECT_SWAPPED);

  stamp_window_update_badges (self);

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   stamp_window_action_entries,
                                   G_N_ELEMENTS (stamp_window_action_entries),
                                   self);

  self->layout_action = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "mail-layout"));

  /* Blueprint has no way to spell a shortcut that carries a target, so
   * the layout accelerators are built here. */
  controller = gtk_shortcut_controller_new ();
  gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (controller), GTK_SHORTCUT_SCOPE_GLOBAL);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  for (guint i = 0; i < G_N_ELEMENTS (LayoutShortcuts); i++) {
    GtkShortcut *shortcut;

    shortcut = gtk_shortcut_new (gtk_shortcut_trigger_parse_string (LayoutShortcuts[i].shortcut),
                                 gtk_named_action_new ("win.mail-layout"));
    gtk_shortcut_set_arguments (shortcut, g_variant_new_string (LayoutShortcuts[i].layout));
    gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);
  }

  g_signal_connect_object (stamp_profile_manager_get_default (), "changed",
                           G_CALLBACK (on_profile_changed), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (STAMP_SETTINGS_MAIL, "changed::" STAMP_PREFS_MAIL_LAYOUT,
                           G_CALLBACK (on_layout_setting_changed), self, G_CONNECT_DEFAULT);

  {
    StampProfile *active = stamp_profile_manager_get_active (stamp_profile_manager_get_default ());

    self->active_profile_id = g_strdup (active ? stamp_profile_get_id (active) : NULL);
  }

  stamp_window_update_layout (self);

  g_signal_connect_object (session, "account-added", G_CALLBACK (on_account_changed), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (session, "account-removed", G_CALLBACK (on_account_changed), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (session, "accounts-loaded", G_CALLBACK (stamp_window_update_view), self, G_CONNECT_SWAPPED);

  /* The session may have finished loading before this window existed. */
  if (stamp_session_get_accounts_loaded (session))
    stamp_window_update_view (self);

  if (stamp_is_running_inside_flatpak () && g_settings_get_boolean (STAMP_SETTINGS, STAMP_PREFS_BACKGROUND_NOTIFICATIONS))
    xdp_portal_set_background_status (portal, _("Waiting for new emails"), NULL, on_background_status, self);

  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_BACKGROUND_NOTIFICATIONS, self, "hide-on-close", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_WINDOW_WIDTH, self, "default-width", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_WINDOW_HEIGHT, self, "default-height", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (STAMP_SETTINGS, STAMP_PREFS_WINDOW_MAXIMIZED, self, "maximized", G_SETTINGS_BIND_DEFAULT);
  g_settings_get (STAMP_SETTINGS, STAMP_PREFS_VIEW, "s", &view);

  if (view)
    adw_view_stack_set_visible_child_name (self->main_view_stack, view);

#ifdef DEVEL
  gtk_widget_add_css_class (GTK_WIDGET (self), "devel");
#endif
}

void
stamp_window_search_contact (StampWindow *self,
                             const gchar *mail)
{
  stamp_mail_view_search_contact (self->mail_view, mail);
}

void
stamp_window_show_contact (StampWindow *self,
                           const gchar *mail)
{
  stamp_contact_view_show_contact (self->contact_view, mail);
  adw_view_stack_set_visible_child_name (self->main_view_stack, "contacts");
}

StampMailView *
stamp_window_get_mail_view (StampWindow *self)
{
  return self->mail_view;
}

StampWindow *
stamp_get_main_window (void)
{
  GtkApplication *app = GTK_APPLICATION (g_application_get_default ());
  GList *windows = gtk_application_get_windows (app);

  for (GList *win = windows; win && win->data; win = g_list_next (win)) {
    if (STAMP_IS_WINDOW (win->data))
      return STAMP_WINDOW (win->data);
  }

  return NULL;
}

void
stamp_window_show_mail_view (StampWindow *self)
{
  adw_view_stack_set_visible_child_name (self->main_view_stack, "mail");
}

/**
 * stamp_window_create_event:
 * @self: a #StampWindow
 * @summary: (nullable): the event title
 * @description: (nullable): the event description
 *
 * Brings the calendar view up and opens its event editor on a new
 * event prefilled with @summary and @description.
 */
void
stamp_window_create_event (StampWindow *self,
                           const gchar *summary,
                           const gchar *description)
{
  g_return_if_fail (STAMP_IS_WINDOW (self));

  adw_view_stack_set_visible_child_name (self->main_view_stack, "calendar");
  stamp_calendar_view_create_event (self->calendar_view, summary, description);
}

/**
 * stamp_window_show_event:
 * @self: a #StampWindow
 * @event: the #GcalEvent to show
 *
 * Brings the calendar view up on @event's day and opens its editor.
 */
void
stamp_window_show_event (StampWindow *self,
                         GcalEvent   *event)
{
  g_return_if_fail (STAMP_IS_WINDOW (self));
  g_return_if_fail (GCAL_IS_EVENT (event));

  adw_view_stack_set_visible_child_name (self->main_view_stack, "calendar");
  stamp_calendar_view_show_event (self->calendar_view, event);
}
