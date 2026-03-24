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

#include "stamp-webview.h"

struct _StampWebView {
  WebKitWebView parent_instance;
};

typedef struct {
  WebKitWebView parent_instance;

  GHashTable *internal_resources;
  gint32 width_request;
  gint32 height_request;
  gboolean loaded;
  char *queued_body_content;
  gboolean queued_load_images;
  GCancellable *cancellable;
  gboolean body_html_changed;
} StampWebViewPrivate;

G_DEFINE_FINAL_TYPE_WITH_PRIVATE (StampWebView, stamp_webview, WEBKIT_TYPE_WEB_VIEW)

enum {
  PROP_0,
  PROP_SIZE_REQUEST,
  LAST_PROP
};

enum {
  IMAGE_LOAD_BLOCKED,
  SELECTION_CHANGED,
  LOADED,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

static void
stamp_webview_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  StampWebView *self = STAMP_WEB_VIEW (object);
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);

  switch (property_id) {
    case PROP_SIZE_REQUEST:
      g_value_set_boolean (value, priv->loaded);
      break;
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_web_view_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  switch (property_id) {
    case PROP_SIZE_REQUEST:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static gboolean
handle_internal_response (StampWebView           *self,
                          WebKitURISchemeRequest *request)
{
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);
  g_autofree char *path = g_uri_unescape_string (webkit_uri_scheme_request_get_path (request), NULL);
  GInputStream *stream = g_hash_table_lookup (priv->internal_resources, path);

  if (stream) {
    if (G_IS_SEEKABLE (stream)) {
      GSeekable *seekable = G_SEEKABLE (stream);
      g_seekable_seek (seekable, 0, G_SEEK_SET, NULL, NULL);
    }
    webkit_uri_scheme_request_finish (request, stream, -1, NULL);
    return TRUE;
  }

  return FALSE;
}

static void
on_cid_request (WebKitURISchemeRequest *request,
                gpointer                user_data)
{
  StampWebView *self = STAMP_WEB_VIEW (webkit_uri_scheme_request_get_web_view (request));

  if (!handle_internal_response (self, request)) {
    GError *error = g_error_new_literal (g_quark_from_string ("Stamp"), 1, "Failed to handle internal response");
    webkit_uri_scheme_request_finish_error (request, error);
  }
}

static void
on_send_message_to_page (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  StampWebView *self = STAMP_WEB_VIEW (user_data);
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);
  g_autoptr (GError) error = NULL;
  g_autoptr (WebKitUserMessage) response = NULL;
  GVariant *variant;
  gint32 width, height;

  response = webkit_web_view_send_message_to_page_finish (WEBKIT_WEB_VIEW (source), res, &error);
  if (error) {
    g_warning ("Could not send message to page: %s", error->message);
    return;
  }

  variant = webkit_user_message_get_parameters (response);
  g_variant_get (variant, "(uu)", &width, &height);

  priv->width_request = width;
  priv->height_request = height;

  g_object_notify (G_OBJECT (self), "size-request");
}

static void
on_load_changed (WebKitWebView   *web_view,
                 WebKitLoadEvent  load_event,
                 gpointer         user_data)
{
  StampWebView *self = STAMP_WEB_VIEW (user_data);
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);

  g_signal_emit (self, signals[LOADED], 0, load_event == WEBKIT_LOAD_FINISHED);

  if (load_event == WEBKIT_LOAD_FINISHED || load_event == WEBKIT_LOAD_COMMITTED) {
    WebKitUserMessage *message = webkit_user_message_new ("get-page-size", NULL);

    webkit_web_view_send_message_to_page (web_view, message, NULL, on_send_message_to_page, self);
  }

  if (load_event == WEBKIT_LOAD_FINISHED) {
    priv->loaded = TRUE;

    webkit_web_view_evaluate_javascript (web_view,
                                         "document.querySelector('[contenteditable]').focus();",
                                         -1, NULL, NULL, NULL, NULL, NULL);
    if (priv->queued_body_content) {
      stamp_web_view_set_body_content (self, priv->queued_body_content);
    }

    if (priv->queued_load_images) {
      stamp_web_view_load_images (self);
    }
  }
}

