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

#include "stamp-account-editor.h"

#include <glib/gi18n.h>
#include <libedataserverui4/libedataserverui4.h>

#include "backend/stamp-session.h"

struct _StampAccountEditor {
  AdwDialog parent_instance;

  AdwNavigationView *navigation_view;
  AdwNavigationPage *form_page;
  AdwEntryRow *account_name_row;
  AdwEntryRow *name_row;
  AdwEntryRow *email_row;
  AdwEntryRow *reply_to_row;
  AdwEntryRow *imap_host_row;
  AdwSpinRow *imap_port_row;
  AdwComboRow *imap_security_row;
  AdwEntryRow *imap_user_row;
  AdwEntryRow *smtp_host_row;
  AdwSpinRow *smtp_port_row;
  AdwComboRow *smtp_security_row;
  AdwEntryRow *smtp_user_row;
  AdwButtonRow *save_row;
  AdwEntryRow *oauth_email_row;
  AdwEntryRow *carddav_url_row;
  AdwEntryRow *carddav_user_row;
  GtkBox *carddav_content_box;
  AdwComboRow *carddav_account_row;
  AdwButtonRow *carddav_add_row;

  GtkWidget *carddav_content;
  ECredentialsPrompter *carddav_prompter;

  StampAccount *account;

  const gchar *oauth_backend;
  const gchar *oauth_method;
};

G_DEFINE_FINAL_TYPE (StampAccountEditor, stamp_account_editor, ADW_TYPE_DIALOG);

/* Keep in sync with the encryption combo rows in the blueprint. */
static const gchar *security_methods[] = {
  "none",
  "starttls-on-standard-port",
  "ssl-on-alternate-port",
};

static guint
security_method_to_index (const gchar *method)
{
  for (guint i = 0; i < G_N_ELEMENTS (security_methods); i++) {
    if (g_strcmp0 (method, security_methods[i]) == 0)
      return i;
  }

  return 1;
}

static const gchar *
entry_text (AdwEntryRow *row)
{
  return gtk_editable_get_text (GTK_EDITABLE (row));
}

static const gchar *
entry_text_fallback (AdwEntryRow *row,
                     const gchar *fallback)
{
  const gchar *text = entry_text (row);

  return text && *text ? text : fallback;
}

static void
show_error (StampAccountEditor *self,
            const gchar        *heading,
            const gchar        *message)
{
  AdwDialog *dialog = adw_alert_dialog_new (heading, message);

  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", _("_Close"));
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "close");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "close");

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
on_type_imap_activated (AdwActionRow       *row,
                        StampAccountEditor *self)
{
  adw_navigation_view_push_by_tag (self->navigation_view, "form");
}

static void
on_type_google_activated (AdwActionRow       *row,
                          StampAccountEditor *self)
{
  self->oauth_backend = "google";
  self->oauth_method = "Google";
  adw_navigation_view_push_by_tag (self->navigation_view, "oauth");
}

static void
on_type_microsoft_activated (AdwActionRow       *row,
                             StampAccountEditor *self)
{
  self->oauth_backend = "outlook";
  self->oauth_method = "Outlook";
  adw_navigation_view_push_by_tag (self->navigation_view, "oauth");
}

static void
on_type_carddav_activated (AdwActionRow       *row,
                           StampAccountEditor *self)
{
  g_autoptr (GtkStringList) model = gtk_string_list_new (NULL);
  GList *accounts = stamp_session_get_accounts (stamp_session_get_default ());

  for (GList *iter = accounts; iter && iter->data; iter = g_list_next (iter))
    gtk_string_list_append (model, stamp_account_get_name (STAMP_ACCOUNT (iter->data)));

  adw_combo_row_set_model (self->carddav_account_row, G_LIST_MODEL (model));

  adw_navigation_view_push_by_tag (self->navigation_view, "carddav");
}

static void
on_carddav_refresh_done (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr (StampAccountEditor) self = STAMP_ACCOUNT_EDITOR (user_data);
  g_autoptr (GError) error = NULL;

  if (!e_webdav_discover_content_refresh_finish (self->carddav_content, result, &error)) {
    e_webdav_discover_content_show_error (self->carddav_content, error);
    return;
  }

  gtk_widget_set_sensitive (GTK_WIDGET (self->carddav_add_row), TRUE);
}

