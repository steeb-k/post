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

#include "stamp-contact-editor.h"

#include <glib/gi18n.h>

#include "gcal-date-chooser-row.h"
#include "stamp-account.h"
#include "stamp-gcal.h"
#include "stamp-photo-crop.h"
#include "stamp-session.h"

struct _StampContactEditor {
  AdwDialog parent_instance;

  GtkWidget *window_title;
  GtkWidget *save_button;
  GtkWidget *avatar;
  GtkWidget *photo_button;
  GtkWidget *remove_photo_button;

  GtkWidget *given_row;
  GtkWidget *family_row;
  GtkWidget *book_row;

  GtkWidget *email_group;

  GtkWidget *phone_mobile;
  GtkWidget *phone_business;
  GtkWidget *phone_home;
  GtkWidget *phone_primary;
  GtkWidget *phone_other;

  GtkWidget *company_row;
  GtkWidget *unit_row;
  GtkWidget *office_row;

  GtkWidget *birthday_row;
  GtkWidget *birthday_date;

  GtkWidget *home_street;
  GtkWidget *home_code;
  GtkWidget *home_city;
  GtkWidget *home_country;

  GtkWidget *work_street;
  GtkWidget *work_code;
  GtkWidget *work_city;
  GtkWidget *work_country;

  GtkWidget *notes_view;

  /* Writable books, in the order the combo row lists them. */
  GPtrArray *books;
  GPtrArray *email_rows;

  EContact *contact;
  EBookClient *client;

  /* Only written back when the user actually touched the picture, so
   * saving an unrelated edit does not re-encode what is already there. */
  GdkTexture *photo;
  gboolean photo_changed;
  /* Prefixes, middle names and suffixes the editor does not show, kept so
   * that saving a contact does not quietly drop them. */
  EContactName *name;

  GCancellable *cancellable;
};

G_DEFINE_FINAL_TYPE (StampContactEditor, stamp_contact_editor, ADW_TYPE_DIALOG);

static const struct {
  EContactField field;
  goffset offset;
} phone_fields[] = {
  { E_CONTACT_PHONE_MOBILE, G_STRUCT_OFFSET (StampContactEditor, phone_mobile) },
  { E_CONTACT_PHONE_BUSINESS, G_STRUCT_OFFSET (StampContactEditor, phone_business) },
  { E_CONTACT_PHONE_HOME, G_STRUCT_OFFSET (StampContactEditor, phone_home) },
  { E_CONTACT_PHONE_PRIMARY, G_STRUCT_OFFSET (StampContactEditor, phone_primary) },
  { E_CONTACT_PHONE_OTHER, G_STRUCT_OFFSET (StampContactEditor, phone_other) },
};

static GtkWidget *
phone_row (StampContactEditor *self,
           guint               idx)
{
  return G_STRUCT_MEMBER (GtkWidget *, self, phone_fields[idx].offset);
}

static const gchar *
row_text (GtkWidget *row)
{
  return gtk_editable_get_text (GTK_EDITABLE (row));
}

static gboolean
row_is_empty (GtkWidget *row)
{
  const gchar *text = row_text (row);

  return !text || *text == '\0';
}

static void
set_row_text (GtkWidget   *row,
              const gchar *text)
{
  gtk_editable_set_text (GTK_EDITABLE (row), text ? text : "");
}

/* An empty string clears the field rather than storing a blank one:
 * e_contact_set removes the attribute when handed NULL. */
static void
set_contact_string (EContact      *contact,
                    EContactField  field,
                    const gchar   *text)
{
  e_contact_set (contact, field, (text && *text) ? (gpointer)text : NULL);
}

static void
update_save_sensitivity (StampContactEditor *self)
{
  gboolean has_name;
  gboolean has_mail = FALSE;
  gboolean has_book;

  has_name = !row_is_empty (self->given_row) || !row_is_empty (self->family_row);

  for (guint idx = 0; idx < self->email_rows->len; idx++) {
    if (!row_is_empty (g_ptr_array_index (self->email_rows, idx))) {
      has_mail = TRUE;
      break;
    }
  }

  has_book = self->contact ? self->client != NULL : self->books->len > 0;

  gtk_widget_set_sensitive (self->save_button, (has_name || has_mail) && has_book);
}

