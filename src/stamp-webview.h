#include <gtk/gtk.h>

#include <webkit/webkit.h>

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_WEBVIEW (stamp_webview_get_type ())

G_DECLARE_FINAL_TYPE (StampWebView, stamp_webview, STAMP, WEB_VIEW, WebKitWebView);

StampWebView *
stamp_webview_new (void);

void
stamp_webview_load_html (StampWebView *self,
                         char         *content);

void
stamp_webview_add_internal_resource (StampWebView *self,
                                     const char   *id,
                                     GInputStream *stream);

void
stamp_webview_load_plain_text (StampWebView *self,
                               char         *content);

G_END_DECLS

