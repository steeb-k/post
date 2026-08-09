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
 * The profile switcher: a colored badge at the top of the sidebar, and
 * a popover to pick from. It lives in the shared header bar, so all
 * three views get one.
 *
 * The popover is rebuilt from the model rather than kept in sync
 * row-by-row -- it holds at most a handful of rows and is only built
 * while it is being opened.
 */

#include "stamp-badge.h"
#include "stamp-profile-button.h"

#include <glib/gi18n.h>

#include "stamp-profile-manager.h"

struct _StampProfileButton {
  AdwBin parent_instance;

  GtkMenuButton *button;
  AdwAvatar *avatar;
  GtkPopover *popover;
  GtkBox *popover_box;

  StampProfileManager *manager;
};

G_DEFINE_FINAL_TYPE (StampProfileButton, stamp_profile_button, ADW_TYPE_BIN);

void
stamp_profile_button_style_avatar_parts (AdwAvatar   *avatar,
                                         const gchar *color,
                                         const gchar *initial,
                                         const gchar *badge_id)
{
  guint n_colors = 0;
  const StampProfileColor *palette = stamp_profile_get_palette (&n_colors);

  g_return_if_fail (ADW_IS_AVATAR (avatar));

  gtk_widget_add_css_class (GTK_WIDGET (avatar), "profile-avatar");

  gtk_widget_remove_css_class (GTK_WIDGET (avatar), "profile-all");
  for (guint i = 0; i < n_colors; i++) {
    g_autofree gchar *css_class = g_strconcat ("profile-", palette[i].id, NULL);

    gtk_widget_remove_css_class (GTK_WIDGET (avatar), css_class);
  }

  if (!color) {
    adw_avatar_set_custom_image (avatar, NULL);
    adw_avatar_set_show_initials (avatar, FALSE);
    adw_avatar_set_text (avatar, NULL);
    adw_avatar_set_icon_name (avatar, "view-grid-symbolic");
    gtk_widget_add_css_class (GTK_WIDGET (avatar), "profile-all");
    return;
  }

  {
    g_autofree gchar *css_class = g_strconcat ("profile-", color, NULL);

    stamp_badge_apply (avatar, stamp_badge_find (badge_id), initial);
    gtk_widget_add_css_class (GTK_WIDGET (avatar), css_class);
  }
}

void
stamp_profile_button_style_avatar (AdwAvatar    *avatar,
                                   StampProfile *profile)
{
  if (!profile) {
    stamp_profile_button_style_avatar_parts (avatar, NULL, NULL, NULL);
    return;
  }

  stamp_profile_button_style_avatar_parts (avatar,
                                           stamp_profile_get_color (profile),
                                           stamp_profile_get_initial (profile),
                                           stamp_profile_get_badge (profile));
}

static void
update_button (StampProfileButton *self)
{
  StampProfile *active = stamp_profile_manager_get_active (self->manager);
  g_autofree gchar *tooltip = NULL;

  stamp_profile_button_style_avatar (self->avatar, active);

  if (active) {
    if (stamp_profile_manager_get_overridden (self->manager)) {
      /* Translators: %s is a profile name. */
      tooltip = g_strdup_printf (_("Profile: %s (overriding the schedule)"), stamp_profile_get_name (active));
    } else {
      /* Translators: %s is a profile name. */
      tooltip = g_strdup_printf (_("Profile: %s"), stamp_profile_get_name (active));
    }
  } else {
    tooltip = g_strdup (_("Showing all accounts"));
  }

  gtk_widget_set_tooltip_text (GTK_WIDGET (self->button), tooltip);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self->button),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL, tooltip,
                                  -1);
}

/*
 * Popover
 */

static void
on_row_activated (GtkListBox    *box,
                  GtkListBoxRow *row,
                  gpointer       user_data)
{
  StampProfileButton *self = STAMP_PROFILE_BUTTON (user_data);
  StampProfile *profile = g_object_get_data (G_OBJECT (row), "profile");

  gtk_popover_popdown (self->popover);

  stamp_profile_manager_set_active (self->manager, profile);
}

static void
on_resume_clicked (GtkButton *button,
                   gpointer   user_data)
{
  StampProfileButton *self = STAMP_PROFILE_BUTTON (user_data);

  gtk_popover_popdown (self->popover);

  stamp_profile_manager_resume_schedule (self->manager);
}

static void
on_edit_clicked (GtkButton *button,
                 gpointer   user_data)
{
  StampProfileButton *self = STAMP_PROFILE_BUTTON (user_data);

  gtk_popover_popdown (self->popover);

  gtk_widget_activate_action (GTK_WIDGET (self), "app.profiles", NULL);
}