static void
on_field_changed (GtkWidget *row,
                  gpointer   user_data)
{
  update_save_sensitivity (STAMP_CONTACT_EDITOR (user_data));
}

static void
on_name_changed (GtkWidget *row,
                 gpointer   user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);
  g_autofree char *full = g_strconcat (row_text (self->given_row), " ", row_text (self->family_row), NULL);

  adw_avatar_set_text (ADW_AVATAR (self->avatar), g_strstrip (full));
  update_save_sensitivity (self);
}

static void add_email_row (StampContactEditor *self,
                           const gchar        *address);

static void
on_remove_email_clicked (GtkButton *button,
                         gpointer   user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);
  GtkWidget *row = g_object_get_data (G_OBJECT (button), "row");

  adw_preferences_group_remove (ADW_PREFERENCES_GROUP (self->email_group), row);
  g_ptr_array_remove (self->email_rows, row);

  /* Something to type into is always better than an empty group. */
  if (self->email_rows->len == 0)
    add_email_row (self, NULL);

  update_save_sensitivity (self);
}

static void
add_email_row (StampContactEditor *self,
               const gchar        *address)
{
  GtkWidget *row = adw_entry_row_new ();
  GtkWidget *remove_button = gtk_button_new_from_icon_name ("list-remove-symbolic");

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _("Address"));
  gtk_editable_set_text (GTK_EDITABLE (row), address ? address : "");
  g_object_set (row, "input-purpose", GTK_INPUT_PURPOSE_EMAIL, NULL);

  gtk_widget_set_valign (remove_button, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (remove_button, _("Remove Email"));
  gtk_widget_add_css_class (remove_button, "flat");
  g_object_set_data (G_OBJECT (remove_button), "row", row);
  g_signal_connect (remove_button, "clicked", G_CALLBACK (on_remove_email_clicked), self);
  adw_entry_row_add_suffix (ADW_ENTRY_ROW (row), remove_button);

  g_signal_connect (row, "changed", G_CALLBACK (on_field_changed), self);

  adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->email_group), row);
  g_ptr_array_add (self->email_rows, row);
}

static void
on_add_email_clicked (GtkButton *button,
                      gpointer   user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);

  add_email_row (self, NULL);
}

static void
on_cancel_clicked (GtkButton *button,
                   gpointer   user_data)
{
  adw_dialog_close (ADW_DIALOG (user_data));
}

/* Adding and changing are the same operation, but not the same sentence
 * to someone deciding whether to press it. */
static void
update_photo_buttons (StampContactEditor *self,
                      gboolean            has_photo)
{
  gtk_button_set_label (GTK_BUTTON (self->photo_button),
                        has_photo ? _("Change Picture") : _("Add Picture"));
  gtk_widget_set_visible (self->remove_photo_button, has_photo);
}

static void
set_photo (StampContactEditor *self,
           GdkTexture         *texture)
{
  g_set_object (&self->photo, texture);
  self->photo_changed = TRUE;

  adw_avatar_set_custom_image (ADW_AVATAR (self->avatar), texture ? GDK_PAINTABLE (texture) : NULL);
  update_photo_buttons (self, texture != NULL);
}

static void
on_photo_cropped (GdkTexture *texture,
                  gpointer    user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);

  /* A cancelled crop leaves whatever was there alone. */
  if (!texture)
    return;

  set_photo (self, texture);
}

static void
on_photo_chosen (GObject      *object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) error = NULL;

  file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (object), res, &error);
  if (!file)
    return;

  stamp_photo_crop_present (GTK_WIDGET (self), file, on_photo_cropped, self);
}

static void
on_photo_button_clicked (GtkButton *button,
                         gpointer   user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
  g_autoptr (GtkFileFilter) filter = gtk_file_filter_new ();
  g_autoptr (GListStore) filters = g_list_store_new (GTK_TYPE_FILE_FILTER);

  gtk_file_filter_set_name (filter, _("Pictures"));
  gtk_file_filter_add_mime_type (filter, "image/*");
  g_list_store_append (filters, filter);

  gtk_file_dialog_set_title (dialog, _("Choose a Picture"));
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  gtk_file_dialog_open (dialog, GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self))),
                        self->cancellable, on_photo_chosen, self);
}

