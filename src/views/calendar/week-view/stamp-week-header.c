/*
 * Copyright 2025-2026 Jan-Michael Brummer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stamp-week-header.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

typedef struct {
  GtkWidget *day_number_label;
  GtkWidget *weekday_name_label;
} WeekdayHeader;

struct _StampWeekHeader {
  GtkWidget parent_instance;

  GtkWidget *main_box;
  GtkGrid *grid;
  GtkBox *weekdays_box;

  WeekdayHeader weekday_header[7];

  GDateTime *active_date;
};

G_DEFINE_FINAL_TYPE (StampWeekHeader, stamp_week_header, GTK_TYPE_WIDGET);

#define ALIGNED(x)      (round (x) + 0.5)

static void
stamp_week_header_init (StampWeekHeader *self)
{
}

static gboolean
read_first_weekday_from_portal (gint *out_first_weekday)
{
  static gsize first_weekday_init = FALSE;
  static gsize first_weekday = G_MAXSIZE;

  if (g_once_init_enter (&first_weekday_init)){
    g_autoptr (GDBusProxy) settings_portal = NULL;
    g_autoptr (GVariant) result = NULL;
    g_autoptr (GVariant) aux2 = NULL;
    g_autoptr (GVariant) aux = NULL;
    g_autoptr (GError) error = NULL;
    const gchar *day;

    /*
     * If the value is not "default" then there is a user preference
     * override, in that case skip the autodetection dance.
     */
    static const gchar *portal_weekdays[] = {
      "sunday",
      "monday",
      "tuesday",
      "wednesday",
      "thursday",
      "friday",
      "saturday",
    };

    settings_portal = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                     G_DBUS_PROXY_FLAGS_NONE,
                                                     NULL,
                                                     "org.freedesktop.portal.Desktop",
                                                     "/org/freedesktop/portal/desktop",
                                                     "org.freedesktop.portal.Settings",
                                                     NULL,
                                                     &error);

    if (error){
      g_warning ("Failed to load portals: %s. Aborting...", error->message);
      goto out;
    }

    result = g_dbus_proxy_call_sync (settings_portal,
                                     "ReadOne",
                                     g_variant_new ("(ss)", "org.gnome.desktop.calendar", "week-start-day"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     G_MAXINT,
                                     NULL,
                                     &error);

    if (error){
      g_warning ("Failed to load first weekday settings: %s", error->message);
      goto out;
    }

    aux = g_variant_get_child_value (result, 0);
    aux2 = g_variant_get_variant (aux);
    day = g_variant_get_string (aux2, NULL);

    g_debug ("Setting 'week-start-day' is currently: %s", day);

    for (int i = 0; i < G_N_ELEMENTS (portal_weekdays); i++){
      if (g_strcmp0 (day, portal_weekdays[i]) == 0){
        first_weekday = i;
        break;
      }
    }

out:
    g_once_init_leave (&first_weekday_init, TRUE);
  }

  if (first_weekday != G_MAXSIZE && out_first_weekday)
    *out_first_weekday = first_weekday;

  return first_weekday != G_MAXSIZE;
}


gint
get_first_weekday (void)
{
  gint week_start;

  if (read_first_weekday_from_portal (&week_start))
    return week_start;

#ifdef HAVE__NL_TIME_FIRST_WEEKDAY
  union {
    unsigned int word;
    char *string;
  } langinfo;
  gint week_1stday = 0;
  gint first_weekday = 1;
  guint week_origin;

  langinfo.string = nl_langinfo (_NL_TIME_FIRST_WEEKDAY);
  first_weekday = langinfo.string[0];
  langinfo.string = nl_langinfo (_NL_TIME_WEEK_1STDAY);
  week_origin = langinfo.word;
  if (week_origin == 19971130) /* Sunday */
    week_1stday = 0;
  else if (week_origin == 19971201) /* Monday */
    week_1stday = 1;
  else
    g_warning ("Unknown value of _NL_TIME_WEEK_1STDAY.\n");

  week_start = (week_1stday + first_weekday - 1) % 7;
#else
  gchar *gtk_week_start;


  /* Use a define to hide the string from xgettext */
# define GTK_WEEK_START "calendar:week_start:0"
  gtk_week_start = dgettext ("gtk40", GTK_WEEK_START);

  if (g_str_has_prefix (gtk_week_start, "calendar:week_start:"))
    week_start = *(gtk_week_start + 20) - '0';
  else
    week_start = -1;

  if (week_start < 0 || week_start > 6){
    g_warning ("Whoever translated calendar:week_start:0 for GTK+ "
               "did so wrongly.\n");
    week_start = 0;
  }
#endif

  return week_start;
}