static void
on_carddav_search_clicked (AdwButtonRow       *row,
                           StampAccountEditor *self)
{
  StampSession *session = stamp_session_get_default ();
  g_autoptr (ESource) scratch = NULL;
  g_autoptr (GError) error = NULL;
  ESourceAuthentication *auth_ext;
  const gchar *url = entry_text (self->carddav_url_row);

  if (!*url) {
    show_error (self, _("Missing Information"), _("A server URL is required."));
    return;
  }

  scratch = e_source_new (NULL, NULL, &error);
  if (!scratch) {
    show_error (self, _("Could Not Search"), error ? error->message : "");
    return;
  }

  auth_ext = e_source_get_extension (scratch, E_SOURCE_EXTENSION_AUTHENTICATION);
  e_source_authentication_set_user (auth_ext, entry_text (self->carddav_user_row));

  if (self->carddav_content)
    gtk_box_remove (self->carddav_content_box, self->carddav_content);
  g_clear_object (&self->carddav_prompter);

  self->carddav_prompter = e_credentials_prompter_new (stamp_session_get_registry (session));
  self->carddav_content = e_webdav_discover_content_new (self->carddav_prompter, scratch, url,
                                                         E_WEBDAV_DISCOVER_SUPPORTS_CONTACTS);
  gtk_box_append (self->carddav_content_box, self->carddav_content);

  gtk_widget_set_sensitive (GTK_WIDGET (self->carddav_add_row), FALSE);
  e_webdav_discover_content_refresh (self->carddav_content, _("Address Book"), NULL,
                                     on_carddav_refresh_done, g_object_ref (self));
}

