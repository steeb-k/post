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

#include "stamp-session.h"
#include "stamp-signature.h"
#include "stamp-webview.h"

#include <glib/gi18n.h>
#include <libportal-gtk4/portal-gtk4.h>

struct _StampPreferencesSignatures {
  AdwNavigationPage parent_instance;

  StampWebView *web_view;
  StampSignature *signature;
  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (StampPreferencesSignatures, stamp_preferences_signatures, ADW_TYPE_NAVIGATION_PAGE);

static void
on_get_body_html (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  StampPreferencesSignatures *self = STAMP_PREFERENCES_SIGNATURES (user_data);
  g_autoptr (GError) error = NULL;
  g_autofree char *html = NULL;

  html = stamp_webview_get_body_html_finish (self->web_view, res, &error);
  if (error) {
    g_warning ("%s: Could not load body html: %s", G_STRFUNC, error->message);
    return;
  }

  if (self->signature)
    stamp_signature_save (self->signature, html);
  else
    g_warning ("%s: No signature structure…", G_STRFUNC);
}

static void
on_save_clicked (GtkWidget *button,
                 gpointer   user_data)
{
  StampPreferencesSignatures *self = STAMP_PREFERENCES_SIGNATURES (user_data);

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  self->cancellable = g_cancellable_new ();

  stamp_webview_get_body_html (self->web_view, self->cancellable, on_get_body_html, self);
}

static void
stamp_preferences_signatures_dispose (GObject *object)
{
  StampPreferencesSignatures *self = STAMP_PREFERENCES_SIGNATURES (object);

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (stamp_preferences_signatures_parent_class)->dispose (object);
}

void
stamp_preferences_signatures_class_init (StampPreferencesSignaturesClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = stamp_preferences_signatures_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/stamp-preferences-signatures.ui");

  gtk_widget_class_bind_template_child (widget_class, StampPreferencesSignatures, web_view);

  gtk_widget_class_bind_template_callback (widget_class, on_save_clicked);
}

void
stamp_preferences_signatures_init (StampPreferencesSignatures *self)
{
  g_autoptr (GError) error = NULL;
  StampSession *session = stamp_session_get_default ();
  GList *signatures;

  gtk_widget_init_template (GTK_WIDGET (self));

  webkit_web_view_set_editable (WEBKIT_WEB_VIEW (self->web_view), TRUE);

  signatures = stamp_session_get_signatures (session);
  if (signatures) {
    StampSignature *signature = signatures->data;

    self->signature = signature;
    if (g_strcmp0 (stamp_signature_get_mime_type (signature), "text/html") == 0)
      webkit_web_view_load_html (WEBKIT_WEB_VIEW (self->web_view), stamp_signature_get_content (signature), NULL);
    else
      webkit_web_view_load_plain_text (WEBKIT_WEB_VIEW (self->web_view), stamp_signature_get_content (signature));
  } else {
    g_autoptr (GBytes) template = NULL;

    template = g_resources_lookup_data ("/org/tabos/stamp/blank-message-template.html", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (error) {
      g_warning ("%s: Could not load blank message template: %s", G_STRFUNC, error->message);
      return;
    }

    webkit_web_view_load_html (WEBKIT_WEB_VIEW (self->web_view), g_bytes_get_data (template, NULL), NULL);
  }
}

GtkWidget *
stamp_preferences_signatures_new (void)
{
  return g_object_new (STAMP_TYPE_PREFERENCES_SIGNATURES, NULL);
}
