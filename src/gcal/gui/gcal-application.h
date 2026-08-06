/*
 * gcal-application.h
 *
 * Post: stub replacing GNOME Calendar's GcalApplication.
 *
 * The vendored sources only ever use the application singleton to reach the
 * GcalContext (gcal_application_get_context (GCAL_DEFAULT_APPLICATION)).
 * Post has its own application class, so this header aliases GcalApplication
 * to plain GApplication and serves the context from gcal-global instead,
 * leaving every vendored call site untouched.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

/* adwaita.h was included by the real gcal-application.h; several vendored
 * files rely on getting it from here */
#include <adwaita.h>
#include <gio/gio.h>

#include "gcal-context.h"
#include "gcal-global.h"

G_BEGIN_DECLS

typedef GApplication GcalApplication;

#define GCAL_APPLICATION(ptr) ((GcalApplication *) (ptr))

static inline GcalContext *
gcal_application_get_context (GcalApplication *self)
{
  (void) self;

  return gcal_get_default_context ();
}

G_END_DECLS
