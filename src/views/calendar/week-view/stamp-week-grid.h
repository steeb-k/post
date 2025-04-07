#include <adwaita.h>
#include <gtk/gtk.h>

#include "stamp-window.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_WEEK_GRID (stamp_week_grid_get_type ())

G_DECLARE_FINAL_TYPE (StampWeekGrid, stamp_week_grid, STAMP, WEEK_GRID, GtkWidget);

G_END_DECLS

