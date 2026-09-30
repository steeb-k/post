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

#include "stamp-invitation-dialog.h"

#include <glib/gi18n.h>
#include <libecal/libecal.h>

#include "gcal-context.h"
#include "gcal-event.h"
#include "gcal-event-attendee.h"
#include "gcal-event-organizer.h"
#include "gcal-meeting-row.h"
#include "gcal-organizer-row.h"
#include "gcal-utils.h"
#include "stamp-gcal.h"

struct _StampInvitationDialog {
  AdwDialog parent;

  GtkLabel *summary_label;
  GtkLabel *date_time_label;
  GtkLabel *location_label;
  GtkLabel *response_label;
  GtkListBox *meetings_listbox;
  AdwPreferencesGroup *participants_group;
  GtkWidget *notes_box;
  GtkLabel *description_label;
  GtkWidget *decline_button;
  GtkWidget *tentative_button;
  GtkWidget *accept_button;
  GtkWidget *add_button;

  GcalEvent *event;
};

G_DEFINE_FINAL_TYPE (StampInvitationDialog, stamp_invitation_dialog, ADW_TYPE_DIALOG)

enum {
  RESPOND,
  ADD_TO_CALENDAR,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/*
 * Dates
 */

static gchar *
format_time (GDateTime *date)
{
  GcalContext *context = stamp_gcal_ensure_context ();

  if (gcal_context_get_time_format (context) == GCAL_TIME_FORMAT_12H)
    return g_date_time_format (date, "%I:%M %P");

  return g_date_time_format (date, "%R");
}

static gchar *
format_date (GDateTime *date)
{
  /* Translators: a full date, for example "Tuesday, March 3, 2026" */
  return g_date_time_format (date, _("%A, %B %-e, %Y"));
}

static gchar *
format_event_time (GcalEvent *event)
{
  g_autoptr (GDateTime) start = NULL;
  g_autoptr (GDateTime) end = NULL;
  g_autofree gchar *start_date = NULL;
  g_autofree gchar *end_date = NULL;
  g_autofree gchar *start_time = NULL;
  g_autofree gchar *end_time = NULL;
  gboolean all_day = gcal_event_get_all_day (event);

  if (all_day) {
    start = g_date_time_ref (gcal_event_get_date_start (event));
    /* An all-day event ends at midnight after its last day */
    end = g_date_time_add_days (gcal_event_get_date_end (event), -1);
  } else {
    start = g_date_time_to_local (gcal_event_get_date_start (event));
    end = g_date_time_to_local (gcal_event_get_date_end (event));
  }

  start_date = format_date (start);
  end_date = format_date (end);

  if (all_day) {
    if (g_strcmp0 (start_date, end_date) == 0)
      return g_steal_pointer (&start_date);

    /* Translators: the first and last day of an event lasting several days */
    return g_strdup_printf (_("%1$s – %2$s"), start_date, end_date);
  }

  start_time = format_time (start);
  end_time = format_time (end);

  if (g_strcmp0 (start_date, end_date) == 0) {
    /* Translators: a date, then the start and end time of an event on that day */
    return g_strdup_printf (_("%1$s, %2$s – %3$s"), start_date, start_time, end_time);
  }

  /* Translators: the start date and time, then the end date and time, of an event */
  return g_strdup_printf (_("%1$s, %2$s – %3$s, %4$s"), start_date, start_time, end_date, end_time);
}

/*
 * Meetings
 */

static void
on_uri_launched (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr (GError) error = NULL;

  if (!gtk_uri_launcher_launch_finish (GTK_URI_LAUNCHER (source), result, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("%s: Could not open the meeting link: %s", G_STRFUNC, error->message);
}

static void
on_join_meeting (GcalMeetingRow        *row G_GNUC_UNUSED,
                 const gchar           *url,
                 StampInvitationDialog *self)
{
  g_autoptr (GtkUriLauncher) launcher = gtk_uri_launcher_new (url);
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));

  gtk_uri_launcher_launch (launcher, GTK_IS_WINDOW (root) ? GTK_WINDOW (root) : NULL, NULL, on_uri_launched, NULL);
}

static void
add_meeting (StampInvitationDialog *self,
             const gchar           *url)
{
  GtkWidget *child;
  GtkWidget *row;

  if (!url || !*url)
    return;

  /* The same call tends to be named more than once: X-GOOGLE-CONFERENCE
   * and the Google block in the notes carry one link, and so do
   * LOCATION and CONFERENCE on many invitations. */
  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->meetings_listbox));
       child;
       child = gtk_widget_get_next_sibling (child)) {
    if (g_strcmp0 (g_object_get_data (G_OBJECT (child), "meeting-url"), url) == 0)
      return;
  }

  row = gcal_meeting_row_new (url);
  g_object_set_data_full (G_OBJECT (row), "meeting-url", g_strdup (url), g_free);
  g_signal_connect (row, "join-meeting", G_CALLBACK (on_join_meeting), self);
  gtk_list_box_append (self->meetings_listbox, row);
  gtk_widget_set_visible (GTK_WIDGET (self->meetings_listbox), TRUE);
}

