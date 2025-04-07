#include "extension.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

static gboolean
on_user_message_received (WebKitWebPage     *page,
                          WebKitUserMessage *message)
{
  const char *name = webkit_user_message_get_name (message);
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  WebKitFrame *frame = webkit_web_page_get_main_frame (page);
  G_GNUC_END_IGNORE_DEPRECATIONS
  JSCContext *jsc_context = webkit_frame_get_js_context (frame);

  if (g_strcmp0 (name, "get-page-height") == 0) {
    g_autoptr (JSCValue) value = NULL;
    WebKitUserMessage *reply;

    value = jsc_context_evaluate (jsc_context,
                                  "Math.max(document.body.scrollHeight, document.body.offsetHeight, document.documentElement.clientHeight, document.documentElement.scrollHeight, document.documentElement.offsetHeight);", -1);
    reply = webkit_user_message_new ("get-page-height", g_variant_new_int32 (jsc_value_to_int32 (value)));
    webkit_user_message_send_reply (message, reply);
  } else {
    g_warning ("Unhandled page message: %s", name);
    return FALSE;
  }

  return TRUE;
}

static void
on_page_created (WebKitWebProcessExtension *extension,
                 WebKitWebPage      *page,
                 gpointer            user_data)
{
  g_print ("%s: ENTER %ld\n", G_STRFUNC, webkit_web_page_get_id (page));
  g_signal_connect (G_OBJECT (page), "user-message-received", G_CALLBACK (on_user_message_received), NULL);
  /* g_object_add_weak_pointer (GObject *object, gpointer *weak_pointer_location) */
}

G_MODULE_EXPORT void
webkit_web_process_extension_initialize (WebKitWebProcessExtension *webkit_extension)
{
  g_print ("%s: ENTER\n", G_STRFUNC);
  g_object_ref (webkit_extension);
  g_signal_connect (G_OBJECT (webkit_extension), "page-created", G_CALLBACK (on_page_created), NULL);
}

G_MODULE_EXPORT void
webkit_web_process_extension_initialize_with_user_data (WebKitWebProcessExtension *webkit_extension,
                                                        GVariant                  *var)
{
  g_print ("%s: ENTER\n", G_STRFUNC);
  g_signal_connect (G_OBJECT (webkit_extension), "page-created", G_CALLBACK (on_page_created), NULL);
}

static void __attribute__((destructor))
stamp_web_process_extension_shutdown (void)
{
}

#pragma GCC diagnostic pop