static void
on_remove_photo_clicked (GtkButton *button,
                         gpointer   user_data)
{
  set_photo (STAMP_CONTACT_EDITOR (user_data), NULL);
}

/*
 * Every book in every account that will accept a write, so that adding a
 * contact from one view does not depend on which book a sidebar elsewhere
 * happens to have selected.
 */
static void
populate_books (StampContactEditor *self,
                EBookClient        *preselect)
{
  StampSession *session = stamp_session_get_default ();
  g_autoptr (GtkStringList) labels = gtk_string_list_new (NULL);
  guint selected = 0;

  for (GList *iter = stamp_session_get_accounts (session); iter; iter = g_list_next (iter)) {
    StampAccount *account = STAMP_ACCOUNT (iter->data);
    g_autoptr (GPtrArray) books = stamp_account_get_writable_books (account);

    for (guint idx = 0; idx < books->len; idx++) {
      StampContactsService *service = g_ptr_array_index (books, idx);
      EBookClient *client = stamp_contacts_service_get_client (service);
      ESource *source = stamp_contacts_service_get_source (service);
      g_autofree char *label = NULL;

      label = g_strdup_printf ("%s — %s", e_source_get_display_name (source), stamp_account_get_name (account));
      gtk_string_list_append (labels, label);
      g_ptr_array_add (self->books, g_object_ref (client));

      if (client == preselect)
        selected = self->books->len - 1;
    }
  }

  adw_combo_row_set_model (ADW_COMBO_ROW (self->book_row), G_LIST_MODEL (labels));
  adw_combo_row_set_selected (ADW_COMBO_ROW (self->book_row), selected);
}

static void
load_address (EContact      *contact,
              EContactField  field,
              GtkWidget     *street,
              GtkWidget     *code,
              GtkWidget     *city,
              GtkWidget     *country)
{
  g_autoptr (EContactAddress) address = e_contact_get (contact, field);

  set_row_text (street, address ? address->street : NULL);
  set_row_text (code, address ? address->code : NULL);
  set_row_text (city, address ? address->locality : NULL);
  set_row_text (country, address ? address->country : NULL);
}

static void
load_photo (StampContactEditor *self,
            EContact           *contact)
{
  g_autoptr (EContactPhoto) photo = e_contact_get (contact, E_CONTACT_PHOTO);
  g_autoptr (GdkTexture) texture = NULL;

  if (!photo)
    return;

  if (photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {
    g_autoptr (GBytes) bytes = g_bytes_new (photo->data.inlined.data, photo->data.inlined.length);

    texture = gdk_texture_new_from_bytes (bytes, NULL);
  } else if (photo->type == E_CONTACT_PHOTO_TYPE_URI && photo->data.uri &&
             g_str_has_prefix (photo->data.uri, "file://")) {
    /* A picture written inline does not necessarily come back that way:
     * the local backend moves it into its own store and leaves a URI
     * behind. Only local ones are read, since anything else would mean
     * fetching over the network to draw a dialog. */
    g_autoptr (GFile) file = g_file_new_for_uri (photo->data.uri);

    texture = gdk_texture_new_from_file (file, NULL);
  }

  if (texture) {
    adw_avatar_set_custom_image (ADW_AVATAR (self->avatar), GDK_PAINTABLE (texture));
    update_photo_buttons (self, TRUE);
  }
}

static void
load_contact (StampContactEditor *self,
              EContact           *contact)
{
  g_autoptr (EContactDate) birthday = NULL;
  GList *mails;
  GtkTextBuffer *buffer;
  const gchar *note;

  /* Contacts that only ever had a display name -- the ones the mail side
   * synthesises -- have no structured name to read, so take one apart. */
  self->name = e_contact_get (contact, E_CONTACT_NAME);
  if (!self->name || (!self->name->given && !self->name->family)) {
    const gchar *full = e_contact_get_const (contact, E_CONTACT_FULL_NAME);

    if (full && *full) {
      g_clear_pointer (&self->name, e_contact_name_free);
      self->name = e_contact_name_from_string (full);
    }
  }

  if (!self->name)
    self->name = e_contact_name_new ();

  set_row_text (self->given_row, self->name->given);
  set_row_text (self->family_row, self->name->family);

  mails = e_contact_get (contact, E_CONTACT_EMAIL);
  for (GList *iter = mails; iter && iter->data; iter = g_list_next (iter))
    add_email_row (self, iter->data);
  g_list_free_full (mails, g_free);

  if (self->email_rows->len == 0)
    add_email_row (self, NULL);

  for (guint idx = 0; idx < G_N_ELEMENTS (phone_fields); idx++)
    set_row_text (phone_row (self, idx), e_contact_get_const (contact, phone_fields[idx].field));

  set_row_text (self->company_row, e_contact_get_const (contact, E_CONTACT_ORG));
  set_row_text (self->unit_row, e_contact_get_const (contact, E_CONTACT_ORG_UNIT));
  set_row_text (self->office_row, e_contact_get_const (contact, E_CONTACT_OFFICE));

  birthday = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
  if (birthday) {
    g_autoptr (GDateTime) date = g_date_time_new_local (birthday->year, birthday->month, birthday->day, 0, 0, 0.0);

    if (date) {
      gcal_date_chooser_row_set_date (GCAL_DATE_CHOOSER_ROW (self->birthday_date), date);
      adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (self->birthday_row), TRUE);
    }
  }

  load_address (contact, E_CONTACT_ADDRESS_HOME,
                self->home_street, self->home_code, self->home_city, self->home_country);
  load_address (contact, E_CONTACT_ADDRESS_WORK,
                self->work_street, self->work_code, self->work_city, self->work_country);

  note = e_contact_get_const (contact, E_CONTACT_NOTE);
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->notes_view));
  gtk_text_buffer_set_text (buffer, note ? note : "", -1);

  load_photo (self, contact);
  adw_avatar_set_text (ADW_AVATAR (self->avatar), e_contact_get_const (contact, E_CONTACT_FULL_NAME));
}