static gboolean
looks_like_url (const gchar *text)
{
  g_autoptr (GUri) uri = NULL;

  if (!text || !*text)
    return FALSE;

  uri = g_uri_parse (text, G_URI_FLAGS_PARSE_RELAXED, NULL);
  if (!uri)
    return FALSE;

  return g_strcmp0 (g_uri_get_scheme (uri), "http") == 0 || g_strcmp0 (g_uri_get_scheme (uri), "https") == 0;
}

static void
setup_meetings_and_location (StampInvitationDialog *self)
{
  g_autoptr (GPtrArray) urls = NULL;
  g_autofree gchar *location = NULL;
  ECalComponent *component = gcal_event_get_component (self->event);
  guint i;

  urls = gcal_utils_get_conference_urls (e_cal_component_get_icalcomponent (component));
  for (i = 0; i < urls->len; i++)
    add_meeting (self, g_ptr_array_index (urls, i));

  location = g_strdup (gcal_event_get_location (self->event));
  if (location)
    g_strstrip (location);

  if (looks_like_url (location)) {
    add_meeting (self, location);
  } else if (location && *location) {
    gtk_label_set_label (self->location_label, location);
    gtk_widget_set_visible (GTK_WIDGET (self->location_label), TRUE);
  }
}

static void
setup_notes (StampInvitationDialog *self)
{
  g_autofree gchar *description = NULL;
  g_autofree gchar *meeting_url = NULL;
  g_autoptr (GString) text = NULL;

  gcal_utils_extract_meeting_url (gcal_event_get_description (self->event), &description, &meeting_url);

  if (meeting_url)
    add_meeting (self, meeting_url);

  if (!description)
    return;

  text = g_string_new (description);
  g_string_replace (text, "<br>", "\n", 0);
  g_string_replace (text, "&nbsp;", " ", 0);
  g_strstrip (text->str);

  if (!*text->str)
    return;

  gtk_label_set_label (self->description_label, text->str);
  gtk_widget_set_visible (self->notes_box, TRUE);
}

/*
 * Participants
 */

static GtkWidget *
create_attendee_row (GcalEventAttendee *attendee)
{
  const gchar *name = gcal_event_attendee_get_name (attendee);
  /* Despite its signature the helper hands back a copy */
  g_autofree gchar *email = (gchar *) gcal_get_email_from_mailto_uri (gcal_event_attendee_get_uri (attendee));
  const gchar *status = gcal_event_attendee_get_part_status_label (attendee);
  GtkWidget *row = adw_action_row_new ();

  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), FALSE);
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), name && *name ? name : (email ? email : ""));
  if (name && *name && email && g_strcmp0 (name, email) != 0)
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), email);
  gtk_widget_add_css_class (row, "property");

  if (status) {
    GtkWidget *label = gtk_label_new (status);

    gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (label, "dimmed");
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), label);
  }

  return row;
}