static void
on_carddav_add_clicked (AdwButtonRow       *row,
                        StampAccountEditor *self)
{
  StampSession *session = stamp_session_get_default ();
  StampAccount *account;
  ESource *collection;
  g_autoptr (ESource) book = NULL;
  g_autoptr (GUri) uri = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *href = NULL;
  g_autofree gchar *display_name = NULL;
  g_autofree gchar *color = NULL;
  guint supports = 0;
  guint order = 0;
  ESourceWebdav *webdav_ext;
  ESourceAuthentication *auth_ext;

  if (!self->carddav_content ||
      !e_webdav_discover_content_get_selected (self->carddav_content, 0, &href, &supports, &display_name, &color, &order)) {
    show_error (self, _("Missing Information"), _("Select an address book to add."));
    return;
  }

  account = g_list_nth_data (stamp_session_get_accounts (session),
                             adw_combo_row_get_selected (self->carddav_account_row));
  collection = account ? stamp_account_get_collection (account) : NULL;
  if (!collection) {
    show_error (self, _("Missing Information"), _("Select the account to add the address book to."));
    return;
  }

  uri = g_uri_parse (href, G_URI_FLAGS_NONE, &error);
  if (!uri) {
    show_error (self, _("Could Not Add Address Book"), error ? error->message : "");
    return;
  }

  book = e_source_new (NULL, NULL, &error);
  if (!book) {
    show_error (self, _("Could Not Add Address Book"), error ? error->message : "");
    return;
  }

  e_source_set_parent (book, e_source_get_uid (collection));
  e_source_set_display_name (book, display_name && *display_name ? display_name : _("Address Book"));
  e_source_backend_set_backend_name (E_SOURCE_BACKEND (e_source_get_extension (book, E_SOURCE_EXTENSION_ADDRESS_BOOK)), "carddav");
  webdav_ext = e_source_get_extension (book, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
  e_source_webdav_set_uri (webdav_ext, uri);
  auth_ext = e_source_get_extension (book, E_SOURCE_EXTENSION_AUTHENTICATION);
  e_source_authentication_set_user (auth_ext, entry_text (self->carddav_user_row));

  if (!e_source_registry_commit_source_sync (stamp_session_get_registry (session), book, NULL, &error)) {
    g_warning ("%s: Could not add address book: %s", G_STRFUNC, error ? error->message : "");
    show_error (self, _("Could Not Add Address Book"), error ? error->message : "");
    return;
  }

  adw_dialog_close (ADW_DIALOG (self));
}

static void
on_oauth_save_clicked (AdwButtonRow       *row,
                       StampAccountEditor *self)
{
  StampSession *session = stamp_session_get_default ();
  ESourceRegistry *registry = stamp_session_get_registry (session);
  g_autoptr (ESource) collection = NULL;
  g_autoptr (GError) error = NULL;
  ESourceCollection *collection_ext;
  ESourceAuthentication *auth_ext;
  const gchar *address = entry_text (self->oauth_email_row);

  if (!*address) {
    show_error (self, _("Missing Information"), _("An email address is required."));
    return;
  }

  collection = e_source_new (NULL, NULL, &error);
  if (!collection) {
    show_error (self, _("Could Not Add Account"), error ? error->message : "");
    return;
  }

  /* The matching collection backend in the registry creates the mail,
   * calendar and contacts children on its own. */
  e_source_set_display_name (collection, address);
  collection_ext = e_source_get_extension (collection, E_SOURCE_EXTENSION_COLLECTION);
  e_source_backend_set_backend_name (E_SOURCE_BACKEND (collection_ext), self->oauth_backend);
  e_source_collection_set_identity (collection_ext, address);
  e_source_collection_set_mail_enabled (collection_ext, TRUE);
  e_source_collection_set_calendar_enabled (collection_ext, TRUE);
  e_source_collection_set_contacts_enabled (collection_ext, TRUE);
  auth_ext = e_source_get_extension (collection, E_SOURCE_EXTENSION_AUTHENTICATION);
  e_source_authentication_set_user (auth_ext, address);
  e_source_authentication_set_method (auth_ext, self->oauth_method);

  if (!e_source_registry_commit_source_sync (registry, collection, NULL, &error)) {
    g_warning ("%s: Could not create account: %s", G_STRFUNC, error ? error->message : "");
    show_error (self, _("Could Not Add Account"), error ? error->message : "");
    return;
  }

  adw_dialog_close (ADW_DIALOG (self));
}

static gboolean
stamp_account_editor_validate (StampAccountEditor *self)
{
  if (!*entry_text (self->email_row) ||
      !*entry_text (self->imap_host_row) ||
      !*entry_text (self->smtp_host_row)) {
    show_error (self, _("Missing Information"),
                _("Email address and server names are required."));
    return FALSE;
  }

  return TRUE;
}

static void
stamp_account_editor_create (StampAccountEditor *self)
{
  StampSession *session = stamp_session_get_default ();
  g_autoptr (GError) error = NULL;
  const gchar *address = entry_text (self->email_row);
  StampMailAccountParams params = { 0, };

  params.display_name = entry_text_fallback (self->account_name_row, address);
  params.identity_name = entry_text (self->name_row);
  params.address = address;
  params.reply_to = entry_text (self->reply_to_row);
  params.imap_host = entry_text (self->imap_host_row);
  params.imap_port = (guint16)adw_spin_row_get_value (self->imap_port_row);
  params.imap_security = security_methods[adw_combo_row_get_selected (self->imap_security_row)];
  params.imap_user = entry_text_fallback (self->imap_user_row, address);
  params.smtp_host = entry_text (self->smtp_host_row);
  params.smtp_port = (guint16)adw_spin_row_get_value (self->smtp_port_row);
  params.smtp_security = security_methods[adw_combo_row_get_selected (self->smtp_security_row)];
  params.smtp_user = entry_text_fallback (self->smtp_user_row, address);

  if (!stamp_session_create_mail_account (session, &params, &error)) {
    g_warning ("%s: Could not create account: %s", G_STRFUNC, error ? error->message : "");
    show_error (self, _("Could Not Add Account"), error ? error->message : "");
    return;
  }

  adw_dialog_close (ADW_DIALOG (self));
}

static gboolean
commit_source (ESourceRegistry *registry,
               ESource         *source,
               GError         **error)
{
  return e_source_registry_commit_source_sync (registry, source, NULL, error);
}

static void
stamp_account_editor_save (StampAccountEditor *self)
{
  StampSession *session = stamp_session_get_default ();
  ESourceRegistry *registry = stamp_session_get_registry (session);
  StampMailService *mail = stamp_account_get_mail_service (self->account);
  ESource *collection = stamp_account_get_collection (self->account);
  ESource *mail_source;
  ESource *transport_source;
  ESource *identity_source;
  ESourceAuthentication *auth_ext;
  ESourceSecurity *security_ext;
  ESourceMailIdentity *identity_ext;
  const gchar *address = entry_text (self->email_row);
  const gchar *reply_to = entry_text (self->reply_to_row);
  g_autoptr (GError) error = NULL;

  if (!mail || !stamp_mail_service_get_source (mail)) {
    g_warning ("%s: Account has no mail service", G_STRFUNC);
    return;
  }

  mail_source = stamp_mail_service_get_source (mail);
  transport_source = stamp_mail_service_get_transport_source (mail);
  identity_source = stamp_mail_service_get_identity_source (mail);

  if (collection)
    e_source_set_display_name (collection, entry_text_fallback (self->account_name_row, address));

  auth_ext = e_source_get_extension (mail_source, E_SOURCE_EXTENSION_AUTHENTICATION);
  e_source_authentication_set_host (auth_ext, entry_text (self->imap_host_row));
  e_source_authentication_set_port (auth_ext, (guint16)adw_spin_row_get_value (self->imap_port_row));
  e_source_authentication_set_user (auth_ext, entry_text_fallback (self->imap_user_row, address));
  security_ext = e_source_get_extension (mail_source, E_SOURCE_EXTENSION_SECURITY);
  e_source_security_set_method (security_ext, security_methods[adw_combo_row_get_selected (self->imap_security_row)]);

  if (transport_source) {
    auth_ext = e_source_get_extension (transport_source, E_SOURCE_EXTENSION_AUTHENTICATION);
    e_source_authentication_set_host (auth_ext, entry_text (self->smtp_host_row));
    e_source_authentication_set_port (auth_ext, (guint16)adw_spin_row_get_value (self->smtp_port_row));
    e_source_authentication_set_user (auth_ext, entry_text_fallback (self->smtp_user_row, address));
    security_ext = e_source_get_extension (transport_source, E_SOURCE_EXTENSION_SECURITY);
    e_source_security_set_method (security_ext, security_methods[adw_combo_row_get_selected (self->smtp_security_row)]);
  }

  if (identity_source) {
    identity_ext = e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_IDENTITY);
    e_source_mail_identity_set_name (identity_ext, entry_text (self->name_row));
    e_source_mail_identity_set_address (identity_ext, address);
    e_source_mail_identity_set_reply_to (identity_ext, *reply_to ? reply_to : NULL);
  }

  if ((collection && !commit_source (registry, collection, &error)) ||
      !commit_source (registry, mail_source, &error) ||
      (transport_source && !commit_source (registry, transport_source, &error)) ||
      (identity_source && !commit_source (registry, identity_source, &error))) {
    g_warning ("%s: Could not save account: %s", G_STRFUNC, error ? error->message : "");
    show_error (self, _("Could Not Save Account"), error ? error->message : "");
    return;
  }

  adw_dialog_close (ADW_DIALOG (self));
}

