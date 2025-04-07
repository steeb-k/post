#include <config.h>

#include "stamp-settings.h"

#include <glib.h>
#include <gio/gio.h>

static GSettings *gsettings = NULL;

GSettings *
stamp_settings_get (const char *schema)
{
  if (gsettings)
    return gsettings;

  gsettings = g_settings_new (schema);
  if (gsettings == NULL)
    g_warning ("Invalid schema %s requested", schema);

  return gsettings;
}
