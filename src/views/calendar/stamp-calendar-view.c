#include "stamp-calendar-view.h"

#include "stamp-account.h"

struct _StampCalendarView {
  GtkWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (StampCalendarView, stamp_calendar_view, ADW_TYPE_BREAKPOINT_BIN);

void
stamp_calendar_view_class_init (StampCalendarViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/calendar/stamp-calendar-view.ui");
}

void
stamp_calendar_view_init (StampCalendarView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
stamp_calendar_view_new (void)
{
  return g_object_new (STAMP_TYPE_CALENDAR_VIEW, NULL);
}
