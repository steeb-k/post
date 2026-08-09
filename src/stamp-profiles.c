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
 * Where profiles are made: a list page, and an editor page per profile.
 *
 * Edits apply as they are made rather than behind a save button, so the
 * switcher and the sidebars show the result immediately. That means the
 * editor must not rebuild itself when the manager announces a change --
 * it caused the change, and rebuilding would pull the entry out from
 * under the cursor. Only the list page listens.
 */

#include "stamp-profiles.h"
#include "views/stamp-badge-picker.h"

#include <glib/gi18n.h>

#include "stamp-account.h"
#include "stamp-profile-button.h"
#include "stamp-profile-manager.h"
#include "stamp-session.h"
#include "stamp-settings.h"

#define STAMP_TYPE_PROFILES_EDITOR (stamp_profiles_editor_get_type ())

G_DECLARE_FINAL_TYPE (StampProfilesEditor, stamp_profiles_editor, STAMP, PROFILES_EDITOR, AdwNavigationPage);

struct _StampProfilesEditor {
  AdwNavigationPage parent_instance;

  AdwWindowTitle *editor_title;
  AdwAvatar *preview;
  AdwAvatar *badge_preview;
  AdwEntryRow *name_row;
  AdwComboRow *layout_row;
  GtkBox *swatches;
  AdwPreferencesGroup *accounts_group;
  AdwPreferencesGroup *rules_group;
  AdwButtonRow *remove_row;

  /* Borrowed: the manager's store owns it. */
  StampProfile *profile;

  GPtrArray *account_rows;
  GPtrArray *rule_rows;
  GPtrArray *swatch_buttons;

  guint updating : 1;
};

G_DEFINE_FINAL_TYPE (StampProfilesEditor, stamp_profiles_editor, ADW_TYPE_NAVIGATION_PAGE);

struct _StampProfiles {
  AdwDialog parent_instance;

  AdwNavigationView *navigation_view;
  AdwPreferencesGroup *profiles_group;
  AdwComboRow *default_row;

  StampProfileManager *manager;
  GPtrArray *profile_rows;

  guint updating : 1;
};

G_DEFINE_FINAL_TYPE (StampProfiles, stamp_profiles, ADW_TYPE_DIALOG);

static void stamp_profiles_editor_rebuild_rules (StampProfilesEditor *self);

/*
 * Editor: color swatches
 */

static void
update_swatches (StampProfilesEditor *self)
{
  const gchar *chosen = stamp_profile_get_color (self->profile);

  for (guint i = 0; i < self->swatch_buttons->len; i++) {
    GtkWidget *button = g_ptr_array_index (self->swatch_buttons, i);
    const gchar *id = g_object_get_data (G_OBJECT (button), "color-id");
    GtkWidget *check = gtk_button_get_child (GTK_BUTTON (button));
    gboolean selected = g_strcmp0 (id, chosen) == 0;

    gtk_widget_set_opacity (check, selected ? 1.0 : 0.0);
    gtk_accessible_update_state (GTK_ACCESSIBLE (button),
                                 GTK_ACCESSIBLE_STATE_SELECTED, selected,
                                 -1);
  }
}

static void
on_swatch_clicked (GtkButton *button,
                   gpointer   user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);
  const gchar *id = g_object_get_data (G_OBJECT (button), "color-id");

  stamp_profile_set_color (self->profile, id);

  update_swatches (self);
  stamp_profile_button_style_avatar (self->preview, self->profile);
  stamp_profile_button_style_avatar (self->badge_preview, self->profile);

  stamp_profile_manager_save (stamp_profile_manager_get_default ());
}