static void
on_save_clicked (AdwButtonRow       *row,
                 StampAccountEditor *self)
{
  if (!stamp_account_editor_validate (self))
    return;

  if (self->account)
    stamp_account_editor_save (self);
  else
    stamp_account_editor_create (self);
}

static void
stamp_account_editor_fill (StampAccountEditor *self)
{
  StampMailService *mail = stamp_account_get_mail_service (self->account);
  ESource *collection = stamp_account_get_collection (self->account);
  ESource *mail_source;
  ESource *transport_source;
  ESource *identity_source;
  ESourceAuthentication *auth_ext;
  ESourceSecurity *security_ext;

  if (!mail || !stamp_mail_service_get_source (mail))
    return;

  mail_source = stamp_mail_service_get_source (mail);
  transport_source = stamp_mail_service_get_transport_source (mail);
  identity_source = stamp_mail_service_get_identity_source (mail);

  if (collection)
    gtk_editable_set_text (GTK_EDITABLE (self->account_name_row), e_source_get_display_name (collection));

  auth_ext = e_source_get_extension (mail_source, E_SOURCE_EXTENSION_AUTHENTICATION);
  gtk_editable_set_text (GTK_EDITABLE (self->imap_host_row), e_source_authentication_get_host (auth_ext) ?: "");
  adw_spin_row_set_value (self->imap_port_row, e_source_authentication_get_port (auth_ext));
  gtk_editable_set_text (GTK_EDITABLE (self->imap_user_row), e_source_authentication_get_user (auth_ext) ?: "");
  security_ext = e_source_get_extension (mail_source, E_SOURCE_EXTENSION_SECURITY);
  adw_combo_row_set_selected (self->imap_security_row, security_method_to_index (e_source_security_get_method (security_ext)));

  if (transport_source) {
    auth_ext = e_source_get_extension (transport_source, E_SOURCE_EXTENSION_AUTHENTICATION);
    gtk_editable_set_text (GTK_EDITABLE (self->smtp_host_row), e_source_authentication_get_host (auth_ext) ?: "");
    adw_spin_row_set_value (self->smtp_port_row, e_source_authentication_get_port (auth_ext));
    gtk_editable_set_text (GTK_EDITABLE (self->smtp_user_row), e_source_authentication_get_user (auth_ext) ?: "");
    security_ext = e_source_get_extension (transport_source, E_SOURCE_EXTENSION_SECURITY);
    adw_combo_row_set_selected (self->smtp_security_row, security_method_to_index (e_source_security_get_method (security_ext)));
  }

  if (identity_source) {
    ESourceMailIdentity *identity_ext = e_source_get_extension (identity_source, E_SOURCE_EXTENSION_MAIL_IDENTITY);

    gtk_editable_set_text (GTK_EDITABLE (self->name_row), e_source_mail_identity_get_name (identity_ext) ?: "");
    gtk_editable_set_text (GTK_EDITABLE (self->email_row), e_source_mail_identity_get_address (identity_ext) ?: "");
    gtk_editable_set_text (GTK_EDITABLE (self->reply_to_row), e_source_mail_identity_get_reply_to (identity_ext) ?: "");
  }
}

