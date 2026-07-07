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

#include "stamp-preferences-account.h"

#include <camel/camel.h>
#include <glib/gi18n.h>

#include "stamp-account.h"
#include "stamp-helper.h"
#include "stamp-settings.h"

struct _StampPreferencesAccount {
  AdwNavigationPage parent_instance;

  AdwWindowTitle *window_title;
  AdwPreferencesGroup *alias_group;
  AdwComboRow *notification_mode;
  AdwPreferencesGroup *notification_folders_group;

  StampAccount *account;
  GHashTable *aliases;
  GPtrArray *alias_rows;

  GSettings *account_settings;
  GPtrArray *folder_rows;
  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (StampPreferencesAccount, stamp_preferences_account, ADW_TYPE_NAVIGATION_PAGE);

typedef enum {
  PROP_ACCOUNT = 1,
} StampPreferencesAccountProps;

static GParamSpec *props[PROP_ACCOUNT + 1] = { NULL, };

static void on_alias_edit_clicked (GtkWidget *button,
                                   gpointer   user_data);
static void on_alias_activated (AdwActionRow *row,
                                gpointer      user_data);

static void refresh_alias_list (StampPreferencesAccount *self);
static void setup_notifications (StampPreferencesAccount *self);

static void
set_account (StampPreferencesAccount *self,
             StampAccount            *account)
{
  g_set_object (&self->account, account);
  adw_window_title_set_title (self->window_title, stamp_account_get_name (self->account));

  refresh_alias_list (self);
  setup_notifications (self);
}

static void
refresh_alias_list (StampPreferencesAccount *self)
{
  StampMailService *service;
  GList *mails;

  if (self->alias_rows) {
    guint i;

    for (i = 0; i < self->alias_rows->len; i++)
      adw_preferences_group_remove (self->alias_group, GTK_WIDGET (g_ptr_array_index (self->alias_rows, i)));

    g_ptr_array_set_size (self->alias_rows, 0);
  } else {
    self->alias_rows = g_ptr_array_new ();
  }

  service = stamp_account_get_mail_service (self->account);
  if (service)
    self->aliases = stamp_mail_service_get_aliases (service);

  if (!self->aliases)
    return;

  mails = g_hash_table_get_keys (self->aliases);

  for (GList *iter = mails; iter && iter->data; iter = g_list_next (iter)) {
    gchar *mail = iter->data;
    gchar *name = g_hash_table_lookup (self->aliases, mail);
    GtkWidget *row;
    GtkWidget *edit_button;

    row = adw_action_row_new ();
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), mail);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), name);
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);

    g_object_set_data_full (G_OBJECT (row), "email", g_strdup (mail), g_free);
    g_object_set_data_full (G_OBJECT (row), "name", g_strdup (name), g_free);
    g_signal_connect_object (row, "activated", G_CALLBACK (on_alias_activated), self, 0);

    edit_button = gtk_button_new ();
    gtk_button_set_icon_name (GTK_BUTTON (edit_button), "document-edit-symbolic");
    gtk_widget_add_css_class (edit_button, "flat");
    gtk_widget_set_valign (edit_button, GTK_ALIGN_CENTER);
    g_object_set_data (G_OBJECT (edit_button), "row", row);
    g_signal_connect (edit_button, "clicked", G_CALLBACK (on_alias_edit_clicked), self);
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), edit_button);

    adw_preferences_group_add (self->alias_group, row);
    g_ptr_array_add (self->alias_rows, row);
  }

  g_list_free (mails);
}

static void
on_alias_edit_clicked (GtkWidget *button,
                       gpointer   user_data)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (user_data);
  AdwActionRow *row = ADW_ACTION_ROW (g_object_get_data (G_OBJECT (button), "row"));
  const gchar *mail = adw_preferences_row_get_title (ADW_PREFERENCES_ROW (row));
  const gchar *name = adw_action_row_get_subtitle (row);
  AdwNavigationPage *page;
  GtkWidget *parent;

  parent = gtk_widget_get_parent (GTK_WIDGET (self));
  g_assert (ADW_IS_NAVIGATION_VIEW (parent));

  page = ADW_NAVIGATION_PAGE (stamp_preferences_account_editor_new (self->account, mail, name));
  adw_navigation_view_push (ADW_NAVIGATION_VIEW (parent), page);
}

