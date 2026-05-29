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

#include "stamp-contact-completion.h"

#include <ctype.h>

#include <libebook/libebook.h>
#include <libpsl.h>

#include "stamp-account.h"
#include "stamp-session.h"
#include "stamp-tag.h"

struct _StampContactCompletion {
  AdwBin parent_instance;

  GtkWidget *wrap_box;
  GtkWidget *entry;
  GtkWidget *popover;
  GtkWidget *view;

  GCancellable *cancellable;
  GtkSingleSelection *model;
  GListStore *store;
  GtkFilter *filter;

  GList *receivers;
  gboolean has_entries;
  StampAccount *account;
  gboolean block;
};

G_DEFINE_FINAL_TYPE (StampContactCompletion, stamp_contact_completion, ADW_TYPE_BIN);

#define PAGE_STEP 20

typedef enum {
  PROP_HAS_ENTRIES = 1,
} StampContactCompletionProps;

static GParamSpec *properties[PROP_HAS_ENTRIES + 1];

static void
on_tag_destroy (GtkWidget *obj,
                gpointer   user_data)
{
  StampTag *tag = STAMP_TAG (obj);
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);

  self->receivers = g_list_remove (self->receivers, tag);
  self->has_entries = g_list_length (self->receivers) > 0;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HAS_ENTRIES]);

  self->has_entries = g_list_length (self->receivers) > 0;
}

static void
on_activate (GtkListView *view,
             guint        position,
             gpointer     user_data)
{
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);
  g_autoptr (EContact) contact = g_list_model_get_item (G_LIST_MODEL (self->model), position);
  GSList *emails = e_contact_get (contact, E_CONTACT_EMAIL);
  GtkWidget *tag = stamp_tag_new (self->account);
  const gchar *name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);

  gtk_editable_set_text (GTK_EDITABLE (self->entry), "");

  stamp_tag_set_label (STAMP_TAG (tag), (char *)name);
  stamp_tag_set_mail (STAMP_TAG (tag), emails->data);

  stamp_contact_completion_add_tag (self, STAMP_TAG (tag));

  gtk_popover_popdown (GTK_POPOVER (self->popover));
}

static gboolean
is_valid_email (const gchar *str)
{
  static GRegex *regex = NULL;
  gboolean ret;

  if (!regex)
    regex = g_regex_new (
      "^[a-zA-Z0-9._%+\\-]+@[a-zA-Z0-9.\\-]+\\.[a-zA-Z]{2,}$",
      0, 0, NULL);

  ret = g_regex_match (regex, str, 0, NULL);
  if (ret) {
    const psl_ctx_t *psl = psl_builtin ();
    return psl_registrable_domain (psl, strchr (str, '@') + 1) != NULL;
  }

  return FALSE;
}

static gboolean
convert_to_tag (StampContactCompletion *self,
                const gchar            *text,
                gboolean                focus_leave)
{
  g_autofree char *stripped = g_strstrip (g_strdup (text));

  if (is_valid_email (stripped)) {
    GtkWidget *tag = stamp_tag_new (self->account);

    stamp_tag_set_label (STAMP_TAG (tag), stripped);
    stamp_tag_set_mail (STAMP_TAG (tag), stripped);

    stamp_contact_completion_add_tag (self, STAMP_TAG (tag));

    return TRUE;
  }

  return FALSE;
}

static void
on_focus_leave (GtkEventControllerFocus *controller,
                gpointer                 user_data)
{
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);
  GtkWidget *focus_widget = gtk_root_get_focus (gtk_widget_get_root (GTK_WIDGET (self)));

  if (focus_widget && gtk_widget_has_focus (self->entry) && gtk_widget_is_ancestor (focus_widget, GTK_WIDGET (self->entry)))
    return;

  if (convert_to_tag (self, gtk_editable_get_text (GTK_EDITABLE (self->entry)), TRUE))
    gtk_editable_set_text (GTK_EDITABLE (self->entry), "");

  gtk_popover_popdown (GTK_POPOVER (self->popover));
}

static void
on_items_changed (GListModel *model,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);
  GtkPopover *pop = GTK_POPOVER (self->popover);
  gint items = g_list_model_get_n_items (G_LIST_MODEL (self->model));
  const gchar *text = gtk_editable_get_text (GTK_EDITABLE (self->entry));

  if (items > 0 && text && strlen (text) > 0) {
    GdkRectangle rect = { 0, 0, 1, gtk_widget_get_height (self->wrap_box) };

    gtk_popover_set_position (GTK_POPOVER (pop), GTK_POS_BOTTOM);

    gtk_widget_set_halign (GTK_WIDGET (pop), GTK_ALIGN_START);
    gtk_popover_set_pointing_to (GTK_POPOVER (pop), &rect);
    gtk_popover_present (pop);
    gtk_popover_popup (pop);
  } else {
    gtk_popover_popdown (pop);
  }
}