static void
apply_address (EContact      *contact,
               EContactField  field,
               GtkWidget     *street,
               GtkWidget     *code,
               GtkWidget     *city,
               GtkWidget     *country)
{
  EContactAddress address = { 0 };

  if (row_is_empty (street) && row_is_empty (code) && row_is_empty (city) && row_is_empty (country)) {
    e_contact_set (contact, field, NULL);
    return;
  }

  address.street = (gchar *)row_text (street);
  address.code = (gchar *)row_text (code);
  address.locality = (gchar *)row_text (city);
  address.country = (gchar *)row_text (country);

  e_contact_set (contact, field, &address);
}

static void
apply_fields (StampContactEditor *self,
              EContact           *contact)
{
  g_autofree char *full = NULL;
  GtkTextBuffer *buffer;
  GtkTextIter start;
  GtkTextIter end;
  g_autofree char *note = NULL;
  GList *mails = NULL;
  /* Shallow copy: the parts the editor does not show ride along, while
   * the two it does come from the rows and stay owned by them. */
  EContactName name = *self->name;

  name.given = (gchar *)row_text (self->given_row);
  name.family = (gchar *)row_text (self->family_row);

  e_contact_set (contact, E_CONTACT_NAME, &name);

  full = e_contact_name_to_string (&name);
  set_contact_string (contact, E_CONTACT_FULL_NAME, full);
  /* Other programs sort on file-as; leaving a stale one behind means a
   * renamed contact keeps its old place everywhere else. */
  set_contact_string (contact, E_CONTACT_FILE_AS, full);

  for (guint idx = 0; idx < self->email_rows->len; idx++) {
    GtkWidget *row = g_ptr_array_index (self->email_rows, idx);

    if (!row_is_empty (row))
      mails = g_list_append (mails, (gpointer)row_text (row));
  }

  e_contact_set (contact, E_CONTACT_EMAIL, mails);
  g_list_free (mails);

  for (guint idx = 0; idx < G_N_ELEMENTS (phone_fields); idx++)
    set_contact_string (contact, phone_fields[idx].field, row_text (phone_row (self, idx)));

  set_contact_string (contact, E_CONTACT_ORG, row_text (self->company_row));
  set_contact_string (contact, E_CONTACT_ORG_UNIT, row_text (self->unit_row));
  set_contact_string (contact, E_CONTACT_OFFICE, row_text (self->office_row));

  if (adw_expander_row_get_enable_expansion (ADW_EXPANDER_ROW (self->birthday_row))) {
    GDateTime *date = gcal_date_chooser_row_get_date (GCAL_DATE_CHOOSER_ROW (self->birthday_date));
    EContactDate birthday = { 0 };

    birthday.year = g_date_time_get_year (date);
    birthday.month = g_date_time_get_month (date);
    birthday.day = g_date_time_get_day_of_month (date);

    e_contact_set (contact, E_CONTACT_BIRTH_DATE, &birthday);
  } else {
    e_contact_set (contact, E_CONTACT_BIRTH_DATE, NULL);
  }

  apply_address (contact, E_CONTACT_ADDRESS_HOME,
                 self->home_street, self->home_code, self->home_city, self->home_country);
  apply_address (contact, E_CONTACT_ADDRESS_WORK,
                 self->work_street, self->work_code, self->work_city, self->work_country);

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->notes_view));
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  note = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
  set_contact_string (contact, E_CONTACT_NOTE, note);

  if (!self->photo_changed)
    return;

  if (self->photo) {
    g_autoptr (GBytes) bytes = gdk_texture_save_to_png_bytes (self->photo);
    EContactPhoto photo = { 0 };
    gsize length = 0;

    photo.type = E_CONTACT_PHOTO_TYPE_INLINED;
    photo.data.inlined.mime_type = (gchar *)"image/png";
    photo.data.inlined.data = (guchar *)g_bytes_get_data (bytes, &length);
    photo.data.inlined.length = length;

    e_contact_set (contact, E_CONTACT_PHOTO, &photo);
  } else {
    e_contact_set (contact, E_CONTACT_PHOTO, NULL);
  }
}

