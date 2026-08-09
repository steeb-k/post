/*
 * Copyright 2026 steeb-k
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

/*
 * The calendar list that GNOME Calendar keeps in its own management
 * dialog, rehomed as a page of Post's preferences. The rows and the
 * edit/new subpages are the vendored gcal widgets; only the navigation
 * differs -- gcal pushes onto an AdwNavigationView of its own, we push
 * onto the preferences dialog.
 */

#include "stamp-preferences-calendars.h"

#include <glib/gi18n.h>

#include "gcal-calendar.h"
#include "gcal-calendar-management-page.h"
#include "gcal-context.h"
#include "gcal-edit-calendar-page.h"
#include "gcal-manager.h"
#include "gcal-new-calendar-page.h"
#include "gcal-utils.h"

#include "stamp-gcal.h"

struct _StampPreferencesCalendars {
  AdwPreferencesPage parent_instance;

  GtkListBox *listbox;

  /* The calendar whose removal the undo toast is still holding back */
  AdwToast *toast;
};

G_DEFINE_FINAL_TYPE (StampPreferencesCalendars, stamp_preferences_calendars, ADW_TYPE_PREFERENCES_PAGE);

static void add_calendar (StampPreferencesCalendars *self,
                          GcalCalendar              *calendar);

static AdwPreferencesDialog *
get_dialog (StampPreferencesCalendars *self)
{
  GtkWidget *ancestor = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_PREFERENCES_DIALOG);

  return ancestor ? ADW_PREFERENCES_DIALOG (ancestor) : NULL;
}

static void
on_calendar_color_changed_cb (GcalCalendar *calendar,
                              GParamSpec   *pspec,
                              GtkImage     *icon)
{
  g_autoptr (GdkPaintable) color_paintable = NULL;

  color_paintable = get_circle_paintable_from_color (gcal_calendar_get_color (calendar), 24);
  gtk_image_set_from_paintable (icon, color_paintable);
}

static GtkWidget *
make_calendar_row (StampPreferencesCalendars *self,
                   GcalCalendar              *calendar)
{
  g_autoptr (GdkPaintable) color_paintable = NULL;
  g_autoptr (GtkBuilder) builder = NULL;
  g_autofree gchar *parent_name = NULL;
  GtkWidget *read_only_icon;
  GtkWidget *icon;
  GtkWidget *row;
  GtkWidget *sw;

  parent_name = e_source_dup_display_name (gcal_calendar_get_parent_source (calendar));

  builder = gtk_builder_new_from_resource ("/org/gnome/calendar/ui/gui/calendar-management/calendar-row.ui");

  /* Referenced here so it outlives the builder */
  row = g_object_ref (GTK_WIDGET (gtk_builder_get_object (builder, "row")));

  read_only_icon = GTK_WIDGET (gtk_builder_get_object (builder, "read_only_icon"));
  gtk_widget_set_visible (read_only_icon, gcal_calendar_is_read_only (calendar));

  icon = GTK_WIDGET (gtk_builder_get_object (builder, "icon"));
  color_paintable = get_circle_paintable_from_color (gcal_calendar_get_color (calendar), 24);
  gtk_image_set_from_paintable (GTK_IMAGE (icon), color_paintable);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), gcal_calendar_get_name (calendar));
  g_object_bind_property (calendar, "name", row, "title", G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_signal_connect_object (calendar,
                           "notify::name",
                           G_CALLBACK (gtk_list_box_invalidate_sort),
                           self->listbox,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (calendar,
                           "notify::color",
                           G_CALLBACK (on_calendar_color_changed_cb),
                           icon,
                           0);

  sw = GTK_WIDGET (gtk_builder_get_object (builder, "switch"));
  g_object_bind_property (calendar, "visible", sw, "active", G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), parent_name);

  return row;
}

static void
add_calendar (StampPreferencesCalendars *self,
              GcalCalendar              *calendar)
{
  GtkWidget *child;
  GtkWidget *row;

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->listbox));
       child != NULL;
       child = gtk_widget_get_next_sibling (child)) {
    if (g_object_get_data (G_OBJECT (child), "calendar") == calendar)
      return;
  }

  row = make_calendar_row (self, calendar);
  g_object_set_data (G_OBJECT (row), "source", gcal_calendar_get_source (calendar));
  g_object_set_data (G_OBJECT (row), "calendar", calendar);
  gtk_list_box_append (self->listbox, row);
}