static gboolean
on_key_pressed (GtkEventControllerKey  *controller,
                guint                   keyval,
                guint                   keycode,
                GdkModifierType         state,
                StampContactCompletion *self)
{
  gint selected, matches;

  if (state & (GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_CONTROL_MASK))
    return FALSE;

  if (keyval == GDK_KEY_Escape) {
    gtk_popover_popdown (GTK_POPOVER (self->popover));
    return TRUE;
  }

  if (keyval == GDK_KEY_BackSpace) {
    const gchar *text = gtk_editable_get_text (GTK_EDITABLE (self->entry));
    g_autofree char *mail = NULL;
    StampTag *tag;

    if (*text)
      return FALSE;

    if (!self->receivers)
      return TRUE;

    tag = g_list_last (self->receivers)->data;
    mail = g_strdup (stamp_tag_get_mail (tag));

    gtk_widget_unparent (GTK_WIDGET (tag));
    gtk_editable_set_text (GTK_EDITABLE (self->entry), mail);
    gtk_editable_set_position (GTK_EDITABLE (self->entry), -1);
    return TRUE;
  }

  if (keyval != GDK_KEY_Up && keyval != GDK_KEY_KP_Up &&
      keyval != GDK_KEY_Down && keyval != GDK_KEY_KP_Down &&
      keyval != GDK_KEY_Page_Up && keyval != GDK_KEY_KP_Page_Up &&
      keyval != GDK_KEY_Page_Down && keyval != GDK_KEY_KP_Page_Down) {
    return FALSE;
  }

  if (!gtk_widget_get_visible (self->popover))
    return FALSE;

  matches = g_list_model_get_n_items (G_LIST_MODEL (self->model));
  selected = gtk_single_selection_get_selected (self->model);

  if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up) {
    if (selected <= 0)
      selected = matches - 1;
    else
      selected--;
  } else if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) {
    if (selected < matches - 1)
      selected++;
    else if (selected == matches - 1)
      selected = 0;
    else
      selected = -1;
  } else if (keyval == GDK_KEY_Page_Up || keyval == GDK_KEY_KP_Page_Up) {
    if (selected == -1)
      selected = matches - 1;
    else if (selected == 0)
      selected = -1;
    else if (selected < PAGE_STEP)
      selected = 0;
    else
      selected -= PAGE_STEP;
  } else if (keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_KP_Page_Down) {
    if (selected == -1)
      selected = 0;
    else if (selected == matches - 1)
      selected = -1;
    else if (selected + PAGE_STEP > matches - 1)
      selected = matches - 1;
    else
      selected += PAGE_STEP;
  }

  if (selected < 0) {
    /* Unselect and restore text */
    gtk_single_selection_set_selected (self->model, GTK_INVALID_LIST_POSITION);
  } else if (selected < matches) {
    gtk_single_selection_set_selected (self->model, selected);
    gtk_list_view_scroll_to (GTK_LIST_VIEW (self->view), selected, GTK_LIST_SCROLL_NONE, NULL);
    /* set_selected_suggestion_as_url (self); */
  }

  return TRUE;
}

static void
on_entry_activate (GtkWidget *widget,
                   gpointer   user_data)
{
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);

  if (gtk_widget_get_visible (self->popover)) {
    guint position = gtk_single_selection_get_selected (self->model);

    if (position != GTK_INVALID_LIST_POSITION) {
      on_activate (NULL, position, self);
      return;
    }
  }

  if (convert_to_tag (self, gtk_editable_get_text (GTK_EDITABLE (self->entry)), TRUE))
    gtk_editable_set_text (GTK_EDITABLE (self->entry), "");

  gtk_popover_popdown (GTK_POPOVER (self->popover));
}

static void
on_search_contacts (GObject      *source,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) array = NULL;
  GSList *contacts;

  contacts = stamp_account_search_contacts_finish (self->account, NULL, res, &error);
  if (error) {
    g_warning ("%s: Could not search contacts: %s", G_STRFUNC, error->message);
    return;
  }

  array = g_ptr_array_new ();
  for (GSList *iter = contacts; iter && iter->data; iter = g_slist_next (iter)) {
    EContact *contact = E_CONTACT (iter->data);

    g_ptr_array_add (array, contact);
  }

  g_list_store_splice (self->store, 0, g_list_model_get_n_items (G_LIST_MODEL (self->store)), array->pdata, array->len);

  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
