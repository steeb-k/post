#include "stamp-week-hour-bar.h"
#include "stamp-week-header.h"

#include <glib/gi18n.h>

struct _StampWeekHourBar {
  GtkBox parent_instance;

  GtkLabel *labels[24];

  /* StampContext        *context; */
};

G_DEFINE_FINAL_TYPE (StampWeekHourBar, stamp_week_hour_bar, GTK_TYPE_BOX)

typedef enum {
  STAMP_TIME_FORMAT_12H,
  STAMP_TIME_FORMAT_24H,
} StampTimeFormat;


static void
update_labels (StampWeekHourBar *self)
{
  StampTimeFormat time_format;
  gint i;

  time_format = STAMP_TIME_FORMAT_24H; /*stamp_context_get_time_format (self->context); */

  for (i = 0; i < 24; i++){
    g_autofree gchar *hours = NULL;

    if (time_format == STAMP_TIME_FORMAT_24H){
      hours = g_strdup_printf ("%02d:00", i);
    } else {
      hours = g_strdup_printf ("%d %s",
                               i % 12 == 0 ? 12 : i % 12,
                               i >= 12 ? _("PM") : _("AM"));
    }

    gtk_label_set_label (self->labels[i], hours);
  }
}

static void
stamp_week_hour_bar_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  stamp_week_view_common_snapshot_hour_lines (widget,
                                              snapshot,
                                              GTK_ORIENTATION_VERTICAL,
                                              gtk_widget_get_width (widget),
                                              gtk_widget_get_height (widget));

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child;
       child = gtk_widget_get_next_sibling (child)){
    gtk_widget_snapshot_child (widget, child, snapshot);
  }
}

static void
stamp_week_hour_bar_class_init (StampWeekHourBarClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->snapshot = stamp_week_hour_bar_snapshot;

  gtk_widget_class_set_css_name (widget_class, "weekhourbar");
}

static void
stamp_week_hour_bar_init (StampWeekHourBar *self)
{
  gint i;

  g_object_set (self,
                "orientation", GTK_ORIENTATION_VERTICAL,
                "homogeneous", TRUE,
                "spacing", 1,
                NULL);


  for (i = 0; i < 24; i++){
    GtkWidget *label = gtk_label_new ("");
    gtk_widget_add_css_class (label, "dimmed");
    gtk_widget_set_vexpand (label, TRUE);
    gtk_label_set_yalign (GTK_LABEL (label), 0.0);
    gtk_box_append (GTK_BOX (self), label);

    self->labels[i] = GTK_LABEL (label);
  }

  update_labels (self);
}

/* void */
/* stamp_week_hour_bar_set_context (StampWeekHourBar *self, */
/*                                 StampContext     *context) */
/* { */
/*   g_return_if_fail (STAMP_IS_WEEK_HOUR_BAR (self)); */

/*   self->context = context; */

/*   g_signal_connect_object (context, */
/*                            "notify::time-format", */
/*                            G_CALLBACK (update_labels), */
/*                            self, */
/*                            G_CONNECT_SWAPPED); */
/*   update_labels (self); */
/* } */