static GtkWidget *
build_row (StampProfileButton *self,
           StampProfile       *profile,
           StampProfile       *active)
{
  GtkWidget *row = adw_action_row_new ();
  GtkWidget *avatar = adw_avatar_new (28, NULL, FALSE);
  g_autofree gchar *schedule = profile ? stamp_profile_dup_schedule_summary (profile) : NULL;

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row),
                                 profile ? stamp_profile_get_name (profile) : _("Show All"));
  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);

  if (schedule)
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), schedule);
  else if (!profile)
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), _("Do not filter by profile"));

  stamp_profile_button_style_avatar (ADW_AVATAR (avatar), profile);
  adw_action_row_add_prefix (ADW_ACTION_ROW (row), avatar);

  if (profile == active) {
    GtkWidget *check = gtk_image_new_from_icon_name ("object-select-symbolic");

    adw_action_row_add_suffix (ADW_ACTION_ROW (row), check);
    gtk_accessible_update_state (GTK_ACCESSIBLE (row), GTK_ACCESSIBLE_STATE_SELECTED, TRUE, -1);
  }

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_object_set_data (G_OBJECT (row), "profile", profile);

  return row;
}

static void
rebuild_popover (StampProfileButton *self)
{
  GListModel *profiles = stamp_profile_manager_get_profiles (self->manager);
  StampProfile *active = stamp_profile_manager_get_active (self->manager);
  guint n_profiles = g_list_model_get_n_items (profiles);
  GtkWidget *list;
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self->popover_box))))
    gtk_box_remove (self->popover_box, child);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (list, "boxed-list");
  g_signal_connect (list, "row-activated", G_CALLBACK (on_row_activated), self);

  gtk_list_box_append (GTK_LIST_BOX (list), build_row (self, NULL, active));

  for (guint i = 0; i < n_profiles; i++) {
    g_autoptr (StampProfile) profile = g_list_model_get_item (profiles, i);

    gtk_list_box_append (GTK_LIST_BOX (list), build_row (self, profile, active));
  }

  gtk_box_append (self->popover_box, list);

  if (n_profiles == 0) {
    GtkWidget *hint = gtk_label_new (_("Profiles let you show only some of your accounts."));

    gtk_label_set_wrap (GTK_LABEL (hint), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (hint), 28);
    gtk_label_set_xalign (GTK_LABEL (hint), 0.0);
    gtk_widget_add_css_class (hint, "dim-label");
    gtk_widget_add_css_class (hint, "caption");
    gtk_widget_set_margin_top (hint, 6);
    gtk_box_append (self->popover_box, hint);
  }

  /* Only worth saying anything about the schedule when there is one. */
  if (stamp_profile_manager_has_schedule (self->manager)) {
    if (stamp_profile_manager_get_overridden (self->manager)) {
      GtkWidget *resume = gtk_button_new_with_label (_("Resume Schedule"));

      gtk_widget_add_css_class (resume, "flat");
      gtk_widget_set_margin_top (resume, 6);
      g_signal_connect (resume, "clicked", G_CALLBACK (on_resume_clicked), self);
      gtk_box_append (self->popover_box, resume);
    } else {
      GtkWidget *following = gtk_label_new (_("Following schedule"));

      gtk_widget_add_css_class (following, "dim-label");
      gtk_widget_add_css_class (following, "caption");
      gtk_widget_set_margin_top (following, 6);
      gtk_box_append (self->popover_box, following);
    }
  }

  {
    GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *edit = gtk_button_new_with_label (_("Edit Profiles…"));

    gtk_widget_set_margin_top (separator, 6);
    gtk_box_append (self->popover_box, separator);

    gtk_widget_add_css_class (edit, "flat");
    gtk_widget_set_margin_top (edit, 6);
    g_signal_connect (edit, "clicked", G_CALLBACK (on_edit_clicked), self);
    gtk_box_append (self->popover_box, edit);
  }
}

static void
on_popover_show (GtkPopover *popover,
                 gpointer    user_data)
{
  rebuild_popover (STAMP_PROFILE_BUTTON (user_data));
}

static void
on_manager_changed (StampProfileManager *manager,
                    gpointer             user_data)
{
  update_button (STAMP_PROFILE_BUTTON (user_data));
}

/*
 * GObject
 */

static void
stamp_profile_button_dispose (GObject *object)
{
  StampProfileButton *self = STAMP_PROFILE_BUTTON (object);

  if (self->manager)
    g_signal_handlers_disconnect_by_data (self->manager, self);

  self->manager = NULL;

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_PROFILE_BUTTON);

  G_OBJECT_CLASS (stamp_profile_button_parent_class)->dispose (object);
}

static void
stamp_profile_button_class_init (StampProfileButtonClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_profile_button_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/stamp-profile-button.ui");

  gtk_widget_class_bind_template_child (widget_class, StampProfileButton, button);
  gtk_widget_class_bind_template_child (widget_class, StampProfileButton, avatar);
  gtk_widget_class_bind_template_child (widget_class, StampProfileButton, popover);
  gtk_widget_class_bind_template_child (widget_class, StampProfileButton, popover_box);

  gtk_widget_class_bind_template_callback (widget_class, on_popover_show);
}

static void
stamp_profile_button_init (StampProfileButton *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->manager = stamp_profile_manager_get_default ();

  g_signal_connect_object (self->manager, "changed", G_CALLBACK (on_manager_changed), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (self->manager, "profiles-changed", G_CALLBACK (on_manager_changed), self, G_CONNECT_DEFAULT);

  update_button (self);
}

GtkWidget *
stamp_profile_button_new (void)
{
  return g_object_new (STAMP_TYPE_PROFILE_BUTTON, NULL);
}