on_changed (GtkEditable *ed,
            gpointer     user_data)
{
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);
  g_autofree char *text = NULL;

  if (self->block)
    return;

  text = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->entry)));
  self->block = TRUE;

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  if (text) {
    g_auto (GStrv) split = g_strsplit (g_strstrip (text), ",", -1);
    GString *new_string = g_string_new (NULL);
    gint n_parts = g_strv_length (split);
    gboolean converted = FALSE;

    for (gint idx = 0; idx < n_parts; idx++) {
      gchar *part = g_strstrip (split[idx]);
      gboolean is_last = (idx == n_parts - 1);

      if (*part && is_valid_email (part) && !is_last) {
        convert_to_tag (self, part, FALSE);
        converted = TRUE;
      } else {
        if (new_string->len > 0)
          g_string_append (new_string, ", ");
        g_string_append (new_string, part);
      }
    }

    if (converted) {
      gtk_editable_set_text (GTK_EDITABLE (self->entry), new_string->str);
      gtk_editable_set_position (GTK_EDITABLE (self->entry), -1);
    }

    self->cancellable = g_cancellable_new ();
    stamp_account_search_contacts (self->account, NULL, new_string->str, self->cancellable, on_search_contacts, self);
  }

  self->block = FALSE;
}

static void
stamp_contact_completion_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (object);

  switch ((StampContactCompletionProps)property_id) {
    case PROP_HAS_ENTRIES:
      self->has_entries = g_value_get_boolean (value);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HAS_ENTRIES]);
      break;
  }
}

static void
stamp_contact_completion_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (object);

  switch ((StampContactCompletionProps)property_id) {
    case PROP_HAS_ENTRIES:
      g_value_set_boolean (value, self->has_entries);
      break;
  }
}


static void
stamp_contact_completion_class_init (StampContactCompletionClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/composer/stamp-contact-completion.ui");

  gtk_widget_class_bind_template_child (widget_class, StampContactCompletion, wrap_box);
  gtk_widget_class_bind_template_child (widget_class, StampContactCompletion, entry);

  gtk_widget_class_bind_template_callback (widget_class, on_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_entry_activate);
  gtk_widget_class_bind_template_callback (widget_class, on_focus_leave);
  gtk_widget_class_bind_template_callback (widget_class, on_key_pressed);

  object_class->set_property = stamp_contact_completion_set_property;
  object_class->get_property = stamp_contact_completion_get_property;

  properties[PROP_HAS_ENTRIES] =
    g_param_spec_boolean ("has-entries",
                          NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);
}

static gint
match (gpointer item,
       gpointer user_data)
{
  EContact *contact = E_CONTACT (item);
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);
  const gchar *name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
  const gchar *text = gtk_editable_get_text (GTK_EDITABLE (self->entry));
  g_autofree char *lower_text = NULL;

  if (!text)
    return 0;

  lower_text = g_ascii_strdown (text, -1);

  if (name) {
    g_autofree char *lower_name = g_ascii_strdown (name, -1);

    if (g_strstr_len (lower_name, -1, lower_text) != NULL)
      return 1;
  }

  if (e_contact_field_is_string (E_CONTACT_EMAIL)) {
    const gchar *mail = e_contact_get_const (contact, E_CONTACT_EMAIL);
    g_autofree char *lower_mail = g_ascii_strdown (mail, -1);

    if (g_strstr_len (lower_mail, -1, lower_text) != NULL)
      return 1;
  }

  return 0;
}

static gint
sort (gconstpointer a,
      gconstpointer b,
      gpointer      user_data)
{
  EContact *contact_a = (EContact *)a;
  EContact *contact_b = (EContact *)b;
  const gchar *name_a = e_contact_get_const (contact_a, E_CONTACT_FULL_NAME);
  const gchar *name_b = e_contact_get_const (contact_b, E_CONTACT_FULL_NAME);

  return g_strcmp0 (name_a, name_b);
}


static void
on_setup (GtkSignalListItemFactory *f,
          GtkListItem              *item,
          gpointer                  data)
{
  GtkWidget *row = adw_action_row_new ();
  GtkWidget *avatar = adw_avatar_new (32, "", TRUE);

  gtk_widget_set_hexpand (row, TRUE);
  adw_action_row_add_prefix (ADW_ACTION_ROW (row), avatar);
  g_object_set_data (G_OBJECT (row), "avatar", avatar);
  gtk_list_item_set_child (item, row);
}

static void
on_get_photo (gpointer photo,
              gpointer user_data)
{
  GtkWidget *avatar = GTK_WIDGET (user_data);

  if (photo)
    adw_avatar_set_custom_image (ADW_AVATAR (avatar), GDK_PAINTABLE (photo));
}