static void
build_swatches (StampProfilesEditor *self)
{
  guint n_colors = 0;
  const StampProfileColor *palette = stamp_profile_get_palette (&n_colors);

  for (guint i = 0; i < n_colors; i++) {
    GtkWidget *button = gtk_button_new ();
    GtkWidget *check = gtk_image_new_from_icon_name ("object-select-symbolic");
    g_autofree gchar *css_class = g_strconcat ("profile-", palette[i].id, NULL);

    gtk_button_set_child (GTK_BUTTON (button), check);
    gtk_widget_add_css_class (button, "profile-swatch");
    gtk_widget_add_css_class (button, css_class);
    /* The palette names are marked with N_() where they are declared. */
    gtk_widget_set_tooltip_text (button, gettext (palette[i].name));
    gtk_accessible_update_property (GTK_ACCESSIBLE (button),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL,
                                    gettext (palette[i].name),
                                    -1);

    g_object_set_data (G_OBJECT (button), "color-id", (gpointer)palette[i].id);
    g_signal_connect (button, "clicked", G_CALLBACK (on_swatch_clicked), self);

    gtk_box_append (self->swatches, button);
    g_ptr_array_add (self->swatch_buttons, button);
  }
}

/*
 * Editor: accounts
 */

static void
on_account_toggled (GObject    *row,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);
  const gchar *uid = g_object_get_data (row, "account-uid");

  if (self->updating)
    return;

  if (adw_switch_row_get_active (ADW_SWITCH_ROW (row)))
    stamp_profile_add_account (self->profile, uid);
  else
    stamp_profile_remove_account (self->profile, uid);

  stamp_profile_manager_save (stamp_profile_manager_get_default ());
}

static void
build_accounts (StampProfilesEditor *self)
{
  GList *accounts = stamp_session_get_accounts (stamp_session_get_default ());

  self->updating = TRUE;

  for (GList *iter = accounts; iter; iter = g_list_next (iter)) {
    StampAccount *account = STAMP_ACCOUNT (iter->data);
    const gchar *uid = stamp_account_get_uid (account);
    GtkWidget *row = adw_switch_row_new ();

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), stamp_account_get_name (account));
    adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
    adw_switch_row_set_active (ADW_SWITCH_ROW (row), stamp_profile_has_account (self->profile, uid));

    g_object_set_data_full (G_OBJECT (row), "account-uid", g_strdup (uid), g_free);
    g_signal_connect (row, "notify::active", G_CALLBACK (on_account_toggled), self);

    adw_preferences_group_add (self->accounts_group, row);
    g_ptr_array_add (self->account_rows, row);
  }

  if (!accounts) {
    GtkWidget *row = adw_action_row_new ();

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _("No accounts yet"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), _("Add an account first, then choose which profiles show it."));
    adw_preferences_group_add (self->accounts_group, row);
    g_ptr_array_add (self->account_rows, row);
  }

  self->updating = FALSE;
}

/*
 * Editor: schedule rules
 */

typedef struct {
  StampProfilesEditor *editor;
  guint index;
  AdwExpanderRow *row;
  GtkToggleButton *days[7];
  GtkSpinButton *start_hour;
  GtkSpinButton *start_minute;
  GtkSpinButton *end_hour;
  GtkSpinButton *end_minute;
} RuleRow;

static void
rule_row_free (gpointer data)
{
  g_free (data);
}

static void
rule_row_update_title (RuleRow *rule_row)
{
  StampProfile *profile = rule_row->editor->profile;
  g_autoptr (StampProfile) scratch = stamp_profile_new (NULL, "", NULL);
  StampProfileRule rule;
  g_autofree gchar *summary = NULL;

  if (!stamp_profile_get_rule (profile, rule_row->index, &rule))
    return;

  /* Borrow the profile's own formatting for a single rule by lending it
   * to a throwaway profile that holds only that rule. */
  stamp_profile_add_rule (scratch, &rule);
  summary = stamp_profile_dup_schedule_summary (scratch);

  if (rule.days == 0) {
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (rule_row->row), _("No days selected"));
    adw_expander_row_set_subtitle (rule_row->row, _("This rule never applies."));
  } else {
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (rule_row->row), summary);
    adw_expander_row_set_subtitle (rule_row->row,
                                   rule.end_minute > rule.start_minute ? NULL : _("Continues past midnight"));
  }
}