static gboolean
on_decide_policy (WebKitWebView            *web_view,
                  WebKitPolicyDecision     *decision,
                  WebKitPolicyDecisionType  type,
                  gpointer                  user_data)
{
  if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION || type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
    WebKitNavigationPolicyDecision *navigation_decision;
    WebKitNavigationAction *navigation_action;
    WebKitNavigationType navigation_type;

    navigation_decision = WEBKIT_NAVIGATION_POLICY_DECISION (decision);
    navigation_action = webkit_navigation_policy_decision_get_navigation_action (navigation_decision);

    navigation_type = webkit_navigation_action_get_navigation_type (navigation_action);
    if (navigation_type == WEBKIT_NAVIGATION_TYPE_LINK_CLICKED) {
      WebKitURIRequest *request = webkit_navigation_action_get_request (navigation_action);
      const char *uri = webkit_uri_request_get_uri (request);
      g_autoptr (GError) error = NULL;

      if (!g_app_info_launch_default_for_uri (uri, NULL, &error)) {
        g_warning ("Could not launch URI '%s': %s", uri, error ? error->message : "unknown error");
      }
      webkit_policy_decision_ignore (decision);
      return GDK_EVENT_STOP;
    } else if (navigation_type == WEBKIT_NAVIGATION_TYPE_OTHER) {
      webkit_policy_decision_use (decision);
      return GDK_EVENT_STOP;
    }
  }

  webkit_policy_decision_ignore (decision);
  return GDK_EVENT_STOP;
}

static void
initialize_web_process_extensions (WebKitWebContext *web_context,
                                   StampWebView     *self)
{
  webkit_web_context_set_web_process_extensions_directory (web_context, STAMP_WEB_PROCESS_EXTENSIONS_DIR);
  webkit_web_context_set_web_process_extensions_initialization_user_data (web_context, g_variant_new_int32 (0));
}

static WebKitSettings *settings;

static gpointer
stamp_prefs_init (gpointer user_data)
{
  StampWebView *self = STAMP_WEB_VIEW (user_data);
  WebKitWebContext *web_context;
  g_autoptr (GStrvBuilder) builder = NULL;
  g_auto (GStrv) languages = NULL;

  settings = webkit_settings_new_with_settings ("allow-modal-dialogs", FALSE,
                                                "enable-fullscreen", FALSE,
                                                "enable-html5-database", FALSE,
                                                "enable-html5-local-storage", FALSE,
                                                "enable-javascript", TRUE,
                                                "enable-javascript-markup", FALSE,
                                                "enable-media-stream", FALSE,
                                                NULL);

  /* FIXME: Sometimes widget is not drawn, try to avoid it */
  webkit_settings_set_hardware_acceleration_policy (settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

  web_context = webkit_web_context_get_default ();
  webkit_web_context_set_spell_checking_enabled (web_context, TRUE);

  builder = g_strv_builder_new ();
  g_strv_builder_add (builder, "de_DE");
  languages = g_strv_builder_end (builder);

  webkit_web_context_set_spell_checking_languages (
    web_context,
    (const char * const *)languages
    );
  g_signal_connect_object (web_context, "initialize-web-process-extensions",
                           G_CALLBACK (initialize_web_process_extensions),
                           self, 0);

  webkit_web_context_register_uri_scheme (web_context, "cid", on_cid_request, g_object_ref (self), g_object_unref);

  return settings;
}

static WebKitSettings *
stamp_get_webkit_settings (StampWebView *self)
{
  static GOnce once_init = G_ONCE_INIT;

  return g_once (&once_init, stamp_prefs_init, self);
}

static
void
on_key_released (GtkEventControllerKey *controller,
                 guint                  keyval,
                 guint                  keycode,
                 GdkModifierType        state,
                 gpointer               user_data)
{
  StampWebView *self = STAMP_WEB_VIEW (user_data);
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);

  priv->body_html_changed = TRUE;
}

