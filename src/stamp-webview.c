#include "config.h"

#include "stamp-webview.h"

struct _StampWebView {
  WebKitWebView parent_instance;

  GHashTable *internal_resources;
  gint32 height_request;

};

G_DEFINE_FINAL_TYPE (StampWebView, stamp_webview, WEBKIT_TYPE_WEB_VIEW)


static gboolean
handle_internal_response (StampWebView           *self,
                          WebKitURISchemeRequest *request)
{
  g_autofree char *path = g_uri_unescape_string (webkit_uri_scheme_request_get_path (request), NULL);
  GInputStream *stream = g_hash_table_lookup (self->internal_resources, path);

  g_print ("%s: Search for %p '%s'\n", G_STRFUNC, self->internal_resources, path);
  g_print ("%s: stream %p\n", G_STRFUNC, stream);
  if (stream) {
    g_print ("%s: RETURN CID\n", G_STRFUNC);
    webkit_uri_scheme_request_finish (request, stream, -1, NULL);
    return TRUE;
  }

  return FALSE;
}

static void
on_cid_request (WebKitURISchemeRequest *request,
                gpointer                user_data)
{
  StampWebView *self = STAMP_WEB_VIEW (user_data);

  if (!handle_internal_response (self, request)) {
    GError *error = g_error_new_literal (g_quark_from_string ("Stamp"), 1, "FAILED");
    webkit_uri_scheme_request_finish_error (request, error);
  }

  g_print ("%s: SUCCESS\n", G_STRFUNC);

  /* g_autofree char *uri = g_uri_unescape_string (webkit_uri_scheme_request_get_uri (request), NULL); */
  /* StampWebView *self = STAMP_WEB_VIEW (user_data); */
  /* GInputStream *stream; */

  /* g_print ("%s: ir %p\n", G_STRFUNC, self->internal_resources); */

  /* stream = g_hash_table_lookup (self->internal_resources, uri); */
  /* if (!stream) { */
  /*   g_warning ("Unknown CID `%s`\n", uri); */
    {
      GList *keys = g_hash_table_get_keys (self->internal_resources);
      for (GList *iter = keys; iter; iter = iter->next) {
        g_print ("--> %s\n", (char*) iter->data);
      }
    }
  /*   return; */
  /* } */

  /* webkit_uri_scheme_request_finish (request, stream, -1, NULL); */
}

static void
on_send_message_to_page (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  StampWebView *self = STAMP_WEB_VIEW (user_data);
  g_autoptr (GError) error = NULL;
  WebKitUserMessage *response;
  GVariant *variant;

  g_print ("%s: ENTER\n", G_STRFUNC);
  response = webkit_web_view_send_message_to_page_finish (WEBKIT_WEB_VIEW (source), res, &error);
  if (error) {
    g_warning ("Could not send message to page: %s", error->message);
    return;
  }

  variant = webkit_user_message_get_parameters (response);
  self->height_request = g_variant_get_int32 (variant);
  g_print ("%s: height_request %d\n", G_STRFUNC, self->height_request);
  gtk_widget_set_size_request (GTK_WIDGET (self), -1, self->height_request);
}

static void
on_load_changed (WebKitWebView   *web_view,
                 WebKitLoadEvent  load_event,
                 gpointer         user_data)
{
  StampWebView *self = STAMP_WEB_VIEW (user_data);

  if (load_event == WEBKIT_LOAD_FINISHED || load_event == WEBKIT_LOAD_COMMITTED) {
    WebKitUserMessage *message = webkit_user_message_new ("get-page-height", NULL);

    g_print ("%s: Send page height request %p / %p\n", G_STRFUNC, web_view, WEBKIT_WEB_VIEW (self));
    g_print ("%s: page id: %ld\n", G_STRFUNC, webkit_web_view_get_page_id (web_view));
    webkit_web_view_send_message_to_page (web_view, message, NULL, on_send_message_to_page, self);
  }

  if (load_event == WEBKIT_LOAD_FINISHED) {

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

      g_app_info_launch_default_for_uri (uri, NULL, NULL);
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
                                   StampWebView   *self)
{
  g_print ("%s: %s\n", G_STRFUNC, STAMP_WEB_PROCESS_EXTENSIONS_DIR);
  webkit_web_context_set_web_process_extensions_directory (web_context, STAMP_WEB_PROCESS_EXTENSIONS_DIR);
  webkit_web_context_set_web_process_extensions_initialization_user_data (web_context, g_variant_new_int32 (0));
}

static GObject *
stamp_webview_constructor (GType                  type,
                           guint                  n_construct_properties,
                           GObjectConstructParam *construct_properties)
{
  GObjectClass * parent_class = G_OBJECT_CLASS (stamp_webview_parent_class);
  GObject *object = parent_class->constructor (type, n_construct_properties, construct_properties);
  StampWebView *self = STAMP_WEB_VIEW (object);
  WebKitSettings *settings;
  WebKitWebContext *web_context;
  static gboolean registered = FALSE;

  self->internal_resources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  g_print ("%s: %p\n", G_STRFUNC, self->internal_resources);

  settings = webkit_settings_new_with_settings ("allow-modal-dialogs", FALSE,
                                                "enable-fullscreen", FALSE,
                                                "enable-html5-database", FALSE,
                                                "enable-html5-local-storage", FALSE,
                                                "enable-javascript", FALSE,
                                                "enable-media-stream", FALSE,
                                                "enable-offline-web-application-cache", FALSE,
                                                "enable-page-cache", FALSE,
                                                "enable-write-console-messages-to-stdout", TRUE,
                                                NULL);

  webkit_web_view_set_settings (WEBKIT_WEB_VIEW (self), settings);

  web_context = webkit_web_context_get_default ();
  g_signal_connect_object (web_context, "initialize-web-process-extensions",
                           G_CALLBACK (initialize_web_process_extensions),
                           self, 0);

  if (!registered) {
    webkit_web_context_register_uri_scheme (web_context, "cid", on_cid_request, g_object_ref (self), g_object_unref);
    registered = TRUE;
  }

  g_signal_connect_object (G_OBJECT (self), "load-changed", G_CALLBACK (on_load_changed), self, 0);
  g_signal_connect_object (G_OBJECT (self), "decide-policy", G_CALLBACK (on_decide_policy), self, 0);
  /* g_signal_connect (self->webview, "mouse-target-changed", G_CALLBACK (on_mouse_target_changed), self); */

  return object;
}

static void
stamp_webview_class_init (StampWebViewClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructor = stamp_webview_constructor;
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
  webkit_web_view_load_html (WEBKIT_WEB_VIEW (self), content, NULL);
}

void
stamp_webview_load_plain_text (StampWebView *self,
                               char         *content)
{
  webkit_web_view_load_plain_text (WEBKIT_WEB_VIEW (self), content);
}

void
stamp_webview_add_internal_resource (StampWebView *self,
                                     const char   *id,
                                     GInputStream *stream)
{
  g_print ("%s: %p '%s'\n", G_STRFUNC, self->internal_resources, id);
  g_hash_table_insert (self->internal_resources, g_strdup (id), g_object_ref (stream));
}