GDateTime *
stamp_date_time_get_start_of_week (GDateTime *date)
{
  g_autoptr (GDateTime) start_of_week = NULL;
  gint n_days_after_week_start;
  gint first_weekday;
  gint weekday;

  g_assert (date != NULL);

  first_weekday = get_first_weekday ();
  weekday = g_date_time_get_day_of_week (date) % 7;
  n_days_after_week_start = (7 + weekday - first_weekday) % 7;

  start_of_week = g_date_time_add_days (date, -n_days_after_week_start);

  return g_date_time_new_local (g_date_time_get_year (start_of_week),
                                g_date_time_get_month (start_of_week),
                                g_date_time_get_day_of_month (start_of_week),
                                0, 0, 0);
}


static inline gint
get_today_column (StampWeekHeader *self)
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


static void
update_title (StampWeekHeader *self)
{
  g_autoptr (GDateTime) week_start = NULL;
  gint today_column;

  if(!self->active_date)
    return;

  week_start = stamp_date_time_get_start_of_week (self->active_date);
  today_column = get_today_column (self);

  for (gint i = 0; i < 7; i++){
    g_autoptr (GDateTime) day = NULL;
    g_autofree gchar *weekday_date = NULL;
    g_autofree gchar *weekday_abv = NULL;
    g_autofree gchar *weekday = NULL;
    WeekdayHeader *header;
    gint n_day;

    day = g_date_time_add_days (week_start, i);
    n_day = g_date_time_get_day_of_month (day);

    if (n_day > g_date_get_days_in_month (g_date_time_get_month (week_start), g_date_time_get_year (week_start)))
      n_day = n_day - g_date_get_days_in_month (g_date_time_get_month (week_start), g_date_time_get_year (week_start));

    header = &self->weekday_header[i];

    if (i == today_column){
      gtk_widget_add_css_class (header->weekday_name_label, "accent");
      gtk_widget_add_css_class (header->day_number_label, "accent");
      gtk_widget_remove_css_class (header->weekday_name_label, "dimmed");
      gtk_widget_remove_css_class (header->day_number_label, "dimmed");
    } else {
      gtk_widget_remove_css_class (header->weekday_name_label, "accent");
      gtk_widget_remove_css_class (header->day_number_label, "accent");
      gtk_widget_add_css_class (header->weekday_name_label, "dimmed");
      gtk_widget_add_css_class (header->day_number_label, "dimmed");
    }

    weekday_date = g_strdup_printf ("%d", n_day);
    gtk_label_set_label (GTK_LABEL (header->day_number_label), weekday_date);

    weekday = g_date_time_format (day, "%a");
    weekday_abv = g_utf8_strup (weekday, -1);
    gtk_label_set_label (GTK_LABEL (header->weekday_name_label), weekday_abv);
  }
}

static void
stamp_week_header_constructed (GObject *object)
{
  StampWeekHeader *self = STAMP_WEEK_HEADER (object);
  int i;

  G_OBJECT_CLASS (stamp_week_header_parent_class)->constructed (object);

  gtk_widget_init_template (GTK_WIDGET (self));

  for (i = 0; i < 7; i++) {
    WeekdayHeader *header = &self->weekday_header[i];
    GtkWidget *box;

    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start (box, 6);

    header->weekday_name_label = gtk_label_new ("");
    gtk_widget_set_hexpand (header->weekday_name_label, TRUE);
    gtk_widget_add_css_class (header->weekday_name_label, "heading");
    gtk_widget_add_css_class (header->weekday_name_label, "dimmed");
    gtk_label_set_xalign (GTK_LABEL (header->weekday_name_label), 0.0);
    gtk_box_append (GTK_BOX (box), header->weekday_name_label);

    header->day_number_label = gtk_label_new ("");
    gtk_widget_set_hexpand (header->day_number_label, TRUE);
    gtk_widget_add_css_class (header->day_number_label, "title-2");
    gtk_widget_add_css_class (header->day_number_label, "dimmed");
    gtk_label_set_xalign (GTK_LABEL (header->day_number_label), 0.0);
    gtk_box_append (GTK_BOX (box), header->day_number_label);

    gtk_box_append (self->weekdays_box, box);

    /* Add 7 empty widget to the grid to ensure proper spacing */
    gtk_grid_attach (self->grid, gtk_box_new (GTK_ORIENTATION_VERTICAL, 0), i, 0, 1, 1);
  }

  self->active_date = g_date_time_new_now_local ();
  update_title (self);
}

static void
stamp_week_header_dispose (GObject *object)
{
  /* StampContactDetails *self = STAMP_CONTACT_DETAILS (object); */

  G_OBJECT_CLASS (stamp_week_header_parent_class)->dispose (object);
}