static void
on_alias_activated (AdwActionRow *row,
                    gpointer      user_data)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (user_data);
  const gchar *mail = g_object_get_data (G_OBJECT (row), "email");
  const gchar *name = g_object_get_data (G_OBJECT (row), "name");
  AdwNavigationPage *page;
  GtkWidget *parent;

  parent = gtk_widget_get_parent (GTK_WIDGET (self));
  g_assert (ADW_IS_NAVIGATION_VIEW (parent));

  page = ADW_NAVIGATION_PAGE (stamp_preferences_account_editor_new (self->account, mail, name));
  adw_navigation_view_push (ADW_NAVIGATION_VIEW (parent), page);
}

static void
on_add_alias_clicked (GtkWidget *button,
                      gpointer   user_data)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (user_data);
  AdwNavigationPage *page;
  GtkWidget *parent;

  parent = gtk_widget_get_parent (GTK_WIDGET (self));
  g_assert (ADW_IS_NAVIGATION_VIEW (parent));

  page = ADW_NAVIGATION_PAGE (stamp_preferences_account_editor_new (self->account, NULL, NULL));
  adw_navigation_view_push (ADW_NAVIGATION_VIEW (parent), page);
}

static void
on_notification_folder_toggled (GtkWidget *widget,
                                gpointer   user_data)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (user_data);
  const gchar *full_name = g_object_get_data (G_OBJECT (widget), "folder-full-name");
  gboolean active = adw_switch_row_get_active (ADW_SWITCH_ROW (widget));
  gchar **folders = g_settings_get_strv (self->account_settings, "notification-folders");
  gchar **new_folders;

  if (active)
    new_folders = g_strv_append ((const gchar * const *)folders, full_name);
  else
    new_folders = g_strv_remove ((const gchar * const *)folders, full_name);

  g_settings_set_strv (self->account_settings, "notification-folders", (const char * const *)new_folders);
  g_strfreev (new_folders);
}

static void
add_folder_recursive (StampPreferencesAccount *self,
                      CamelFolderInfo         *info)
{
  if (!info)
    return;

  if (!(info->flags & CAMEL_FOLDER_NOSELECT)) {
    g_auto (GStrv) folders = g_settings_get_strv (self->account_settings, "notification-folders");
    GtkWidget *row = adw_switch_row_new ();
    gboolean active = g_strv_contains ((const char **)folders, info->full_name);

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), info->display_name ? info->display_name : info->full_name);
    adw_switch_row_set_active (ADW_SWITCH_ROW (row), active);

    g_object_set_data_full (G_OBJECT (row), "folder-full-name", g_strdup (info->full_name), g_free);
    g_signal_connect (row, "notify::active", G_CALLBACK (on_notification_folder_toggled), self);

    adw_preferences_group_add (self->notification_folders_group, row);
    g_ptr_array_add (self->folder_rows, row);
  }

  add_folder_recursive (self, info->child);
}

static void
on_get_folder_info (GObject      *source,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (user_data);
  CamelStore *store = CAMEL_STORE (source);
  g_autoptr (CamelFolderInfo) folder_info = NULL;
  g_autoptr (GError) error = NULL;

  folder_info = camel_store_get_folder_info_finish (store, res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("%s: Error loading folders: %s", G_STRFUNC, error->message);
    return;
  }

  for (CamelFolderInfo *fi = folder_info; fi; fi = fi->next)
    add_folder_recursive (self, fi);
}

static void
load_folders (StampPreferencesAccount *self)
{
  StampMailService *mail_service = stamp_account_get_mail_service (self->account);
  CamelService *service;

  if (!mail_service)
    return;

  service = stamp_mail_service_get_service (mail_service);
  if (!service)
    return;

  camel_store_get_folder_info (CAMEL_STORE (service),
                               NULL,
                               CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST,
                               G_PRIORITY_DEFAULT,
                               self->cancellable,
                               on_get_folder_info,
                               self);
}

static void
on_notification_mode_changed (GObject    *gobject,
                              GParamSpec *pspec,
                              gpointer    user_data)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (user_data);
  guint selected = adw_combo_row_get_selected (self->notification_mode);
  const gchar *mode;

  switch (selected) {
    case 1:
      mode = "all";
      break;
    case 2:
      mode = "custom";
      break;
    default:
      mode = "inbox";
      break;
  }

  g_settings_set_string (self->account_settings, "notification-mode", mode);

  if (selected == 2) {
    gtk_widget_set_visible (GTK_WIDGET (self->notification_folders_group), TRUE);

    if (self->folder_rows->len == 0)
      load_folders (self);
  } else {
    gtk_widget_set_visible (GTK_WIDGET (self->notification_folders_group), FALSE);
  }
}