static void
rule_row_apply (RuleRow *rule_row)
{
  StampProfileRule rule = { 0, 0, 0 };

  if (rule_row->editor->updating)
    return;

  for (gint day = 0; day < 7; day++) {
    if (gtk_toggle_button_get_active (rule_row->days[day]))
      rule.days |= 1 << day;
  }

  rule.start_minute = gtk_spin_button_get_value_as_int (rule_row->start_hour) * 60 +
                      gtk_spin_button_get_value_as_int (rule_row->start_minute);
  rule.end_minute = gtk_spin_button_get_value_as_int (rule_row->end_hour) * 60 +
                    gtk_spin_button_get_value_as_int (rule_row->end_minute);

  stamp_profile_set_rule (rule_row->editor->profile, rule_row->index, &rule);
  rule_row_update_title (rule_row);

  stamp_profile_manager_save (stamp_profile_manager_get_default ());
}

static void
on_rule_day_toggled (GtkToggleButton *button,
                     gpointer         user_data)
{
  rule_row_apply (user_data);
}

static void
on_rule_time_changed (GtkSpinButton *spin,
                      gpointer       user_data)
{
  rule_row_apply (user_data);
}

static void
on_rule_remove_clicked (AdwButtonRow *row,
                        gpointer      user_data)
{
  RuleRow *rule_row = user_data;
  StampProfilesEditor *editor = rule_row->editor;

  stamp_profile_remove_rule (editor->profile, rule_row->index);
  stamp_profile_manager_save (stamp_profile_manager_get_default ());

  /* Removing shifts every later rule's index, so start the rows over
   * rather than trying to patch them up. */
  stamp_profiles_editor_rebuild_rules (editor);
}

/*
 * An hour:minute pair. Wrapping spin buttons make dragging past
 * midnight or the top of the hour behave the way a clock would.
 */
static GtkWidget *
build_time_box (RuleRow        *rule_row,
                guint           minute_of_day,
                GtkSpinButton **out_hour,
                GtkSpinButton **out_minute)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
  GtkWidget *hour = gtk_spin_button_new_with_range (0, 23, 1);
  GtkWidget *separator = gtk_label_new (":");
  GtkWidget *minute = gtk_spin_button_new_with_range (0, 59, 5);

  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);

  gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (hour), TRUE);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (hour), minute_of_day / 60);
  gtk_accessible_update_property (GTK_ACCESSIBLE (hour), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Hour"), -1);

  gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (minute), TRUE);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (minute), minute_of_day % 60);
  gtk_accessible_update_property (GTK_ACCESSIBLE (minute), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Minute"), -1);

  gtk_box_append (GTK_BOX (box), hour);
  gtk_box_append (GTK_BOX (box), separator);
  gtk_box_append (GTK_BOX (box), minute);

  g_signal_connect (hour, "value-changed", G_CALLBACK (on_rule_time_changed), rule_row);
  g_signal_connect (minute, "value-changed", G_CALLBACK (on_rule_time_changed), rule_row);

  *out_hour = GTK_SPIN_BUTTON (hour);
  *out_minute = GTK_SPIN_BUTTON (minute);

  return box;
}

