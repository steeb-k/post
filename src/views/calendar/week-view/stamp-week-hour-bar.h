#include <adwaita.h>
#include <gtk/gtk.h>

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_WEEK_HOUR_BAR (stamp_week_hour_bar_get_type ())

G_DECLARE_FINAL_TYPE (StampWeekHourBar, stamp_week_hour_bar, STAMP, WEEK_HOUR_BAR, GtkBox);

G_END_DECLS