static void
stamp_webview_constructed (GObject *object)
{
  StampWebView *self = STAMP_WEB_VIEW (object);
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);
  GtkEventController *controller;

  G_OBJECT_CLASS (stamp_webview_parent_class)->constructed (object);

  priv->internal_resources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  priv->queued_body_content = NULL;

  webkit_web_view_set_settings (WEBKIT_WEB_VIEW (self), stamp_get_webkit_settings (self));

  controller = gtk_event_controller_key_new ();
  g_signal_connect_object (G_OBJECT (controller), "key-released", G_CALLBACK (on_key_released), self, 0);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);
  g_signal_connect_object (G_OBJECT (self), "load-changed", G_CALLBACK (on_load_changed), self, 0);
  g_signal_connect_object (G_OBJECT (self), "decide-policy", G_CALLBACK (on_decide_policy), self, 0);
}

static void
stamp_webview_image_load_blocked (StampWebView *self)
{
  g_signal_emit (self, signals[IMAGE_LOAD_BLOCKED], 0);
}

static gboolean
stamp_webview_user_message_received (WebKitWebView     *webview,
                                     WebKitUserMessage *message)
{
  StampWebView *self = STAMP_WEB_VIEW (webview);
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);
  const char *name = webkit_user_message_get_name (message);

  g_print ("%s: ENTER %s\n", G_STRFUNC, name);
  if (g_strcmp0 (name, "image-load-blocked") == 0) {
    g_print ("%s: %d\n", G_STRFUNC, priv->queued_load_images);
    if (!priv->queued_load_images)
      stamp_webview_image_load_blocked (self);

    return TRUE;
  } else if (g_strcmp0 (name, "selection-changed") == 0) {
    g_signal_emit (self, signals[SELECTION_CHANGED], 0);
  } else {
    g_critical ("%s: Unhandled message: %s", G_STRFUNC, name);
  }

  return FALSE;
}

static void
stamp_web_view_dispose (GObject *object)
{
  StampWebView *self = STAMP_WEB_VIEW (object);
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);

  if (priv->cancellable)
    g_cancellable_cancel (priv->cancellable);

  g_clear_object (&priv->cancellable);

  g_clear_pointer (&priv->queued_body_content, g_free);
  g_clear_pointer (&priv->internal_resources, g_hash_table_unref);

  G_OBJECT_CLASS (stamp_webview_parent_class)->dispose (object);
}