static void
stamp_week_header_measure (GtkWidget      *widget,
                           GtkOrientation  orientation,
                           gint            for_size,
                           gint           *minimum,
                           gint           *natural,
                           gint           *minimum_baseline,
                           gint           *natural_baseline)
{
  StampWeekHeader *self = STAMP_WEEK_HEADER (widget);

  g_assert (STAMP_IS_WEEK_HEADER (self));

  gtk_widget_measure (self->main_box,
                      orientation,
                      for_size,
                      minimum, natural,
                      minimum_baseline,
                      natural_baseline);
}

static void
stamp_week_header_size_allocate (GtkWidget *widget,
                                 gint       width,
                                 gint       height,
                                 gint       baseline)
{
  StampWeekHeader *self = STAMP_WEEK_HEADER (widget);
  gboolean ltr;
  gdouble cell_width;

  g_assert (STAMP_IS_WEEK_HEADER (self));

  gtk_widget_allocate (self->main_box, width, height, baseline, NULL);

  ltr = gtk_widget_get_direction (widget) != GTK_TEXT_DIR_RTL;
  cell_width = width / 7.0;
}

void
stamp_week_view_common_snapshot_hour_lines (GtkWidget      *widget,
                                            GtkSnapshot    *snapshot,
                                            GtkOrientation  orientation,
                                            gint            width,
                                            gint            height)
{
  GtkStyleContext *context;
  GdkRGBA color;
  gboolean ltr;
  gdouble column_width;
  guint i;

  ltr = gtk_widget_get_direction (widget) != GTK_TEXT_DIR_RTL;

  context = gtk_widget_get_style_context (widget);
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "lines");
  gtk_style_context_get_color (context, &color);
  gtk_style_context_restore (context);

  column_width = width / 7.0;

  switch (orientation){
    case GTK_ORIENTATION_HORIZONTAL:
      for (i = 0; i < 7; i++){
        gdouble x;

        if (ltr)
          x = column_width * i;
        else
          x = width - column_width * i;

        gtk_snapshot_append_color (snapshot,
                                   &color,
                                   &GRAPHENE_RECT_INIT (x, 0.f, 1.0, height));
      }
      break;


    case GTK_ORIENTATION_VERTICAL:
      /* Main lines */
      for (i = 1; i < 24; i++){
        gtk_snapshot_append_color (snapshot,
                                   &color,
                                   &GRAPHENE_RECT_INIT (0.f,
                                                        ALIGNED ((height / 24.0) * i),
                                                        width,
                                                        1.0));
      }

      /* In-between lines */
      color.alpha /= 2.0;
      for (i = 0; i < 24; i++){
        gdouble half_cell_height = (height / 24.0) / 2.0;
        gtk_snapshot_append_color (snapshot,
                                   &color,
                                   &GRAPHENE_RECT_INIT (0.f,
                                                        ALIGNED ((height / 24.0) * i + half_cell_height),
                                                        width,
                                                        1.0));
      }
      break;
  }
}

static void
stamp_week_header_snapshot (GtkWidget   *widget,
                            GtkSnapshot *snapshot)
{
  StampWeekHeader *self;
  gboolean ltr;
  gint height;
  gint width;
  gint x;

  /* Fonts and colour selection */
  self = STAMP_WEEK_HEADER (widget);
  ltr = gtk_widget_get_direction (widget) != GTK_TEXT_DIR_RTL;

  width = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);
  x = ALIGNED (ltr ? 0 : width);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x, 0));
  stamp_week_view_common_snapshot_hour_lines (widget, snapshot, GTK_ORIENTATION_HORIZONTAL, width, height);
  gtk_snapshot_restore (snapshot);

  gtk_widget_snapshot_child (widget, self->main_box, snapshot);
}


static void
stamp_week_header_class_init (StampWeekHeaderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/calendar/week-view/stamp-week-header.ui");

  object_class->constructed = stamp_week_header_constructed;
  object_class->dispose = stamp_week_header_dispose;

  widget_class->measure = stamp_week_header_measure;
  widget_class->size_allocate = stamp_week_header_size_allocate;
  widget_class->snapshot = stamp_week_header_snapshot;

  gtk_widget_class_bind_template_child (widget_class, StampWeekHeader, main_box);
  gtk_widget_class_bind_template_child (widget_class, StampWeekHeader, grid);
  gtk_widget_class_bind_template_child (widget_class, StampWeekHeader, weekdays_box);
}


gboolean
stamp_set_date_time (GDateTime **dest,
                     GDateTime  *src)
{
  if (*dest == src)
    return FALSE;

  stamp_clear_date_time (dest);

  if (src)
    *dest = g_date_time_ref (src);

  return TRUE;
}