static void
on_bind (GtkSignalListItemFactory *f,
         GtkListItem              *item,
         gpointer                  user_data)
{
  EContact *contact = gtk_list_item_get_item (item);
  GtkWidget *row = gtk_list_item_get_child (item);
  g_autofree char *tmp = NULL;
  GtkWidget *avatar = g_object_get_data (G_OBJECT (row), "avatar");
  const gchar *name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
  GSList *emails = e_contact_get (contact, E_CONTACT_EMAIL);
  StampContactCompletion *self = STAMP_CONTACT_COMPLETION (user_data);

  adw_preferences_row_set_use_markup (ADW_PREFERENCES_ROW (row), TRUE);

  tmp = g_markup_escape_text (name, -1);
  adw_avatar_set_text (ADW_AVATAR (avatar), tmp);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), tmp);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (row), emails->data);

  stamp_account_get_photo (self->account,
                           emails->data,
                           NULL,
                           on_get_photo,
                           avatar);
}

static void
stamp_contact_completion_init (StampContactCompletion *self)
{
  GtkFilterListModel *filtered;
  GtkWidget *scrolled_window;
  GtkSorter *sorter;
  GtkSortListModel *sorted;
  GtkListItemFactory *factory;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->store = g_list_store_new (E_TYPE_CONTACT);

  self->filter = GTK_FILTER (gtk_custom_filter_new (match, self, NULL));
  filtered = gtk_filter_list_model_new (G_LIST_MODEL (self->store), GTK_FILTER (self->filter));

  sorter = GTK_SORTER (gtk_custom_sorter_new (sort, NULL, NULL));
  sorted = gtk_sort_list_model_new (G_LIST_MODEL (filtered), GTK_SORTER (sorter));

  self->popover = gtk_popover_new ();
  gtk_widget_set_hexpand (self->popover, TRUE);
  gtk_widget_add_css_class (self->popover, "menu");
  gtk_widget_add_css_class (self->popover, "contact-completion-popover");
  gtk_widget_set_parent (self->popover, self->wrap_box);
  gtk_widget_set_can_focus (self->popover, FALSE);
  gtk_popover_set_has_arrow (GTK_POPOVER (self->popover), FALSE);
  gtk_popover_set_autohide (GTK_POPOVER (self->popover), FALSE);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect_object (factory, "setup", G_CALLBACK (on_setup), self, 0);
  g_signal_connect_object (factory, "bind", G_CALLBACK (on_bind), self, 0);

  scrolled_window = gtk_scrolled_window_new ();
  gtk_widget_set_hexpand (scrolled_window, TRUE);
  /* gtk_widget_set_halign(scrolled_window, GTK_ALIGN_START); */
  gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scrolled_window), 400);
  gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (scrolled_window), TRUE);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scrolled_window), TRUE);
  self->model = gtk_single_selection_new (G_LIST_MODEL (sorted));
  g_signal_connect_object (self->model, "items-changed", G_CALLBACK (on_items_changed), self, 0);
  self->view = gtk_list_view_new (GTK_SELECTION_MODEL (self->model), factory);
  gtk_widget_set_size_request (self->view, 300, -1);
  gtk_scrolled_window_set_policy (
    GTK_SCROLLED_WINDOW (scrolled_window),
    GTK_POLICY_NEVER,
    GTK_POLICY_AUTOMATIC
    );
  gtk_widget_set_hexpand (self->view, TRUE);
  gtk_widget_set_halign (self->view, GTK_ALIGN_START);
  gtk_list_view_set_single_click_activate (GTK_LIST_VIEW (self->view), TRUE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), self->view);
  gtk_popover_set_child (GTK_POPOVER (self->popover), scrolled_window);

  g_signal_connect_object (self->view, "activate", G_CALLBACK (on_activate), self, 0);
}

GtkWidget *
stamp_contact_completion_new (void)
{
  return g_object_new (STAMP_TYPE_CONTACT_COMPLETION, NULL);
}

GList *
stamp_contact_completion_get_tags (StampContactCompletion *self)
{
  return self->receivers;
}

void
stamp_contact_completion_add_tag (StampContactCompletion *self,
                                  StampTag               *tag)
{
  g_signal_connect_object (tag, "destroy", G_CALLBACK (on_tag_destroy), self, 0);

  adw_wrap_box_append (ADW_WRAP_BOX (self->wrap_box), GTK_WIDGET (tag));
  self->receivers = g_list_append (self->receivers, tag);
  self->has_entries = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HAS_ENTRIES]);

  adw_wrap_box_reorder_child_after (ADW_WRAP_BOX (self->wrap_box), self->entry, GTK_WIDGET (tag));
}

void
stamp_contact_completion_set_account (StampContactCompletion *self,
                                      StampAccount           *account)
{
  self->account = account;
}

gboolean
stamp_contact_completion_contains_address (StampContactCompletion *self,
                                           const gchar            *mail)
{
  for (GList *iter = self->receivers; iter && iter->data; iter = g_list_next (iter)) {
    StampTag *tag = STAMP_TAG (iter->data);

    if (g_strcmp0 (stamp_tag_get_mail (tag), mail) == 0)
      return TRUE;
  }

  return FALSE;
}