static gboolean
attendee_is (GcalEventAttendee *attendee,
             const gchar       *email)
{
  g_autofree gchar *attendee_email = (gchar *) gcal_get_email_from_mailto_uri (gcal_event_attendee_get_uri (attendee));

  return email && attendee_email && g_ascii_strcasecmp (attendee_email, email) == 0;
}

static void
setup_participants (StampInvitationDialog *self,
                    const gchar           *own_email)
{
  GcalEventOrganizer *organizer = gcal_event_get_organizer (self->event);
  GListModel *attendees = gcal_event_get_attendees (self->event);
  gboolean has_participants = FALSE;
  guint n_attendees = attendees ? g_list_model_get_n_items (attendees) : 0;
  guint i;

  if (organizer) {
    GtkWidget *row = g_object_new (GCAL_TYPE_ORGANIZER_ROW, "organizer", organizer, NULL);

    adw_preferences_group_add (self->participants_group, row);
    has_participants = TRUE;
  }

  for (i = 0; i < n_attendees; i++) {
    g_autoptr (GcalEventAttendee) attendee = g_list_model_get_item (attendees, i);

    if (attendee_is (attendee, own_email)) {
      const gchar *status = gcal_event_attendee_get_part_status_label (attendee);

      if (status && gcal_event_attendee_get_part_status (attendee) != GCAL_EVENT_ATTENDEE_PART_NEEDS_ACTION) {
        /* Translators: %s is the reader's own answer to the invitation, for example "Accepted" */
        g_autofree gchar *text = g_strdup_printf (_("Your response: %s"), status);

        gtk_label_set_label (self->response_label, text);
        gtk_widget_set_visible (GTK_WIDGET (self->response_label), TRUE);
      }
    }

    /* Rooms and equipment are listed among the attendees too, but they are not people */
    switch (gcal_event_attendee_get_attendee_type (attendee)) {
      case GCAL_EVENT_ATTENDEE_TYPE_RESOURCE:
      case GCAL_EVENT_ATTENDEE_TYPE_ROOM:
        continue;

      default:
        break;
    }

    adw_preferences_group_add (self->participants_group, create_attendee_row (attendee));
    has_participants = TRUE;
  }

  gtk_widget_set_visible (GTK_WIDGET (self->participants_group), has_participants);
}

/*
 * Buttons
 */

static void
respond (StampInvitationDialog *self,
         ICalParameterPartstat  partstat)
{
  g_object_ref (self);
  g_signal_emit (self, signals[RESPOND], 0, (gint) partstat);
  adw_dialog_close (ADW_DIALOG (self));
  g_object_unref (self);
}

static void
on_decline_clicked (GtkButton             *button G_GNUC_UNUSED,
                    StampInvitationDialog *self)
{
  respond (self, I_CAL_PARTSTAT_DECLINED);
}

static void
on_tentative_clicked (GtkButton             *button G_GNUC_UNUSED,
                      StampInvitationDialog *self)
{
  respond (self, I_CAL_PARTSTAT_TENTATIVE);
}

static void
on_accept_clicked (GtkButton             *button G_GNUC_UNUSED,
                   StampInvitationDialog *self)
{
  respond (self, I_CAL_PARTSTAT_ACCEPTED);
}

static void
on_add_clicked (GtkButton             *button G_GNUC_UNUSED,
                StampInvitationDialog *self)
{
  g_object_ref (self);
  g_signal_emit (self, signals[ADD_TO_CALENDAR], 0);
  adw_dialog_close (ADW_DIALOG (self));
  g_object_unref (self);
}

/*
 * GObject
 */

static void
stamp_invitation_dialog_dispose (GObject *object)
{
  StampInvitationDialog *self = STAMP_INVITATION_DIALOG (object);

  g_clear_object (&self->event);
  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_INVITATION_DIALOG);

  G_OBJECT_CLASS (stamp_invitation_dialog_parent_class)->dispose (object);
}

