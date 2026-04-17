#include "stamp-range.h"
#include "stamp-range-tree.h"
#include "stamp-week-grid.h"
#include "stamp-week-header.h"

struct _StampWeekGrid {
  GtkWidget parent;

  GtkWidget *hours_sidebar;
  GtkWidget *now_strip;

  GDateTime *active_date;

  StampRangeTree *events;

  /* GcalContext        *context; */
};

typedef struct  {
  char *name;
} StampEvent;

typedef struct {
  GtkWidget *widget;
  StampEvent *event;
} ChildData;


G_DEFINE_FINAL_TYPE (StampWeekGrid, stamp_week_grid, GTK_TYPE_WIDGET);

static void
stamp_week_grid_measure (GtkWidget      *widget,
                         GtkOrientation  orientation,
                         gint            for_size,
                         gint           *minimum,
                         gint           *natural,
                         gint           *minimum_baseline,
                         gint           *natural_baseline)
{
  if (minimum)
    *minimum = 1;
  if (natural)
    *natural = 1;
}

static void
stamp_week_grid_snapshot (GtkWidget   *widget,
                          GtkSnapshot *snapshot)
{
  g_autoptr (GPtrArray) widgets = NULL;
  StampWeekGrid *self;
  gint width, height;

  self = STAMP_WEEK_GRID (widget);
  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  stamp_week_view_common_snapshot_hour_lines (widget, snapshot, GTK_ORIENTATION_HORIZONTAL, width, height);
  stamp_week_view_common_snapshot_hour_lines (widget, snapshot, GTK_ORIENTATION_VERTICAL, width, height);

  /* First, draw the selection */
  /* gtk_widget_snapshot_child (widget, self->selection.widget, snapshot); */
  /* gtk_widget_snapshot_child (widget, self->dnd.widget, snapshot); */

  widgets = stamp_range_tree_get_all_data (self->events);
  for (guint i = 0; i < widgets->len; i++){
    ChildData *child_data = g_ptr_array_index (widgets, i);

    gtk_widget_snapshot_child (widget, child_data->widget, snapshot);
  }

  gtk_widget_snapshot_child (widget, self->now_strip, snapshot);
}

  #define MINUTES_PER_DAY 1440

static inline gint
get_today_column (StampWeekGrid *self)
{
  g_autoptr (GDateTime) week_start = NULL;
  g_autoptr (GDateTime) today = NULL;
  gint days_diff;

  today = g_date_time_new_now_local ();
  week_start = stamp_date_time_get_start_of_week (self->active_date);
  days_diff = g_date_time_difference (today, week_start) / G_TIME_SPAN_DAY;

  /* Today is out of range */
  if (g_date_time_compare (today, week_start) < 0 || days_diff > 7)
    return -1;

  return days_diff;
}


static inline gint
uint16_compare (gconstpointer a,
                gconstpointer b)
{
  return GPOINTER_TO_UINT (*(gint *)a) - GPOINTER_TO_UINT (*(gint *)b);
}

static inline guint
get_event_index (StampRangeTree *tree,
                 StampRange     *range)
{
  g_autoptr (GPtrArray) array = NULL;
  gint idx, i;

  idx = 0;
  array = stamp_range_tree_get_data_at_range (tree, range);

  if (!array)
    return 0;

  g_ptr_array_sort (array, uint16_compare);

  for (i = 0; array && i < array->len; i++){
    if (idx == GPOINTER_TO_INT (g_ptr_array_index (array, i)))
      idx++;
    else
      break;
  }

  return idx;
}

static guint
count_overlaps_at_range (StampRangeTree *self,
                         StampRange     *range)
{
  g_autoptr (GDateTime) start = NULL;
  g_autoptr (GDateTime) end = NULL;
  guint64 counter;
  gint minutes;
  gint i;

  g_return_val_if_fail (self, 0);

  counter = 0;
  start = stamp_range_get_start (range);
  end = stamp_range_get_end (range);
  minutes = g_date_time_difference (end, start) / G_TIME_SPAN_MINUTE;

  for (i = 0; i < minutes; i++){
    g_autoptr (StampRange) minute_range = NULL;
    guint n_events;

    minute_range = stamp_range_new_take (g_date_time_add_minutes (start, i),
                                         g_date_time_add_minutes (start, i + 1),
                                         STAMP_RANGE_DEFAULT);
    n_events = stamp_range_tree_count_entries_at_range (self, minute_range);

    if (n_events == 0)
      break;

    counter = MAX (counter, n_events);
  }

  return counter;
}