static void
stamp_account_editor_dispose (GObject *object)
{
  StampAccountEditor *self = STAMP_ACCOUNT_EDITOR (object);

  g_clear_object (&self->account);
  g_clear_object (&self->carddav_prompter);

  gtk_widget_dispose_template (GTK_WIDGET (object), STAMP_TYPE_ACCOUNT_EDITOR);

  G_OBJECT_CLASS (stamp_account_editor_parent_class)->dispose (object);
}

static void
stamp_account_editor_class_init (StampAccountEditorClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = stamp_account_editor_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/stamp-account-editor.ui");

  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, navigation_view);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, form_page);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, account_name_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, name_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, email_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, reply_to_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, imap_host_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, imap_port_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, imap_security_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, imap_user_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, smtp_host_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, smtp_port_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, smtp_security_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, smtp_user_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, save_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, oauth_email_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, carddav_url_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, carddav_user_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, carddav_content_box);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, carddav_account_row);
  gtk_widget_class_bind_template_child (widget_class, StampAccountEditor, carddav_add_row);

  gtk_widget_class_bind_template_callback (widget_class, on_type_imap_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_type_google_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_type_microsoft_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_type_carddav_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_oauth_save_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_carddav_search_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_carddav_add_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_save_clicked);
}

static void
stamp_account_editor_init (StampAccountEditor *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
stamp_account_editor_new (void)
{
  return g_object_new (STAMP_TYPE_ACCOUNT_EDITOR, NULL);
}

GtkWidget *
stamp_account_editor_new_for_account (StampAccount *account)
{
  StampAccountEditor *self = g_object_new (STAMP_TYPE_ACCOUNT_EDITOR, NULL);

  self->account = g_object_ref (account);

  adw_navigation_page_set_title (self->form_page, _("Edit Account"));
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->save_row), _("Save"));
  stamp_account_editor_fill (self);

  adw_navigation_view_push_by_tag (self->navigation_view, "form");

  return GTK_WIDGET (self);
}
