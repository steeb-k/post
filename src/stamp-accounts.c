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
 * Account setup, in process. Post used to shell out to
 * gnome-online-accounts-gtk, which meant it could not add an account at
 * all unless that separate program happened to be installed. libgoa
 * -backend ships the same provider dialogs the Settings panel uses, so
 * we host them ourselves and drop the external dependency.
 */

#include "stamp-accounts.h"

#include <glib/gi18n.h>

/* GOA gates both libraries behind these, the way gnome-control-center
 * and gnome-online-accounts-gtk do. */
#define GOA_API_IS_SUBJECT_TO_CHANGE
#define GOA_BACKEND_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>
#include <goabackend/goabackend.h>

/* Post is a mail, contacts and calendar client; the chat, photo and
 * ticketing providers GOA also knows about have nothing to offer it. */
#define STAMP_ACCOUNTS_FEATURES (GOA_PROVIDER_FEATURE_MAIL | \
                                 GOA_PROVIDER_FEATURE_CALENDAR | \
                                 GOA_PROVIDER_FEATURE_CONTACTS)

struct _StampAccounts {
  AdwDialog parent_instance;

  AdwPreferencesGroup *accounts_group;
  AdwPreferencesGroup *providers_group;
  GtkWidget *providers_spinner;

  GoaClient *client;
  GList *rows;
  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (StampAccounts, stamp_accounts, ADW_TYPE_DIALOG);

static void populate_accounts (StampAccounts *self);

static void
report_error (StampAccounts *self,
              const gchar   *title,
              GError        *error)
{
  AdwDialog *dialog;

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
      g_error_matches (error, GOA_ERROR, GOA_ERROR_DIALOG_DISMISSED))
    return;

  dialog = adw_alert_dialog_new (title, error->message);
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", _("Close"));
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "close");

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

/*
 * Adding an account
 */

static void
on_account_added (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  StampAccounts *self = STAMP_ACCOUNTS (user_data);
  g_autoptr (GoaObject) object = NULL;
  g_autoptr (GError) error = NULL;

  object = goa_provider_add_account_finish (GOA_PROVIDER (source), result, &error);
  if (!object) {
    report_error (self, _("Could Not Add the Account"), error);
    return;
  }

  /* GoaClient tells us about the new account on its own, but only once
   * the daemon has caught up; refresh now so the list never looks
   * unchanged right after a successful setup. */
  populate_accounts (self);
}

static void
on_provider_row_activated (AdwActionRow  *row,
                           StampAccounts *self)
{
  GoaProvider *provider = g_object_get_data (G_OBJECT (row), "provider");

  goa_provider_add_account (provider,
                            self->client,
                            GTK_WIDGET (self),
                            self->cancellable,
                            on_account_added,
                            self);
}

/*
 * Existing accounts
 */

static void
on_account_shown (GObject      *source,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  StampAccounts *self = STAMP_ACCOUNTS (user_data);
  g_autoptr (GError) error = NULL;

  if (!goa_provider_show_account_finish (GOA_PROVIDER (source), result, &error))
    report_error (self, _("Could Not Open the Account"), error);
}

static void
on_account_row_activated (AdwActionRow  *row,
                          StampAccounts *self)
{
  GoaObject *object = g_object_get_data (G_OBJECT (row), "object");
  GoaAccount *account = goa_object_peek_account (object);
  g_autoptr (GoaProvider) provider = NULL;

  provider = goa_provider_get_for_provider_type (goa_account_get_provider_type (account));
  if (!provider)
    return;

  goa_provider_show_account (provider,
                             self->client,
                             object,
                             GTK_WIDGET (self),
                             self->cancellable,
                             on_account_shown,
                             self);
}

static void
on_account_removed (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  StampAccounts *self = STAMP_ACCOUNTS (user_data);
  g_autoptr (GError) error = NULL;

  if (!goa_account_call_remove_finish (GOA_ACCOUNT (source), result, &error)) {
    report_error (self, _("Could Not Remove the Account"), error);
    return;
  }

  populate_accounts (self);
}

static void
on_remove_response (AdwAlertDialog *dialog,
                    gchar          *response,
                    gpointer        user_data)
{
  StampAccounts *self = STAMP_ACCOUNTS (user_data);
  GoaObject *object = g_object_get_data (G_OBJECT (dialog), "object");

  goa_account_call_remove (goa_object_peek_account (object),
                           self->cancellable,
                           on_account_removed,
                           self);
}