static GtkWidget *
build_rule_row (StampProfilesEditor    *self,
                guint                   index,
                const StampProfileRule *rule)
{
  RuleRow *rule_row = g_new0 (RuleRow, 1);
  GtkWidget *row = adw_expander_row_new ();
  GtkWidget *days_row = adw_action_row_new ();
  GtkWidget *days_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *start_row = adw_action_row_new ();
  GtkWidget *end_row = adw_action_row_new ();
  GtkWidget *remove_row = adw_button_row_new ();
  GtkWidget *start_box;
  GtkWidget *end_box;

  rule_row->editor = self;
  rule_row->index = index;
  rule_row->row = ADW_EXPANDER_ROW (row);

  /* Days */
  gtk_widget_add_css_class (days_box, "linked");
  gtk_widget_set_valign (days_box, GTK_ALIGN_CENTER);

  for (gint day = 0; day < 7; day++) {
    GtkWidget *toggle = gtk_toggle_button_new_with_label (stamp_profile_get_weekday_label (day));

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), (rule->days & (1 << day)) != 0);
    g_signal_connect (toggle, "toggled", G_CALLBACK (on_rule_day_toggled), rule_row);

    gtk_box_append (GTK_BOX (days_box), toggle);
    rule_row->days[day] = GTK_TOGGLE_BUTTON (toggle);
  }

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (days_row), _("Days"));
  adw_action_row_add_suffix (ADW_ACTION_ROW (days_row), days_box);
  adw_expander_row_add_row (ADW_EXPANDER_ROW (row), days_row);

  /* Times */
  start_box = build_time_box (rule_row, rule->start_minute, &rule_row->start_hour, &rule_row->start_minute);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (start_row), _("From"));
  adw_action_row_add_suffix (ADW_ACTION_ROW (start_row), start_box);
  adw_expander_row_add_row (ADW_EXPANDER_ROW (row), start_row);

  end_box = build_time_box (rule_row, rule->end_minute, &rule_row->end_hour, &rule_row->end_minute);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (end_row), _("To"));
  adw_action_row_add_suffix (ADW_ACTION_ROW (end_row), end_box);
  adw_expander_row_add_row (ADW_EXPANDER_ROW (row), end_row);

  /* Remove */
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (remove_row), _("Remove Rule"));
  gtk_widget_add_css_class (remove_row, "destructive-action");
  g_signal_connect (remove_row, "activated", G_CALLBACK (on_rule_remove_clicked), rule_row);
  adw_expander_row_add_row (ADW_EXPANDER_ROW (row), remove_row);

  g_object_set_data_full (G_OBJECT (row), "rule-row", rule_row, rule_row_free);
  g_ptr_array_add (self->rule_rows, row);

  rule_row_update_title (rule_row);

  return row;
}

static void
stamp_profiles_editor_rebuild_rules (StampProfilesEditor *self)
{
  guint n_rules = stamp_profile_get_n_rules (self->profile);

  for (guint i = 0; i < self->rule_rows->len; i++)
    adw_preferences_group_remove (self->rules_group, g_ptr_array_index (self->rule_rows, i));

  g_ptr_array_set_size (self->rule_rows, 0);

  self->updating = TRUE;

  for (guint i = 0; i < n_rules; i++) {
    StampProfileRule rule;

    if (stamp_profile_get_rule (self->profile, i, &rule))
      adw_preferences_group_add (self->rules_group, build_rule_row (self, i, &rule));
  }

  self->updating = FALSE;

  if (n_rules == 0) {
    GtkWidget *row = adw_action_row_new ();

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _("No schedule"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), _("This profile only turns on when you pick it."));
    adw_preferences_group_add (self->rules_group, row);
    g_ptr_array_add (self->rule_rows, row);
  }
}

static void
on_add_rule_clicked (GtkButton *button,
                     gpointer   user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);
  /* A working week is what people reach for first. */
  StampProfileRule rule = { STAMP_PROFILE_WEEKDAYS, 9 * 60, 18 * 60 };

  stamp_profile_add_rule (self->profile, &rule);
  stamp_profile_manager_save (stamp_profile_manager_get_default ());

  stamp_profiles_editor_rebuild_rules (self);
}

/*
 * Editor: name and removal
 */

static void
stamp_profiles_editor_style_avatars (StampProfilesEditor *self)
{
  stamp_profile_button_style_avatar (self->preview, self->profile);
  stamp_profile_button_style_avatar (self->badge_preview, self->profile);
}

static void
on_badge_selected (StampBadgePicker *picker G_GNUC_UNUSED,
                   const gchar      *badge_id,
                   gpointer          user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);

  stamp_profile_set_badge (self->profile, badge_id);
  stamp_profiles_editor_style_avatars (self);

  stamp_profile_manager_save (stamp_profile_manager_get_default ());
}