/*
 * Avatars are cached per address the photo was found under, across every
 * account, so a changed picture has to be forgotten everywhere or the
 * mail side keeps drawing the old one until the cache ages out.
 */
static void
invalidate_photo_cache (StampContactEditor *self)
{
  StampSession *session = stamp_session_get_default ();
  GList *keys = NULL;
  g_autofree char *full_name = NULL;

  if (!self->photo_changed)
    return;

  for (guint idx = 0; idx < self->email_rows->len; idx++) {
    GtkWidget *row = g_ptr_array_index (self->email_rows, idx);

    if (!row_is_empty (row))
      keys = g_list_prepend (keys, g_utf8_strdown (row_text (row), -1));
  }

  /* Contacts without an address are looked up by name instead. */
  full_name = g_strconcat (row_text (self->given_row), " ", row_text (self->family_row), NULL);
  g_strstrip (full_name);
  if (*full_name)
    keys = g_list_prepend (keys, g_strdup (full_name));

  for (GList *account = stamp_session_get_accounts (session); account; account = g_list_next (account)) {
    for (GList *key = keys; key; key = g_list_next (key))
      stamp_account_invalidate_photo (STAMP_ACCOUNT (account->data), key->data);
  }

  g_list_free_full (keys, g_free);
}

static void
show_save_error (StampContactEditor *self,
                 const GError       *error)
{
  AdwDialog *alert;

  alert = adw_alert_dialog_new (_("Could Not Save Contact"), error->message);
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (alert), "close", _("_Close"));
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "close");
  adw_dialog_present (alert, GTK_WIDGET (self));

  gtk_widget_set_sensitive (self->save_button, TRUE);
}

static void
on_contact_added (GObject      *object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);
  g_autofree char *uid = NULL;
  g_autoptr (GError) error = NULL;

  if (!e_book_client_add_contact_finish (E_BOOK_CLIENT (object), res, &uid, &error)) {
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;

    show_save_error (self, error);
    return;
  }

  adw_dialog_close (ADW_DIALOG (self));
}

static void
on_contact_modified (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);
  g_autoptr (GError) error = NULL;

  if (!e_book_client_modify_contact_finish (E_BOOK_CLIENT (object), res, &error)) {
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;

    show_save_error (self, error);
    return;
  }

  adw_dialog_close (ADW_DIALOG (self));
}