static void
setup_notifications (StampPreferencesAccount *self)
{
  g_autofree char *settings_path = NULL;
  g_autoptr (GtkStringList) model = NULL;
  const gchar *mode;
  guint selected;

  settings_path = g_strconcat ("/org/tabos/stamp/mail/accounts/", stamp_account_get_name (self->account), "/", NULL);
  self->account_settings = g_settings_new_with_path ("org.tabos.stamp.mail.accounts", settings_path);

  model = gtk_string_list_new ((const char *[]){_("Inbox"), _("All"), _("Custom"), NULL});
  adw_combo_row_set_model (self->notification_mode, G_LIST_MODEL (model));

  mode = g_settings_get_string (self->account_settings, "notification-mode");
  if (g_strcmp0 (mode, "all") == 0)
    selected = 1;
  else if (g_strcmp0 (mode, "custom") == 0)
    selected = 2;
  else
    selected = 0;

  adw_combo_row_set_selected (self->notification_mode, selected);
  g_free ((gchar *)mode);

  gtk_widget_set_visible (GTK_WIDGET (self->notification_folders_group), selected == 2);

  g_signal_connect_object (self->notification_mode, "notify::selected", G_CALLBACK (on_notification_mode_changed), self, 0);

  if (selected == 2)
    load_folders (self);
}

static void
stamp_preferences_account_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (object);

  switch ((StampPreferencesAccountProps)property_id) {
    case PROP_ACCOUNT:
      g_value_set_object (value, self->account);
      break;
  }
}

static void
stamp_preferences_account_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (object);

  switch ((StampPreferencesAccountProps)property_id) {
    case PROP_ACCOUNT:
      set_account (self, g_value_get_object (value));
      break;
  }
}

static void
stamp_preferences_account_dispose (GObject *object)
{
  StampPreferencesAccount *self = STAMP_PREFERENCES_ACCOUNT (object);

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->account);
  g_clear_object (&self->account_settings);

  if (self->folder_rows)
    g_ptr_array_unref (self->folder_rows);

  G_OBJECT_CLASS (stamp_preferences_account_parent_class)->dispose (object);
}

void
stamp_preferences_account_class_init (StampPreferencesAccountClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = stamp_preferences_account_dispose;
  gobject_class->get_property = stamp_preferences_account_get_property;
  gobject_class->set_property = stamp_preferences_account_set_property;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/stamp-preferences-account.ui");

  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccount, window_title);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccount, alias_group);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccount, notification_mode);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccount, notification_folders_group);

  gtk_widget_class_bind_template_callback (widget_class, on_add_alias_clicked);

  props[PROP_ACCOUNT] = g_param_spec_object ("account",
                                             NULL,
                                             NULL,
                                             STAMP_TYPE_ACCOUNT,
                                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, G_N_ELEMENTS (props), props);
}

void
stamp_preferences_account_init (StampPreferencesAccount *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();
  self->folder_rows = g_ptr_array_new ();

  g_signal_connect_swapped (self, "shown", G_CALLBACK (refresh_alias_list), self);
}

GtkWidget *
stamp_preferences_account_new (StampAccount *account)
{
  return g_object_new (STAMP_TYPE_PREFERENCES_ACCOUNT, "account", account, NULL);
}

struct _StampPreferencesAccountEditor {
  AdwNavigationPage parent_instance;

  AdwWindowTitle *editor_window_title;
  AdwEntryRow *mail_row;
  AdwEntryRow *name_row;
  AdwButtonRow *save;
  AdwButtonRow *remove;

  StampAccount *account;
  gchar *email;
  gchar *name;
  GHashTable *aliases;
};

G_DEFINE_FINAL_TYPE (StampPreferencesAccountEditor, stamp_preferences_account_editor, ADW_TYPE_NAVIGATION_PAGE);