static void
on_remove_clicked (GtkButton     *button,
                   StampAccounts *self)
{
  GoaObject *object = g_object_get_data (G_OBJECT (button), "object");
  GoaAccount *account = goa_object_peek_account (object);
  g_autofree char *body = NULL;
  AdwDialog *dialog;

  body = g_strdup_printf (_("Mail, contacts and calendars from %s will no longer be available in Post."),
                          goa_account_get_presentation_identity (account));

  dialog = adw_alert_dialog_new (_("Remove Account?"), body);
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "cancel", _("Cancel"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "remove", _("Remove"));
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "remove", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  g_object_set_data_full (G_OBJECT (dialog), "object", g_object_ref (object), g_object_unref);
  g_signal_connect (dialog, "response::remove", G_CALLBACK (on_remove_response), self);

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static GtkWidget *
create_account_row (StampAccounts *self,
                    GoaObject     *object)
{
  GoaAccount *account = goa_object_peek_account (object);
  g_autoptr (GIcon) icon = NULL;
  GtkWidget *row = adw_action_row_new ();
  GtkWidget *image;
  GtkWidget *remove;

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), goa_account_get_provider_name (account));
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), goa_account_get_presentation_identity (account));

  icon = g_icon_new_for_string (goa_account_get_provider_icon (account), NULL);
  if (icon) {
    image = gtk_image_new_from_gicon (icon);
    adw_action_row_add_prefix (ADW_ACTION_ROW (row), image);
  }

  remove = gtk_button_new_from_icon_name ("user-trash-symbolic");
  gtk_widget_set_valign (remove, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (remove, _("Remove Account"));
  gtk_widget_add_css_class (remove, "flat");
  g_object_set_data_full (G_OBJECT (remove), "object", g_object_ref (object), g_object_unref);
  g_signal_connect (remove, "clicked", G_CALLBACK (on_remove_clicked), self);
  adw_action_row_add_suffix (ADW_ACTION_ROW (row), remove);

  adw_action_row_set_activatable_widget (ADW_ACTION_ROW (row), NULL);
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_object_set_data_full (G_OBJECT (row), "object", g_object_ref (object), g_object_unref);
  g_signal_connect (row, "activated", G_CALLBACK (on_account_row_activated), self);

  return row;
}

static void
populate_accounts (StampAccounts *self)
{
  g_autolist (GoaObject) accounts = NULL;
  guint n_accounts = 0;

  for (GList *l = self->rows; l; l = l->next)
    adw_preferences_group_remove (self->accounts_group, l->data);
  g_clear_pointer (&self->rows, g_list_free);

  if (!self->client)
    return;

  accounts = goa_client_get_accounts (self->client);
  for (GList *l = accounts; l; l = l->next) {
    GtkWidget *row = create_account_row (self, l->data);

    adw_preferences_group_add (self->accounts_group, row);
    self->rows = g_list_prepend (self->rows, row);
    n_accounts++;
  }

  gtk_widget_set_visible (GTK_WIDGET (self->accounts_group), n_accounts > 0);
}

/*
 * Startup
 */

static void
on_providers_ready (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  StampAccounts *self = STAMP_ACCOUNTS (user_data);
  g_autolist (GoaProvider) providers = NULL;
  g_autoptr (GError) error = NULL;

  if (!goa_provider_get_all_finish (&providers, result, &error)) {
    report_error (self, _("Could Not List the Account Types"), error);
    return;
  }

  gtk_widget_set_visible (self->providers_spinner, FALSE);

  for (GList *l = providers; l; l = l->next) {
    GoaProvider *provider = l->data;
    g_autofree char *name = NULL;
    g_autoptr (GIcon) icon = NULL;
    GtkWidget *row;

    if ((goa_provider_get_provider_features (provider) & STAMP_ACCOUNTS_FEATURES) == 0)
      continue;

    name = goa_provider_get_provider_name (provider, NULL);
    row = adw_action_row_new ();
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), name);

    icon = goa_provider_get_provider_icon (provider, NULL);
    if (icon)
      adw_action_row_add_prefix (ADW_ACTION_ROW (row), gtk_image_new_from_gicon (icon));

    adw_action_row_add_suffix (ADW_ACTION_ROW (row), gtk_image_new_from_icon_name ("go-next-symbolic"));
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);

    g_object_set_data_full (G_OBJECT (row), "provider", g_object_ref (provider), g_object_unref);
    g_signal_connect (row, "activated", G_CALLBACK (on_provider_row_activated), self);

    adw_preferences_group_add (self->providers_group, row);
  }
}

static void
on_client_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  StampAccounts *self = STAMP_ACCOUNTS (user_data);
  g_autoptr (GError) error = NULL;

  self->client = goa_client_new_finish (result, &error);
  if (!self->client) {
    report_error (self, _("Could Not Reach the Account Service"), error);
    return;
  }

  g_signal_connect_object (self->client, "account-added", G_CALLBACK (populate_accounts), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->client, "account-changed", G_CALLBACK (populate_accounts), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->client, "account-removed", G_CALLBACK (populate_accounts), self, G_CONNECT_SWAPPED);

  populate_accounts (self);

  goa_provider_get_all (on_providers_ready, self);
}

/*
 * GObject
 */

static void
stamp_accounts_dispose (GObject *object)
{
  StampAccounts *self = STAMP_ACCOUNTS (object);

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  g_clear_pointer (&self->rows, g_list_free);
  g_clear_object (&self->client);

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_ACCOUNTS);

  G_OBJECT_CLASS (stamp_accounts_parent_class)->dispose (object);
}

static void
stamp_accounts_class_init (StampAccountsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = stamp_accounts_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/stamp-accounts.ui");

  gtk_widget_class_bind_template_child (widget_class, StampAccounts, accounts_group);
  gtk_widget_class_bind_template_child (widget_class, StampAccounts, providers_group);
  gtk_widget_class_bind_template_child (widget_class, StampAccounts, providers_spinner);
}

static void
stamp_accounts_init (StampAccounts *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();

  goa_client_new (self->cancellable, on_client_ready, self);
}

AdwDialog *
stamp_accounts_new (void)
{
  return g_object_new (STAMP_TYPE_ACCOUNTS, NULL);
}

/**
 * stamp_accounts_present:
 * @parent: the widget to present the dialog from
 *
 * Opens the account setup dialog.
 */
void
stamp_accounts_present (GtkWidget *parent)
{
  g_return_if_fail (GTK_IS_WIDGET (parent));

  adw_dialog_present (stamp_accounts_new (), parent);
}