static void
on_save_clicked (GtkButton *button,
                 gpointer   user_data)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (user_data);
  g_autoptr (EContact) contact = NULL;
  EBookClient *client;

  if (self->contact) {
    client = self->client;
  } else {
    guint selected = adw_combo_row_get_selected (ADW_COMBO_ROW (self->book_row));

    if (selected >= self->books->len)
      return;

    client = g_ptr_array_index (self->books, selected);
  }

  if (!client)
    return;

  /* Edit a copy: a save the backend refuses must not leave the contact in
   * the list wearing changes that never reached the book. */
  contact = self->contact ? e_contact_duplicate (self->contact) : e_contact_new ();
  apply_fields (self, contact);

  /* Before the write, not after it: the live view can announce the
   * change before the write's own callback runs, and whatever redraws
   * on the back of that would only refill the cache with the picture
   * being replaced. Clearing early costs a re-read if the save fails. */
  invalidate_photo_cache (self);

  gtk_widget_set_sensitive (self->save_button, FALSE);

  if (self->contact)
    e_book_client_modify_contact (client, contact, E_BOOK_OPERATION_FLAG_NONE,
                                  self->cancellable, on_contact_modified, self);
  else
    e_book_client_add_contact (client, contact, E_BOOK_OPERATION_FLAG_NONE,
                               self->cancellable, on_contact_added, self);
}

static void
stamp_contact_editor_dispose (GObject *object)
{
  StampContactEditor *self = STAMP_CONTACT_EDITOR (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_pointer (&self->books, g_ptr_array_unref);
  g_clear_pointer (&self->email_rows, g_ptr_array_unref);
  g_clear_object (&self->contact);
  g_clear_object (&self->client);
  g_clear_object (&self->photo);

  g_clear_pointer (&self->name, e_contact_name_free);

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_CONTACT_EDITOR);

  G_OBJECT_CLASS (stamp_contact_editor_parent_class)->dispose (object);
}

static void
stamp_contact_editor_class_init (StampContactEditorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = stamp_contact_editor_dispose;

  g_type_ensure (GCAL_TYPE_DATE_CHOOSER_ROW);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/contact/stamp-contact-editor.ui");

  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, window_title);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, save_button);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, avatar);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, photo_button);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, remove_photo_button);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, given_row);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, family_row);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, book_row);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, email_group);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, phone_mobile);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, phone_business);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, phone_home);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, phone_primary);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, phone_other);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, company_row);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, unit_row);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, office_row);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, birthday_row);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, birthday_date);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, home_street);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, home_code);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, home_city);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, home_country);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, work_street);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, work_code);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, work_city);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, work_country);
  gtk_widget_class_bind_template_child (widget_class, StampContactEditor, notes_view);

  gtk_widget_class_bind_template_callback (widget_class, on_cancel_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_save_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_add_email_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_photo_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_remove_photo_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_name_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_field_changed);
}

static void
stamp_contact_editor_init (StampContactEditor *self)
{
  /* GcalDateChooser asks the context for the clock while it is being
   * built, so the context has to exist before the template is. */
  stamp_gcal_ensure_context ();

  gtk_widget_init_template (GTK_WIDGET (self));

  self->books = g_ptr_array_new_with_free_func (g_object_unref);
  self->email_rows = g_ptr_array_new ();
  self->cancellable = g_cancellable_new ();
}

void
stamp_contact_editor_present (GtkWidget   *parent,
                              EBookClient *client,
                              EContact    *contact)
{
  StampContactEditor *self = g_object_new (STAMP_TYPE_CONTACT_EDITOR, NULL);

  populate_books (self, client);

  if (self->books->len == 0 && !contact) {
    AdwDialog *alert;

    g_object_ref_sink (self);
    g_object_unref (self);

    alert = adw_alert_dialog_new (_("No Writable Address Book"),
                                  _("Adding a contact needs an address book that accepts changes. None of the address books in your accounts do."));
    adw_alert_dialog_add_response (ADW_ALERT_DIALOG (alert), "close", _("_Close"));
    adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "close");
    adw_dialog_present (alert, parent);
    return;
  }

  if (contact) {
    self->contact = g_object_ref (contact);
    g_set_object (&self->client, client);

    adw_window_title_set_title (ADW_WINDOW_TITLE (self->window_title), _("Edit Contact"));
    /* Moving a contact between books is a copy and a delete, not an edit. */
    gtk_widget_set_sensitive (self->book_row, FALSE);

    load_contact (self, contact);
  } else {
    self->name = e_contact_name_new ();
    add_email_row (self, NULL);
  }

  update_save_sensitivity (self);

  adw_dialog_present (ADW_DIALOG (self), parent);
}