static void
remove_calendar (StampPreferencesCalendars *self,
                 GcalCalendar              *calendar)
{
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->listbox));
       child != NULL;
       child = gtk_widget_get_next_sibling (child)) {
    if (g_object_get_data (G_OBJECT (child), "calendar") == calendar) {
      gtk_list_box_remove (self->listbox, child);
      break;
    }
  }
}

static void
delete_calendar (StampPreferencesCalendars *self,
                 GcalCalendar              *calendar)
{
  g_autoptr (GError) error = NULL;
  ESource *removed_source;

  g_assert (calendar != NULL);

  removed_source = gcal_calendar_get_source (calendar);

  /* We don't really want to remove non-removable sources */
  if (!e_source_get_removable (removed_source))
    return;

  /* Enable the source again to remove its name from the disabled list */
  gcal_calendar_set_visible (calendar, TRUE);
  e_source_remove_sync (removed_source, NULL, &error);

  if (error != NULL) {
    g_warning ("Error removing calendar source: %s", error->message);
    add_calendar (self, calendar);
  }
}

static void
clear_toast_and_delete_calendar (StampPreferencesCalendars *self,
                                 AdwToast                  *toast)
{
  GcalCalendar *calendar = g_object_get_data (G_OBJECT (toast), "calendar");

  if (!calendar) {
    g_assert (self->toast == NULL);
    return;
  }

  delete_calendar (self, calendar);

  g_clear_object (&self->toast);
}

static gint
listbox_sort_func (GtkListBoxRow *row1,
                   GtkListBoxRow *row2,
                   gpointer       user_data)
{
  GcalCalendar *calendar1 = g_object_get_data (G_OBJECT (row1), "calendar");
  GcalCalendar *calendar2 = g_object_get_data (G_OBJECT (row2), "calendar");
  const gchar *parent_name1;
  const gchar *parent_name2;
  gint retval;

  retval = g_ascii_strcasecmp (gcal_calendar_get_name (calendar1), gcal_calendar_get_name (calendar2));

  if (retval != 0)
    return retval;

  parent_name1 = e_source_get_display_name (gcal_calendar_get_parent_source (calendar1));
  parent_name2 = e_source_get_display_name (gcal_calendar_get_parent_source (calendar2));

  return g_strcmp0 (parent_name1, parent_name2);
}

static void
on_toast_button_clicked_cb (AdwToast                  *toast,
                            StampPreferencesCalendars *self)
{
  GcalCalendar *calendar = g_object_get_data (G_OBJECT (toast), "calendar");

  g_assert (calendar != NULL);

  gcal_calendar_set_visible (calendar, TRUE);
  add_calendar (self, calendar);

  g_object_set_data (G_OBJECT (toast), "calendar", NULL);

  g_clear_object (&self->toast);
}

static void
on_toast_dismissed_cb (AdwToast                  *toast,
                       StampPreferencesCalendars *self)
{
  clear_toast_and_delete_calendar (self, toast);
}

/*
 * The edit page asks to go back to "calendars" carrying a calendar only
 * when the user hit Remove; otherwise -- and always from the new-calendar
 * page -- there is nothing to undo.
 */
static void
offer_undo_of_removal (StampPreferencesCalendars *self,
                       GcalCalendar              *calendar)
{
  AdwPreferencesDialog *dialog = get_dialog (self);
  g_autoptr (AdwToast) toast = NULL;
  g_autofree gchar *message = NULL;

  if (!dialog)
    return;

  /* Commit the previously deleted calendar, if any */
  g_clear_pointer (&self->toast, adw_toast_dismiss);

  remove_calendar (self, calendar);

  /* TRANSLATORS: %s is a calendar name. */
  message = g_markup_printf_escaped (_("Calendar “%s” removed"), gcal_calendar_get_name (calendar));

  toast = adw_toast_new (message);
  adw_toast_set_timeout (toast, 7);
  adw_toast_set_button_label (toast, _("_Undo"));
  g_object_set_data_full (G_OBJECT (toast), "calendar", g_object_ref (calendar), g_object_unref);
  g_signal_connect_object (toast, "dismissed", G_CALLBACK (on_toast_dismissed_cb), self, 0);
  g_signal_connect_object (toast, "button-clicked", G_CALLBACK (on_toast_button_clicked_cb), self, 0);
  adw_preferences_dialog_add_toast (dialog, g_object_ref (toast));

  self->toast = g_steal_pointer (&toast);

  gcal_calendar_set_visible (calendar, FALSE);
}

