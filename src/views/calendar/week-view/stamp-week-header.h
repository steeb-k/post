#include <adwaita.h>
#include <gtk/gtk.h>

#include "stamp-window.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_WEEK_HEADER (stamp_week_header_get_type ())

G_DECLARE_FINAL_TYPE (StampWeekHeader, stamp_week_header, STAMP, WEEK_HEADER, GtkWidget);

#define stamp_clear_date_time(dt) g_clear_pointer (dt, g_date_time_unref)

void
stamp_week_view_common_snapshot_hour_lines (GtkWidget      *widget,
                                           GtkSnapshot    *snapshot,
                                           GtkOrientation  orientation,
                                           gint            width,
                                           gint            height);

GDateTime*
stamp_date_time_get_start_of_week (GDateTime *date);

gboolean
stamp_set_date_time (GDateTime **dest,
                    GDateTime  *src);

G_END_DECLS