static void
on_badge_row_activated (AdwActionRow *row G_GNUC_UNUSED,
                        gpointer      user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);
  GtkWidget *picker = stamp_badge_picker_new (self->profile);

  g_signal_connect (picker, "selected", G_CALLBACK (on_badge_selected), self);

  adw_dialog_present (ADW_DIALOG (picker), GTK_WIDGET (self));
}

static void
on_name_changed (GtkEditable *editable,
                 gpointer     user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);

  if (self->updating)
    return;

  stamp_profile_set_name (self->profile, gtk_editable_get_text (editable));
  stamp_profiles_editor_style_avatars (self);
  adw_window_title_set_title (self->editor_title, stamp_profile_get_name (self->profile));

  stamp_profile_manager_save (stamp_profile_manager_get_default ());
}

/* The first entry is "Default", so the layouts start one along. */
static void
on_layout_changed (GObject    *row,
                   GParamSpec *pspec G_GNUC_UNUSED,
                   gpointer    user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);
  guint selected = adw_combo_row_get_selected (ADW_COMBO_ROW (row));

  if (self->updating)
    return;

  if (selected == 0)
    stamp_profile_set_layout (self->profile, NULL);
  else
    stamp_profile_set_layout (self->profile, stamp_mail_layout_to_nick ((StampMailLayout)(selected - 1)));

  stamp_profile_manager_save (stamp_profile_manager_get_default ());
}

static void
on_remove_response (AdwAlertDialog *dialog,
                    gchar          *response,
                    gpointer        user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);
  GtkWidget *parent;

  if (g_strcmp0 (response, "remove") != 0)
    return;

  parent = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);

  stamp_profile_manager_remove_profile (stamp_profile_manager_get_default (), self->profile);
  self->profile = NULL;

  if (parent)
    adw_navigation_view_pop (ADW_NAVIGATION_VIEW (parent));
}

static void
on_remove_clicked (AdwButtonRow *row,
                   gpointer      user_data)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (user_data);
  AdwDialog *dialog;

  dialog = adw_alert_dialog_new (_("Remove Profile?"), NULL);
  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                                /* Translators: %s is a profile name. */
                                _("“%s” will be removed. The accounts it showed are not touched."),
                                stamp_profile_get_name (self->profile));
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", _("_Cancel"),
                                  "remove", _("_Remove"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "remove", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  g_signal_connect (dialog, "response", G_CALLBACK (on_remove_response), self);

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

/*
 * Editor: GObject
 */

static void
stamp_profiles_editor_dispose (GObject *object)
{
  StampProfilesEditor *self = STAMP_PROFILES_EDITOR (object);

  g_clear_pointer (&self->account_rows, g_ptr_array_unref);
  g_clear_pointer (&self->rule_rows, g_ptr_array_unref);
  g_clear_pointer (&self->swatch_buttons, g_ptr_array_unref);

  self->profile = NULL;

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_PROFILES_EDITOR);

  G_OBJECT_CLASS (stamp_profiles_editor_parent_class)->dispose (object);
}

static void
stamp_profiles_editor_class_init (StampProfilesEditorClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_profiles_editor_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/stamp-profiles-editor.ui");

  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, editor_title);
  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, preview);
  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, badge_preview);
  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, name_row);
  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, layout_row);
  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, swatches);
  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, accounts_group);
  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, rules_group);
  gtk_widget_class_bind_template_child (widget_class, StampProfilesEditor, remove_row);

  gtk_widget_class_bind_template_callback (widget_class, on_badge_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_name_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_layout_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_add_rule_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_remove_clicked);
}

static void
stamp_profiles_editor_init (StampProfilesEditor *self)
{
  self->account_rows = g_ptr_array_new ();
  self->rule_rows = g_ptr_array_new ();
  self->swatch_buttons = g_ptr_array_new ();

  gtk_widget_init_template (GTK_WIDGET (self));
}