static void
on_subpage_hidden_cb (AdwNavigationPage *page,
                      gpointer           user_data)
{
  gcal_calendar_management_page_deactivate (GCAL_CALENDAR_MANAGEMENT_PAGE (page));
}

static void
on_subpage_switch_page_cb (GcalCalendarManagementPage *page,
                           const gchar                *next_page,
                           GcalCalendar               *calendar,
                           StampPreferencesCalendars  *self)
{
  AdwPreferencesDialog *dialog = get_dialog (self);

  if (calendar)
    offer_undo_of_removal (self, calendar);

  if (dialog)
    adw_preferences_dialog_pop_subpage (dialog);
}

static void
push_subpage (StampPreferencesCalendars *self,
              GType                      page_type,
              GcalCalendar              *calendar)
{
  AdwPreferencesDialog *dialog = get_dialog (self);
  GcalCalendarManagementPage *page;

  if (!dialog)
    return;

  page = g_object_new (page_type, NULL);
  gcal_calendar_management_page_activate (page, calendar);

  g_signal_connect_object (page, "switch-page", G_CALLBACK (on_subpage_switch_page_cb), self, 0);
  g_signal_connect (page, "hidden", G_CALLBACK (on_subpage_hidden_cb), NULL);

  adw_preferences_dialog_push_subpage (dialog, ADW_NAVIGATION_PAGE (page));
}

static void
on_listbox_row_activated_cb (GtkListBox                *listbox,
                             GtkListBoxRow             *row,
                             StampPreferencesCalendars *self)
{
  GcalCalendar *calendar = g_object_get_data (G_OBJECT (row), "calendar");

  if (!calendar)
    return;

  push_subpage (self, GCAL_TYPE_EDIT_CALENDAR_PAGE, calendar);
}

static void
on_new_calendar_row_activated_cb (AdwButtonRow              *button,
                                  StampPreferencesCalendars *self)
{
  push_subpage (self, GCAL_TYPE_NEW_CALENDAR_PAGE, NULL);
}

static void
on_manager_calendar_added_cb (GcalManager               *manager,
                              GcalCalendar              *calendar,
                              StampPreferencesCalendars *self)
{
  add_calendar (self, calendar);
}

static void
on_manager_calendar_removed_cb (GcalManager               *manager,
                                GcalCalendar              *calendar,
                                StampPreferencesCalendars *self)
{
  remove_calendar (self, calendar);
}

static void
stamp_preferences_calendars_unmap (GtkWidget *widget)
{
  StampPreferencesCalendars *self = STAMP_PREFERENCES_CALENDARS (widget);

  if (self->toast)
    clear_toast_and_delete_calendar (self, self->toast);

  GTK_WIDGET_CLASS (stamp_preferences_calendars_parent_class)->unmap (widget);
}

static void
stamp_preferences_calendars_finalize (GObject *object)
{
  StampPreferencesCalendars *self = STAMP_PREFERENCES_CALENDARS (object);

  g_clear_object (&self->toast);

  G_OBJECT_CLASS (stamp_preferences_calendars_parent_class)->finalize (object);
}

static void
stamp_preferences_calendars_class_init (StampPreferencesCalendarsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = stamp_preferences_calendars_finalize;

  widget_class->unmap = stamp_preferences_calendars_unmap;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/stamp-preferences-calendars.ui");

  gtk_widget_class_bind_template_child (widget_class, StampPreferencesCalendars, listbox);

  gtk_widget_class_bind_template_callback (widget_class, on_listbox_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_new_calendar_row_activated_cb);
}

static void
stamp_preferences_calendars_init (StampPreferencesCalendars *self)
{
  GcalContext *context;
  GcalManager *manager;
  g_autoptr (GList) calendars = NULL;
  GList *l;

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_list_box_set_sort_func (self->listbox, listbox_sort_func, NULL, NULL);

  /* Preferences can be opened before the calendar view ever exists */
  context = stamp_gcal_ensure_context ();
  if (!context)
    return;

  manager = gcal_context_get_manager (context);

  g_signal_connect_object (manager, "calendar-added", G_CALLBACK (on_manager_calendar_added_cb), self, 0);
  g_signal_connect_object (manager, "calendar-removed", G_CALLBACK (on_manager_calendar_removed_cb), self, 0);

  calendars = gcal_manager_get_calendars (manager);
  for (l = calendars; l; l = l->next)
    add_calendar (self, l->data);
}