static void
stamp_week_grid_size_allocate (GtkWidget *widget,
                               gint       width,
                               gint       height,
                               gint       baseline)
{
  StampWeekGrid *self = STAMP_WEEK_GRID (widget);
  g_autoptr (GDateTime) week_start = NULL;
  StampRangeTree *overlaps;
  gboolean ltr;
  gdouble minutes_height;
  gdouble column_width;
  gint today_column;

  ltr = gtk_widget_get_direction (widget) != GTK_TEXT_DIR_RTL;

  /* Preliminary calculations */
  minutes_height = (gdouble)height / MINUTES_PER_DAY;
  column_width = (gdouble)width / 7.0;

  /* Temporary range tree to hold positioned events' indexes */
  overlaps = stamp_range_tree_new ();

  week_start = stamp_date_time_get_start_of_week (self->active_date);

  /*
   * Iterate through weekdays; we don't have to worry about events that
   * jump between days because they're already handled by GcalWeekHeader.
   */
  for (size_t i = 0; i < 7; i++){
    g_autoptr (StampRange) day_range = NULL;
    GPtrArray *widgets_data;
    guint j;

    day_range = stamp_range_new_take (g_date_time_add_days (week_start, i),
                                      g_date_time_add_days (week_start, i + 1),
                                      STAMP_RANGE_DEFAULT);
    widgets_data = stamp_range_tree_get_data_at_range (self->events, day_range);
    g_print ("%s: %ld widgets_data %p\n", G_STRFUNC, i, widgets_data);

    for (j = 0; widgets_data && j < widgets_data->len; j++){
      g_autoptr (GDateTime) event_start = NULL;
      g_autoptr (GDateTime) event_end = NULL;
      GtkAllocation child_allocation;
      GtkWidget *event_widget;
      StampRange *event_range;
      ChildData *data;
      guint64 events_at_range;
      gint event_minutes;
      gint minimum_width;
      gint minimum_height;
      gint widget_index;
      gint offset;
      gint event_height;
      gint event_width;
      gint x;
      gint y;
      GDateTime *start;
      GDateTime *end;

      data = g_ptr_array_index (widgets_data, j);
      event_widget = data->widget;

      if (!gtk_widget_should_layout (event_widget))
        continue;

      /* event_range = stamp_event_get_range (data->event); */
      /* event_start = g_date_time_to_local (stamp_event_get_date_start (data->event)); */
      /* event_end = g_date_time_to_local (stamp_event_get_date_end (data->event)); */
      start = g_date_time_new (g_time_zone_new_local (), 2026, 02, 04, 17, 00, 00);
      end = g_date_time_new (g_time_zone_new_local (), 2026, 02, 04, 18, 00, 00);
      event_range = stamp_range_new (start, end, STAMP_RANGE_DEFAULT);
      event_start = start;
      event_end = end;

      /* The total number of events available in this range */
      events_at_range = count_overlaps_at_range (self->events, event_range);

      /* The real horizontal position of this event */
      widget_index = get_event_index (overlaps, event_range);

      event_minutes = g_date_time_difference (event_end, event_start) / G_TIME_SPAN_MINUTE;

      /* Compute the height of the widget */
      gtk_widget_measure (event_widget, GTK_ORIENTATION_VERTICAL, -1, &minimum_height, NULL, NULL, NULL);
      event_height = event_minutes * minutes_height;
      event_height = MAX (minimum_height, event_height);

      /* Compute the width of the widget */
      gtk_widget_measure (event_widget, GTK_ORIENTATION_HORIZONTAL, event_height, &minimum_width, NULL, NULL, NULL);
      event_width = column_width / events_at_range;
      event_width = MAX (minimum_width, event_width);

      offset = event_width * widget_index;
      y = (g_date_time_get_hour (event_start) * 60 + g_date_time_get_minute (event_start)) * minutes_height;

      if (ltr)
        x = column_width * i + offset + 1;
      else
        x = width - event_width - (column_width * i + offset + 1);

      /* Setup the child position and size */
      child_allocation.x = x;
      child_allocation.y = y;
      child_allocation.width = event_width;
      child_allocation.height = event_height;

      gtk_widget_size_allocate (event_widget, &child_allocation, baseline);

      /* Add the current event to the temporary overlaps tree so we have a way to */
      /* know how many events are already positioned in the current column. */

      stamp_range_tree_add_range (overlaps, event_range, GUINT_TO_POINTER (widget_index));
    }

    g_clear_pointer (&widgets_data, g_ptr_array_unref);
  }

  /* g_clear_pointer (&overlaps, stamp_range_tree_unref); */

  /* Today column */
  today_column = get_today_column (STAMP_WEEK_GRID (widget));

  gtk_widget_set_child_visible (self->now_strip, today_column != -1);

  if (today_column != -1){
    g_autoptr (GDateTime) now = NULL;
    GtkAllocation allocation;
    guint minutes_from_midnight;
    gint now_strip_height;
    gint x;

    now = g_date_time_new_now_local ();
    minutes_from_midnight = g_date_time_get_hour (now) * 60 + g_date_time_get_minute (now);

    gtk_widget_measure (self->now_strip,
                        GTK_ORIENTATION_VERTICAL,
                        -1,
                        &now_strip_height,
                        NULL,
                        NULL,
                        NULL);

    if (ltr)
      x = today_column * column_width;
    else
      x = width - (today_column * column_width) - column_width;

    allocation.x = x;
    allocation.y = round (minutes_from_midnight * ((gdouble)height / MINUTES_PER_DAY));
    allocation.width = column_width;
    allocation.height = MAX (1, now_strip_height);

    gtk_widget_size_allocate (self->now_strip, &allocation, baseline);
  }
}