static GtkWidget *
stamp_profiles_editor_new (StampProfile *profile)
{
  StampProfilesEditor *self = g_object_new (STAMP_TYPE_PROFILES_EDITOR, NULL);

  self->profile = profile;

  self->updating = TRUE;
  gtk_editable_set_text (GTK_EDITABLE (self->name_row), stamp_profile_get_name (profile));

  {
    const gchar *layout = stamp_profile_get_layout (profile);

    adw_combo_row_set_selected (self->layout_row,
                                layout ? stamp_mail_layout_from_nick (layout) + 1 : 0);
  }

  self->updating = FALSE;

  adw_window_title_set_title (self->editor_title, stamp_profile_get_name (profile));
  stamp_profiles_editor_style_avatars (self);

  build_swatches (self);
  update_swatches (self);
  build_accounts (self);
  stamp_profiles_editor_rebuild_rules (self);

  return GTK_WIDGET (self);
}

/*
 * Dialog
 */

static void
open_editor (StampProfiles *self,
             StampProfile  *profile)
{
  adw_navigation_view_push (self->navigation_view,
                            ADW_NAVIGATION_PAGE (stamp_profiles_editor_new (profile)));
}

static void
on_profile_row_activated (AdwActionRow *row,
                          gpointer      user_data)
{
  StampProfiles *self = STAMP_PROFILES (user_data);
  StampProfile *profile = g_object_get_data (G_OBJECT (row), "profile");

  open_editor (self, profile);
}

static void
on_add_clicked (GtkButton *button,
                gpointer   user_data)
{
  StampProfiles *self = STAMP_PROFILES (user_data);
  guint n_existing = g_list_model_get_n_items (stamp_profile_manager_get_profiles (self->manager));
  guint n_colors = 0;
  const StampProfileColor *palette = stamp_profile_get_palette (&n_colors);
  StampProfile *profile;

  /* Walk the palette so a second profile does not come up the same
   * color as the first. */
  profile = stamp_profile_manager_add_profile (self->manager,
                                               _("New Profile"),
                                               palette[n_existing % n_colors].id);

  open_editor (self, profile);
}

static void
rebuild_default_row (StampProfiles *self)
{
  GListModel *profiles = stamp_profile_manager_get_profiles (self->manager);
  guint n_profiles = g_list_model_get_n_items (profiles);
  g_autoptr (GtkStringList) names = gtk_string_list_new (NULL);
  StampProfile *current = stamp_profile_manager_get_default_profile (self->manager);
  guint selected = 0;

  gtk_string_list_append (names, _("Show All"));

  for (guint i = 0; i < n_profiles; i++) {
    g_autoptr (StampProfile) profile = g_list_model_get_item (profiles, i);

    gtk_string_list_append (names, stamp_profile_get_name (profile));

    if (profile == current)
      selected = i + 1;
  }

  self->updating = TRUE;
  adw_combo_row_set_model (self->default_row, G_LIST_MODEL (names));
  adw_combo_row_set_selected (self->default_row, selected);
  self->updating = FALSE;
}

static void
on_default_changed (GObject    *row,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
  StampProfiles *self = STAMP_PROFILES (user_data);
  guint selected = adw_combo_row_get_selected (self->default_row);
  g_autoptr (StampProfile) profile = NULL;

  if (self->updating || selected == GTK_INVALID_LIST_POSITION)
    return;

  /* Row zero is "Show All", so the profiles start one along. */
  if (selected > 0)
    profile = g_list_model_get_item (stamp_profile_manager_get_profiles (self->manager), selected - 1);

  stamp_profile_manager_set_default_profile (self->manager, profile);
}