static void
stamp_invitation_dialog_class_init (StampInvitationDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = stamp_invitation_dialog_dispose;

  /* The reader answered: the argument is the ICalParameterPartstat they chose */
  signals[RESPOND] = g_signal_new ("respond",
                                   STAMP_TYPE_INVITATION_DIALOG,
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL,
                                   g_cclosure_marshal_VOID__INT,
                                   G_TYPE_NONE, 1, G_TYPE_INT);

  signals[ADD_TO_CALENDAR] = g_signal_new ("add-to-calendar",
                                           STAMP_TYPE_INVITATION_DIALOG,
                                           G_SIGNAL_RUN_LAST,
                                           0, NULL, NULL,
                                           g_cclosure_marshal_VOID__VOID,
                                           G_TYPE_NONE, 0);

  g_type_ensure (GCAL_TYPE_MEETING_ROW);
  g_type_ensure (GCAL_TYPE_ORGANIZER_ROW);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/mail/message-list/stamp-invitation-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, summary_label);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, date_time_label);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, location_label);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, response_label);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, meetings_listbox);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, participants_group);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, notes_box);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, description_label);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, decline_button);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, tentative_button);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, accept_button);
  gtk_widget_class_bind_template_child (widget_class, StampInvitationDialog, add_button);

  gtk_widget_class_bind_template_callback (widget_class, on_decline_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_tentative_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_accept_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_add_clicked);
}

static void
stamp_invitation_dialog_init (StampInvitationDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

StampInvitationDialog *
stamp_invitation_dialog_new (ICalComponent *vcalendar,
                             const gchar   *method,
                             const gchar   *own_email)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (ECalComponent) component = NULL;
  g_autofree gchar *when = NULL;
  StampInvitationDialog *self;
  ICalComponent *vevent;
  GcalEvent *event;
  gboolean asks_for_answer;

  g_return_val_if_fail (vcalendar != NULL, NULL);

  /* No g_autoptr: libical-glib only declares a cleanup function for
   * ICalComponent from 4.0 on, and the flatpak still builds against 3. */
  vevent = i_cal_component_get_first_component (vcalendar, I_CAL_VEVENT_COMPONENT);
  if (!vevent) {
    g_warning ("%s: Invitation carries no event", G_STRFUNC);
    return NULL;
  }

  /* ECalComponent takes the component over, so hand it a copy and keep
   * the message's own untouched for the reply. */
  component = e_cal_component_new_from_icalcomponent (i_cal_component_clone (vevent));
  g_object_unref (vevent);

  if (!component) {
    g_warning ("%s: Invitation carries an event that could not be read", G_STRFUNC);
    return NULL;
  }

  /* No calendar: the event is only being looked at, not stored. GcalEvent
   * copes, and the whole of the vendored calendar's reading of the
   * component comes for free. */
  event = gcal_event_new (NULL, component, &error);
  if (!event) {
    g_warning ("%s: Could not read the invitation: %s", G_STRFUNC, error ? error->message : "unknown error");
    return NULL;
  }

  self = g_object_new (STAMP_TYPE_INVITATION_DIALOG, NULL);
  self->event = event;

  gtk_label_set_label (self->summary_label, gcal_event_get_summary (event));

  when = format_event_time (event);
  gtk_label_set_label (self->date_time_label, when);

  setup_meetings_and_location (self);
  setup_notes (self);
  setup_participants (self, own_email);

  /* Only REQUEST asks the recipient a question. PUBLISH and friends are
   * events to file away. */
  asks_for_answer = !method || g_strcmp0 (method, "REQUEST") == 0;
  gtk_widget_set_visible (self->decline_button, asks_for_answer);
  gtk_widget_set_visible (self->tentative_button, asks_for_answer);
  gtk_widget_set_visible (self->accept_button, asks_for_answer);
  gtk_widget_set_visible (self->add_button, !asks_for_answer);

  return self;
}