static ChildData *
child_data_new (GtkWidget  *widget,
                StampEvent *event)
{
  ChildData *data;

  data = g_new (ChildData, 1);
  data->widget = widget;
  /* data->event = g_object_ref (event); */
  data->event = event;

  return data;
}


void
stamp_week_grid_add_event (StampWeekGrid *self,
                           StampEvent    *event)
{
  GtkWidget *widget;
  GDateTime *start = g_date_time_new (g_time_zone_new_local (), 2026, 02, 25, 13, 00, 00);
  GDateTime *end = g_date_time_new (g_time_zone_new_local (), 2026, 02, 25, 14, 00, 00);
  StampRange *event_range = stamp_range_new (start, end, STAMP_RANGE_DEFAULT);

  g_return_if_fail (STAMP_IS_WEEK_GRID (self));

  /* g_object_ref (event); */

  /* widget = g_object_new (STAMP_TYPE_EVENT_WIDGET, */
  /*                        "context", self->context, */
  /*                        "event", event, */
  /*                        "orientation", GTK_ORIENTATION_VERTICAL, */
  /*                        "timestamp-policy", GCAL_TIMESTAMP_POLICY_START, */
  /*                        NULL); */
  widget = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_hexpand (widget, TRUE);
  gtk_box_append (GTK_BOX (widget), gtk_label_new ("Test Design Review"));
  gtk_widget_add_css_class (widget, "event");

  stamp_range_tree_add_range (self->events,
                              event_range, /*stamp_event_get_range (event), */
                              child_data_new (widget, event));

  /* g_signal_connect (widget, "activate", G_CALLBACK (on_event_widget_activated_cb), self); */

  gtk_widget_set_parent (widget, GTK_WIDGET (self));
}



static void
stamp_week_grid_class_init (StampWeekGridClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  /* GObjectClass *object_class = G_OBJECT_CLASS (klass); */

  /* object_class->dispose = stamp_week_grid_dispose; */
  /* object_class->finalize = stamp_week_grid_finalize; */
  /* object_class->get_property = gcal_week_grid_get_property; */
  /* object_class->set_property = gcal_week_grid_set_property; */

  widget_class->measure = stamp_week_grid_measure;
  widget_class->size_allocate = stamp_week_grid_size_allocate;
  widget_class->snapshot = stamp_week_grid_snapshot;

  gtk_widget_class_set_css_name (widget_class, "weekgrid");
}

static void
child_data_free (gpointer data)
{
  ChildData *child_data = data;

  if (!child_data)
    return;

  g_clear_pointer (&child_data->widget, gtk_widget_unparent);
  g_clear_object (&child_data->event);
  g_free (child_data);
}

static void
stamp_week_grid_init (StampWeekGrid *self)
{
  GtkDropTarget *drop_target;
  GtkGesture *click_gesture;
  StampEvent *event = g_new0 (StampEvent, 1);

  self->events = stamp_range_tree_new_with_free_func (child_data_free);

  self->now_strip = adw_bin_new ();
  gtk_widget_add_css_class (self->now_strip, "now-strip");
  gtk_widget_set_can_target (self->now_strip, FALSE);
  gtk_widget_set_parent (self->now_strip, GTK_WIDGET (self));

  /* click_gesture = gtk_gesture_click_new (); */
  /* gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click_gesture), GDK_BUTTON_PRIMARY); */
  /* g_signal_connect (click_gesture, "pressed", G_CALLBACK (on_click_gesture_pressed_cb), self); */
  /* g_signal_connect (click_gesture, "released", G_CALLBACK (on_click_gesture_released_cb), self); */
  /* gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (click_gesture)); */

  /* self->motion_controller = gtk_event_controller_motion_new (); */
  /* g_signal_connect (self->motion_controller, "motion", G_CALLBACK (on_motion_controller_motion_cb), self); */
  /* gtk_event_controller_set_propagation_phase (self->motion_controller, GTK_PHASE_NONE); */
  /* gtk_widget_add_controller (GTK_WIDGET (self), self->motion_controller); */

  /* drop_target = gtk_drop_target_new (GCAL_TYPE_EVENT_WIDGET, GDK_ACTION_COPY); */
  /* gtk_drop_target_set_preload (drop_target, TRUE); */
  /* g_signal_connect (drop_target, "drop", G_CALLBACK (on_drop_target_drop_cb), self); */
  /* g_signal_connect (drop_target, "enter", G_CALLBACK (on_drop_target_enter_cb), self); */
  /* g_signal_connect (drop_target, "leave", G_CALLBACK (on_drop_target_leave_cb), self); */
  /* g_signal_connect (drop_target, "motion", G_CALLBACK (on_drop_target_motion_cb), self); */
  /* gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drop_target)); */

  self->active_date = g_date_time_new_now_local ();

  stamp_week_grid_add_event (self, event);
}