static void
rebuild_list (StampProfiles *self)
{
  GListModel *profiles = stamp_profile_manager_get_profiles (self->manager);
  guint n_profiles = g_list_model_get_n_items (profiles);

  for (guint i = 0; i < self->profile_rows->len; i++)
    adw_preferences_group_remove (self->profiles_group, g_ptr_array_index (self->profile_rows, i));

  g_ptr_array_set_size (self->profile_rows, 0);

  for (guint i = 0; i < n_profiles; i++) {
    g_autoptr (StampProfile) profile = g_list_model_get_item (profiles, i);
    GtkWidget *row = adw_action_row_new ();
    GtkWidget *avatar = adw_avatar_new (32, NULL, FALSE);
    GtkWidget *chevron = gtk_image_new_from_icon_name ("go-next-symbolic");
    g_autofree gchar *schedule = stamp_profile_dup_schedule_summary (profile);
    guint n_accounts = g_strv_length ((GStrv)stamp_profile_get_accounts (profile));
    g_autofree gchar *subtitle = NULL;

    if (schedule) {
      subtitle = g_strdup_printf (ngettext ("%u account · %s", "%u accounts · %s", n_accounts),
                                  n_accounts, schedule);
    } else {
      subtitle = g_strdup_printf (ngettext ("%u account", "%u accounts", n_accounts), n_accounts);
    }

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), stamp_profile_get_name (profile));
    adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);

    stamp_profile_button_style_avatar (ADW_AVATAR (avatar), profile);
    adw_action_row_add_prefix (ADW_ACTION_ROW (row), avatar);
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), chevron);
    adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), NULL);
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);

    g_object_set_data (G_OBJECT (row), "profile", profile);
    g_signal_connect (row, "activated", G_CALLBACK (on_profile_row_activated), self);

    adw_preferences_group_add (self->profiles_group, row);
    g_ptr_array_add (self->profile_rows, row);
  }

  if (n_profiles == 0) {
    GtkWidget *row = adw_action_row_new ();

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _("No profiles yet"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), _("Add one to start filtering what you see."));
    adw_preferences_group_add (self->profiles_group, row);
    g_ptr_array_add (self->profile_rows, row);
  }

  rebuild_default_row (self);
}

static void
on_profiles_changed (StampProfileManager *manager,
                     gpointer             user_data)
{
  rebuild_list (STAMP_PROFILES (user_data));
}

static void
stamp_profiles_dispose (GObject *object)
{
  StampProfiles *self = STAMP_PROFILES (object);

  if (self->manager)
    g_signal_handlers_disconnect_by_data (self->manager, self);

  self->manager = NULL;

  g_clear_pointer (&self->profile_rows, g_ptr_array_unref);

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_PROFILES);

  G_OBJECT_CLASS (stamp_profiles_parent_class)->dispose (object);
}

static void
stamp_profiles_class_init (StampProfilesClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_profiles_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/stamp-profiles.ui");

  gtk_widget_class_bind_template_child (widget_class, StampProfiles, navigation_view);
  gtk_widget_class_bind_template_child (widget_class, StampProfiles, profiles_group);
  gtk_widget_class_bind_template_child (widget_class, StampProfiles, default_row);

  gtk_widget_class_bind_template_callback (widget_class, on_add_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_default_changed);
}

static void
stamp_profiles_init (StampProfiles *self)
{
  self->profile_rows = g_ptr_array_new ();

  gtk_widget_init_template (GTK_WIDGET (self));

  self->manager = stamp_profile_manager_get_default ();

  g_signal_connect_object (self->manager, "profiles-changed", G_CALLBACK (on_profiles_changed), self, G_CONNECT_DEFAULT);

  rebuild_list (self);
}

AdwDialog *
stamp_profiles_new (void)
{
  return g_object_new (STAMP_TYPE_PROFILES, NULL);
}

/**
 * stamp_profiles_present:
 * @parent: the widget to present the dialog from
 *
 * Opens the profile management dialog.
 */
void
stamp_profiles_present (GtkWidget *parent)
{
  g_return_if_fail (GTK_IS_WIDGET (parent));

  adw_dialog_present (stamp_profiles_new (), parent);
}