static void
stamp_webview_class_init (StampWebViewClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  WebKitWebViewClass *widget_class = WEBKIT_WEB_VIEW_CLASS (klass);

  gobject_class->constructed = stamp_webview_constructed;
  gobject_class->get_property = stamp_webview_get_property;
  gobject_class->set_property = stamp_web_view_set_property;
  gobject_class->dispose = stamp_web_view_dispose;

  widget_class->user_message_received = stamp_webview_user_message_received;

  signals[IMAGE_LOAD_BLOCKED] = g_signal_new ("image-load-blocked", G_OBJECT_CLASS_TYPE (klass),
                                              G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL, NULL,
                                              G_TYPE_NONE,
                                              0);
  signals[SELECTION_CHANGED] = g_signal_new ("selection-changed", G_OBJECT_CLASS_TYPE (klass),
                                             G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                             0, NULL, NULL, NULL,
                                             G_TYPE_NONE,
                                             0);
  signals[LOADED] = g_signal_new ("loaded", G_OBJECT_CLASS_TYPE (klass),
                                  G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                  0, NULL, NULL, NULL,
                                  G_TYPE_NONE,
                                  1, G_TYPE_BOOLEAN);

  g_object_class_install_property (gobject_class, PROP_SIZE_REQUEST,
                                   g_param_spec_boolean ("size-request",
                                                         NULL,
                                                         NULL,
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
stamp_webview_init (StampWebView *self)
{
}

StampWebView *
stamp_webview_new (void)
{
  return g_object_new (STAMP_TYPE_WEBVIEW, NULL);
}

void
stamp_webview_load_html (StampWebView *self,
                         char         *content)
{
  if (content)
    webkit_web_view_load_html (WEBKIT_WEB_VIEW (self), content, NULL);
}

void
stamp_webview_load_plain_text (StampWebView *self,
                               char         *content)
{
  if (content) {
    g_autoptr (GBytes) template = g_resources_lookup_data ("/org/tabos/stamp/plain-message-template.html", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    g_autofree char *html = g_strdup_printf (g_bytes_get_data (template, NULL), content);
#pragma GCC diagnostic pop

    webkit_web_view_load_html (WEBKIT_WEB_VIEW (self), html, NULL);
  }
}

void
stamp_webview_add_internal_resource (StampWebView *self,
                                     const char   *id,
                                     GInputStream *stream)
{
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);

  g_hash_table_insert (priv->internal_resources, g_strdup (id), g_object_ref (stream));
}

void
stamp_webview_get_body_html (StampWebView        *self,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  WebKitUserMessage *message = webkit_user_message_new ("get-body-html", g_variant_new_boolean (TRUE));

  webkit_web_view_send_message_to_page (WEBKIT_WEB_VIEW (self), message, cancellable, callback, user_data);
}

char *
stamp_webview_get_body_html_finish (StampWebView  *self,
                                    GAsyncResult  *res,
                                    GError       **error)
{
  g_autoptr (GError) local_error = NULL;
  g_autoptr (WebKitUserMessage) response = webkit_web_view_send_message_to_page_finish (WEBKIT_WEB_VIEW (self), res, &local_error);
  GVariant *parameters;
  const char *out;

  if (local_error) {
    g_propagate_error (error, g_steal_pointer (&local_error));
    return NULL;
  }

  parameters = webkit_user_message_get_parameters (response);
  out = g_variant_get_string (parameters, NULL);

  return g_strdup (out);
}

/* static void */
/* on_set_body_html (GObject      *source, */
/*                   GAsyncResult *res, */
/*                   gpointer      user_data) */
/* { */
/*   WebKitWebView *web_view = WEBKIT_WEB_VIEW (source); */
/*   StampWebView *self = STAMP_WEB_VIEW (source); */

/*   webkit_web_view_send_message_to_page_finish (web_view, res, NULL); */
/*   gtk_widget_grab_focus (GTK_WIDGET (self)); */
/* } */

void
stamp_web_view_set_body_content (StampWebView *self,
                                 char         *content)
{
#if 0
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);

  if (priv->loaded) {
    WebKitUserMessage *message = webkit_user_message_new ("set-body-html", g_variant_new_string (content));

    if (priv->cancellable)
      g_cancellable_cancel (priv->cancellable);

    g_clear_object (&priv->cancellable);

    priv->cancellable = g_cancellable_new ();
    webkit_web_view_send_message_to_page (WEBKIT_WEB_VIEW (self), message, priv->cancellable, on_set_body_html, self);
  } else {
    if (priv->queued_body_content == content)
      return;

    g_clear_pointer (&priv->queued_body_content, g_free);
    priv->queued_body_content = g_strdup (content);
  }
#else
  g_autoptr (GBytes) template = g_resources_lookup_data ("/org/tabos/stamp/blank-message-template.html", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
  g_autoptr (GBytes) js = g_resources_lookup_data ("/org/tabos/stamp/stamp-composer.js", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
  g_autofree char *html = g_strdup_printf (g_bytes_get_data (template, NULL), content);
#pragma GCC diagnostic pop
  WebKitUserScript *script = webkit_user_script_new (g_bytes_get_data (js, NULL), WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL);

  webkit_web_view_load_html (WEBKIT_WEB_VIEW (self), html, NULL);
  webkit_user_content_manager_add_script (webkit_web_view_get_user_content_manager (WEBKIT_WEB_VIEW (self)), script);
#endif
}

void
stamp_web_view_load_images (StampWebView *self)
{
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);

  g_print ("%s: %d\n", G_STRFUNC, priv->loaded);
  if (priv->loaded) {
    WebKitUserMessage *message = webkit_user_message_new ("set-image-loading-enabled", g_variant_new_boolean (TRUE));

    webkit_web_view_send_message_to_page (WEBKIT_WEB_VIEW (self), message, priv->cancellable, NULL, NULL);
  } else {
    priv->queued_load_images = TRUE;
  }
}

void
stamp_web_view_query_command_state (StampWebView        *self,
                                    const char          *command,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  WebKitUserMessage *message = webkit_user_message_new ("query-command-state", g_variant_new_string (command));

  webkit_web_view_send_message_to_page (WEBKIT_WEB_VIEW (self), message, cancellable, callback, user_data);
}

gboolean
stamp_web_view_query_command_state_finish (GObject       *source,
                                           GAsyncResult  *res,
                                           GError       **error)
{
  g_autoptr (GError) local_error = NULL;
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (source);
  g_autoptr (WebKitUserMessage) response = NULL;
  GVariant *parameters;

  response = webkit_web_view_send_message_to_page_finish (web_view, res, &local_error);
  if (local_error) {
    g_warning ("Could not query command state: %s", local_error->message);
    g_propagate_error (error, local_error);
    return FALSE;
  }

  parameters = webkit_user_message_get_parameters (response);
  return g_variant_get_boolean (parameters);
}

void
stamp_web_view_execute_editor_command (StampWebView *self,
                                       const char   *command,
                                       const char   *argument)
{
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_auto (GStrv) arguments = NULL;
  WebKitUserMessage *message;

  g_strv_builder_add (builder, command);
  g_strv_builder_add (builder, argument);

  arguments = g_strv_builder_end (builder);
  message = webkit_user_message_new ("execute-editor-command", g_variant_new_strv ((const char * const *)arguments, 2));

  webkit_web_view_send_message_to_page (WEBKIT_WEB_VIEW (self), message, priv->cancellable, NULL, NULL);
}

void
stamp_web_view_get_size (StampWebView *self,
                         gint32       *width,
                         gint32       *height)
{
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);

  *width = priv->width_request;
  *height = priv->height_request;
}

gboolean
stamp_web_view_changed (StampWebView *self)
{
  StampWebViewPrivate *priv = stamp_webview_get_instance_private (self);
  return priv->body_html_changed;
}

void
stamp_webview_set_editable (StampWebView *self)
{
  /* g_autoptr (GError) error = NULL; */
  /* g_autoptr (GBytes) css = g_resources_lookup_data("/org/tabos/stamp/views/mail/composer/stamp-composer.css", G_RESOURCE_LOOKUP_FLAGS_NONE, &error); */

  /* if (error) */
  /*   g_warning ("%s: Could not load css: %s", G_STRFUNC, error->message); */

  /* g_print ("%s: %s\n", G_STRFUNC, (char *)g_bytes_get_data (css, NULL)); */
  /* WebKitUserStyleSheet *style_sheet = webkit_user_style_sheet_new (g_bytes_get_data (css, NULL), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_STYLE_LEVEL_USER, NULL, NULL); */
  /* WebKitUserContentManager *manager = webkit_web_view_get_user_content_manager (WEBKIT_WEB_VIEW (self)); */
  /* webkit_user_content_manager_add_style_sheet (manager, style_sheet); */
}

void
stamp_webview_copy_resources (StampWebView *src,
                              StampWebView *dst)
{
  StampWebViewPrivate *priv_src = stamp_webview_get_instance_private (src);
  StampWebViewPrivate *priv_dst = stamp_webview_get_instance_private (dst);

  priv_dst->internal_resources = g_hash_table_ref (priv_src->internal_resources);
}