static void
on_save_clicked (GtkWidget *button,
                 gpointer   user_data)
{
  StampPreferencesAccountEditor *self = STAMP_PREFERENCES_ACCOUNT_EDITOR (user_data);
  const gchar *mail = gtk_editable_get_text (GTK_EDITABLE (self->mail_row));
  const gchar *name = gtk_editable_get_text (GTK_EDITABLE (self->name_row));
  GtkWidget *parent;

  if (g_strcmp0 (mail, "") != 0) {
    StampMailService *service;

    if (self->email && g_strcmp0 (self->email, mail) != 0) {
      g_hash_table_remove (self->aliases, self->email);
    }

    g_hash_table_insert (self->aliases, g_strdup (mail), g_strdup (name));

    service = stamp_account_get_mail_service (self->account);
    stamp_mail_service_set_aliases (service, self->aliases);
  }

  parent = gtk_widget_get_parent (GTK_WIDGET (self));
  adw_navigation_view_pop (ADW_NAVIGATION_VIEW (parent));
}

static void
on_remove_clicked (GtkWidget *button,
                   gpointer   user_data)
{
  StampPreferencesAccountEditor *self = STAMP_PREFERENCES_ACCOUNT_EDITOR (user_data);
  GtkWidget *parent;

  if (self->email) {
    StampMailService *service;

    g_hash_table_remove (self->aliases, self->email);

    service = stamp_account_get_mail_service (self->account);
    stamp_mail_service_set_aliases (service, self->aliases);
  }

  parent = gtk_widget_get_parent (GTK_WIDGET (self));
  adw_navigation_view_pop (ADW_NAVIGATION_VIEW (parent));
}

static void
on_entry_changed (GtkWidget *button,
                  gpointer   user_data)
{
  StampPreferencesAccountEditor *self = STAMP_PREFERENCES_ACCOUNT_EDITOR (user_data);
  const char *mail = gtk_editable_get_text (GTK_EDITABLE (self->mail_row));

  gtk_widget_set_sensitive (GTK_WIDGET (self->save), strlen (mail) != 0);
}

static void
stamp_preferences_account_editor_get_property (GObject    *object,
                                               guint       property_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
}

static void
stamp_preferences_account_editor_set_property (GObject      *object,
                                               guint         property_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
}

static void
stamp_preferences_account_editor_dispose (GObject *object)
{
  StampPreferencesAccountEditor *self = STAMP_PREFERENCES_ACCOUNT_EDITOR (object);

  g_clear_object (&self->account);
  g_clear_pointer (&self->email, g_free);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (stamp_preferences_account_editor_parent_class)->dispose (object);
}

void
stamp_preferences_account_editor_class_init (StampPreferencesAccountEditorClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = stamp_preferences_account_editor_dispose;
  gobject_class->get_property = stamp_preferences_account_editor_get_property;
  gobject_class->set_property = stamp_preferences_account_editor_set_property;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/stamp-preferences-account-editor.ui");

  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccountEditor, editor_window_title);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccountEditor, mail_row);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccountEditor, name_row);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccountEditor, save);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesAccountEditor, remove);

  gtk_widget_class_bind_template_callback (widget_class, on_save_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_remove_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_entry_changed);
}

void
stamp_preferences_account_editor_init (StampPreferencesAccountEditor *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
stamp_preferences_account_editor_new (StampAccount *account,
                                      const gchar  *email,
                                      const gchar  *name)
{
  StampPreferencesAccountEditor *self;
  StampMailService *service;

  self = g_object_new (STAMP_TYPE_PREFERENCES_ACCOUNT_EDITOR, NULL);

  self->account = g_object_ref (account);
  self->email = g_strdup (email);
  self->name = g_strdup (name);

  if (email) {
    adw_window_title_set_title (self->editor_window_title, _("Edit Alias"));
  } else {
    adw_window_title_set_title (self->editor_window_title, _("New Alias"));
  }

  service = stamp_account_get_mail_service (account);
  self->aliases = stamp_mail_service_get_aliases (service);
  if (!self->aliases)
    self->aliases = g_hash_table_new_full (camel_strcase_hash, camel_strcase_equal, g_free, g_free);

  gtk_editable_set_text (GTK_EDITABLE (self->mail_row), email ? email : "");
  gtk_editable_set_text (GTK_EDITABLE (self->name_row), name ? name : "");

  gtk_widget_set_visible (GTK_WIDGET (self->remove), email != NULL);
  gtk_widget_set_visible (GTK_WIDGET (self->save), !email && !name);

  return GTK_WIDGET (self);
}
