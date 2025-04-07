#pragma once

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define STAMP_PREFS_SCHEMA  "org.tabos.stamp"

#define STAMP_PREFS_STATE_WINDOW_SIZE            "window-size"
#define STAMP_PREFS_STATE_IS_MAXIMIZED           "is-maximized"

#define STAMP_SETTINGS stamp_settings_get (STAMP_PREFS_SCHEMA)

GSettings *stamp_settings_get (const char *schema);

G_END_DECLS

