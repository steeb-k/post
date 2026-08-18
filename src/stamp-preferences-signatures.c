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

#include "stamp-preferences-signatures.h"

#include <glib/gi18n.h>

#include "stamp-format-bar.h"
#include "stamp-session.h"
#include "stamp-signature.h"
#include "stamp-webview.h"

struct _StampPreferencesSignatureEditor {
  AdwNavigationPage parent_instance;

  AdwWindowTitle *editor_window_title;
  AdwEntryRow *name_row;
  StampWebView *web_view;
  StampFormatBar *format_bar;
  AdwButtonRow *save;
  AdwButtonRow *remove;

  StampSignature *signature;
  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (StampPreferencesSignatureEditor, stamp_preferences_signature_editor, ADW_TYPE_NAVIGATION_PAGE);

static void
on_editor_get_body_html (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  StampPreferencesSignatureEditor *self = STAMP_PREFERENCES_SIGNATURE_EDITOR (user_data);
  g_autoptr (GError) error = NULL;
  g_autofree char *html = NULL;
  const gchar *name;
  StampSession *session;

  html = stamp_webview_get_body_html_finish (self->web_view, res, NULL, &error);
  if (error) {
    g_warning ("%s: Could not load body html: %s", G_STRFUNC, error->message);
    return;
  }

  name = gtk_editable_get_text (GTK_EDITABLE (self->name_row));
  if (!name || strlen (name) == 0) {
    g_warning ("%s: Name is empty", G_STRFUNC);
    return;
  }

  session = stamp_session_get_default ();

  if (self->signature) {
    ESourceRegistry *registry = stamp_session_get_registry (session);

    stamp_signature_set_name (self->signature, name);
    e_source_set_display_name (stamp_signature_get_source (self->signature), name);
    stamp_signature_save (self->signature, html);

    e_source_registry_commit_source_sync (registry, stamp_signature_get_source (self->signature), NULL, &error);
    if (error)
      g_warning ("%s: Could not commit signature: %s", G_STRFUNC, error->message);
  } else {
    self->signature = stamp_session_create_signature (session, name, html, "text/html");
  }

  {
    GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (self));

    g_assert (ADW_IS_NAVIGATION_VIEW (parent));
    adw_navigation_view_pop (ADW_NAVIGATION_VIEW (parent));
  }
}

static void
on_editor_save_clicked (GtkWidget *button,
                        gpointer   user_data)
{
  StampPreferencesSignatureEditor *self = STAMP_PREFERENCES_SIGNATURE_EDITOR (user_data);

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  self->cancellable = g_cancellable_new ();

  stamp_webview_get_body_html (self->web_view, self->cancellable, on_editor_get_body_html, self);
}

static void
on_editor_remove_clicked (GtkWidget *button,
                          gpointer   user_data)
{
  StampPreferencesSignatureEditor *self = STAMP_PREFERENCES_SIGNATURE_EDITOR (user_data);
  GtkWidget *parent;

  if (!self->signature)
    return;

  stamp_session_remove_signature (stamp_session_get_default (), self->signature);
  self->signature = NULL;

  parent = gtk_widget_get_parent (GTK_WIDGET (self));
  g_assert (ADW_IS_NAVIGATION_VIEW (parent));
  adw_navigation_view_pop (ADW_NAVIGATION_VIEW (parent));
}

static void
stamp_preferences_signature_editor_dispose (GObject *object)
{
  StampPreferencesSignatureEditor *self = STAMP_PREFERENCES_SIGNATURE_EDITOR (object);

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (stamp_preferences_signature_editor_parent_class)->dispose (object);
}

void
stamp_preferences_signature_editor_class_init (StampPreferencesSignatureEditorClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = stamp_preferences_signature_editor_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/stamp-preferences-signatures-editor.ui");

  gtk_widget_class_bind_template_child (widget_class, StampPreferencesSignatureEditor, editor_window_title);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesSignatureEditor, name_row);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesSignatureEditor, web_view);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesSignatureEditor, format_bar);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesSignatureEditor, save);
  gtk_widget_class_bind_template_child (widget_class, StampPreferencesSignatureEditor, remove);

  gtk_widget_class_bind_template_callback (widget_class, on_editor_save_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_editor_remove_clicked);
}

static void
stamp_preferences_signature_editor_setup (StampPreferencesSignatureEditor *self)
{
  webkit_web_view_set_editable (WEBKIT_WEB_VIEW (self->web_view), TRUE);

  /* The composer does the same: without this the view never takes GTK
   * focus, which keyboard shortcuts key off. */
  gtk_widget_set_focusable (GTK_WIDGET (self->web_view), TRUE);

  stamp_format_bar_set_webview (self->format_bar, self->web_view);

  if (self->signature) {
    adw_window_title_set_title (self->editor_window_title, _("Edit Signature"));
    gtk_editable_set_text (GTK_EDITABLE (self->name_row), stamp_signature_get_name (self->signature));

    if (g_strcmp0 (stamp_signature_get_mime_type (self->signature), "text/html") == 0)
      webkit_web_view_load_html (WEBKIT_WEB_VIEW (self->web_view), stamp_signature_get_content (self->signature), NULL);
    else
      webkit_web_view_load_plain_text (WEBKIT_WEB_VIEW (self->web_view), stamp_signature_get_content (self->signature));

    gtk_widget_set_visible (GTK_WIDGET (self->save), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->remove), TRUE);
  } else {
    g_autoptr (GBytes) template = NULL;
    g_autoptr (GError) error = NULL;

    adw_window_title_set_title (self->editor_window_title, _("New Signature"));
    gtk_widget_set_visible (GTK_WIDGET (self->save), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->remove), FALSE);

    template = g_resources_lookup_data ("/io/github/steeb_k/Post/blank-message-template.html", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (error) {
      g_warning ("%s: Could not load blank message template: %s", G_STRFUNC, error->message);
      return;
    }

    webkit_web_view_load_html (WEBKIT_WEB_VIEW (self->web_view), g_bytes_get_data (template, NULL), NULL);
  }
}

void
stamp_preferences_signature_editor_init (StampPreferencesSignatureEditor *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
stamp_preferences_signature_editor_new (StampSignature *signature)
{
  StampPreferencesSignatureEditor *self = g_object_new (STAMP_TYPE_PREFERENCES_SIGNATURE_EDITOR, NULL);

  self->signature = signature;

  stamp_preferences_signature_editor_setup (self);

  return GTK_WIDGET (self);
}
