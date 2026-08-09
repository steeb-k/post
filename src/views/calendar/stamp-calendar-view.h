#include <adwaita.h>
#include <gtk/gtk.h>

#include "gcal-event.h"
#include "stamp-window.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_CALENDAR_VIEW (stamp_calendar_view_get_type ())

G_DECLARE_FINAL_TYPE (StampCalendarView, stamp_calendar_view, STAMP, CALENDAR_VIEW, AdwBreakpointBin);

GtkWidget *
stamp_calendar_view_new (void);

GtkWidget *
stamp_calendar_view_get_sidebar (StampCalendarView *self);

GtkWidget *
stamp_calendar_view_get_primary_action (StampCalendarView *self);

void
stamp_calendar_view_create_event (StampCalendarView *self,
                                  const gchar       *summary,
                                  const gchar       *description);

void
stamp_calendar_view_show_event (StampCalendarView *self,
                                GcalEvent         *event);

G_END_DECLS

