#include <adwaita.h>
#include <gtk/gtk.h>

#include "stamp-window.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_CALENDAR_VIEW (stamp_calendar_view_get_type ())

G_DECLARE_FINAL_TYPE (StampCalendarView, stamp_calendar_view, STAMP, CALENDAR_VIEW, AdwBreakpointBin);

GtkWidget *
stamp_calendar_view_new (void);

GtkWidget *
stamp_calendar_view_get_sidebar (StampCalendarView *self);

G_END_DECLS

