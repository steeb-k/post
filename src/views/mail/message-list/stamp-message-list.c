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

#include "stamp-message-list.h"

#include "stamp-mail-view.h"
#include "stamp-message-list-item.h"
#include "stamp-window.h"

struct _StampMessageList {
  AdwBreakpointBin parent_instance;

  GtkWidget *scrolled_window;
  GtkWidget *list_box;
  GtkWidget *message_title;
  GtkWidget *hover_url;
  GtkWidget *stack;
  GtkWidget *search_bar;
  GtkWidget *search_entry;
  GtkWidget *edit_button;

  GHashTable *messages;
  StampAccount *account;

  char *subject;
};

G_DEFINE_FINAL_TYPE (StampMessageList, stamp_message_list, ADW_TYPE_BREAKPOINT_BIN)

enum {
  HOVERING_OVER_LINK,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

static void
on_message_search_entry_changed (GtkWidget *search_entry,
                                 gpointer   user_data)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (user_data);
  GList *keys;
  const char *search_text = gtk_editable_get_text (GTK_EDITABLE (self->search_entry));

  if (!self->messages)
    return;

  keys = g_hash_table_get_keys (self->messages);
  for (GList *iter = keys; iter && iter->data; iter = g_list_next (iter)) {
    const char *key = iter->data;
    StampMessageListItem *item = STAMP_MESSAGE_LIST_ITEM (g_hash_table_lookup (self->messages, key));

    stamp_message_list_item_search (item, search_text);
  }
}

static void
stamp_message_list_dispose (GObject *object)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (object);

  g_clear_pointer (&self->messages, g_hash_table_unref);

  G_OBJECT_CLASS (stamp_message_list_parent_class)->dispose (object);
}

static void
stamp_message_list_class_init (StampMessageListClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->dispose = stamp_message_list_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/message-list/stamp-message-list.ui");

  gtk_widget_class_bind_template_child (widget_class, StampMessageList, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, list_box);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, message_title);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, hover_url);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, stack);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, search_bar);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, search_entry);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, edit_button);

  gtk_widget_class_bind_template_callback (widget_class, on_message_search_entry_changed);

  signals[HOVERING_OVER_LINK] = g_signal_new ("hovering-over-link", G_OBJECT_CLASS_TYPE (klass),
                                              G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL, NULL,
                                              G_TYPE_NONE,
                                              2, G_TYPE_STRING, G_TYPE_STRING);
}

static gint
sort_mails (GtkListBoxRow *row1,
            GtkListBoxRow *row2,
            gpointer       user_data)
{
  StampMessageListItem *item1 = STAMP_MESSAGE_LIST_ITEM (row1);
  StampMessageListItem *item2 = STAMP_MESSAGE_LIST_ITEM (row2);
  guint ts1 = stamp_message_list_item_get_timestamp (item1);
  guint ts2 = stamp_message_list_item_get_timestamp (item2);

  if (ts1 < ts2)
    return -1;
  if (ts1 > ts2)
    return 1;
  return 0;
}

static gboolean
subject_changed (const char *subject,
                 char       *new_subject)
{
  /* Skip prefix Re: / Fwd: / Yes: / No: / .... */
  g_auto (GStrv) split = g_strsplit (new_subject, ":", 2);
  char *check = new_subject;
  gboolean ret;

  if (g_strv_length (split) == 2)
    check = split[1];

  g_strstrip (check);
  ret = g_strstr_len (subject, -1, check) == NULL;

  return ret;
}

static void
update_header (GtkListBoxRow *row,
               GtkListBoxRow *before,
               gpointer       user_data)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (user_data);
  StampMessageListItem *item;
  const char *subject;

  if (!before)
    return;

  item = STAMP_MESSAGE_LIST_ITEM (before);
  subject = camel_message_info_get_subject (stamp_message_list_item_get_message_info (item));
  if (subject_changed (self->subject, (char *) subject)) {
    GtkWidget *label = gtk_label_new (subject);

    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_widget_add_css_class (label, "title-2");
    gtk_widget_add_css_class (label, "message-list-header");
    g_clear_pointer (&self->subject, g_free);
    self->subject = g_strdup (subject);

    gtk_list_box_row_set_header (row, label);
  } else {
    gtk_list_box_row_set_header (row, NULL);
  }
}

static void
stamp_message_list_init (StampMessageList *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  stamp_message_list_hovering_over_link (self, NULL, NULL);

  gtk_list_box_set_sort_func (GTK_LIST_BOX (self->list_box), sort_mails, NULL, NULL);
  gtk_list_box_set_header_func (GTK_LIST_BOX (self->list_box), update_header, self, NULL);

  gtk_search_bar_connect_entry (GTK_SEARCH_BAR (self->search_bar), GTK_EDITABLE (self->search_entry));
}

GtkWidget *
stamp_message_list_new (void)
{
  return g_object_new (STAMP_TYPE_MESSAGE_LIST, NULL);
}

static void
go_down (StampMessageList      *self,
         CamelFolderThreadNode *node)
{
  CamelFolderThreadNode *current_node = node;

  while (current_node) {
    GtkWidget *item = stamp_message_list_item_new (self->account, current_node);
    CamelMessageInfo *message = camel_folder_thread_node_get_item (current_node);

    gtk_list_box_append (GTK_LIST_BOX (self->list_box), item);
    if ((camel_message_info_get_flags (message) & CAMEL_MESSAGE_SEEN) == 0) {
      stamp_message_list_item_set_expanded (STAMP_MESSAGE_LIST_ITEM (item), TRUE);
    }
    g_hash_table_insert (self->messages, g_strdup (camel_message_info_get_uid (message)), g_object_ref (item));

    if (camel_folder_thread_node_get_next (current_node))
      go_down (self, camel_folder_thread_node_get_next (current_node));

    current_node = camel_folder_thread_node_get_child (current_node);
  }
}

static void
scroll_to_bottom (gpointer user_data)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (user_data);
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));

  gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj));
}

void
stamp_message_list_set_conversation (StampMessageList      *self,
                                     StampAccount          *account,
                                     CamelFolderThreadNode *node)
{
  GtkWidget *item;
  GtkWidget *child;
  CamelMessageInfo *message;
  /* GtkListBoxRow *row; */
  GAction *action;
  GSimpleActionGroup *action_group;
  StampWindow *window = STAMP_WINDOW (stamp_get_main_window ());
  StampMailView *mail_view = stamp_window_get_mail_view (window);
  /* int idx = 0; */
  CamelFolderSummary *summary;
  CamelFolder *folder;
  const char *fname;
  gboolean draft_folder = FALSE;

  self->account = account;

  gtk_list_box_remove_all (GTK_LIST_BOX (self->list_box));

  g_clear_pointer (&self->messages, g_hash_table_unref);
  action_group = stamp_mail_view_get_action_group (mail_view);

  /* Reply */
  action = g_action_map_lookup_action (G_ACTION_MAP (action_group),
                                       "reply-current");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), node != NULL);

  /* Reply All */
  action = g_action_map_lookup_action (G_ACTION_MAP (action_group),
                                       "reply-all-current");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), node != NULL);

  /* Forward */
  action = g_action_map_lookup_action (G_ACTION_MAP (action_group),
                                       "forward-current");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), node != NULL);

  if (!node) {
    /* Show empty stack */
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");
    return;
  }

  /* TODO: E-D-S does not mark messages as DRAFT nor the folder as TYPE_DRAFTS for MS365…, check full name for the moment
   */
  message = camel_folder_thread_node_get_item (node);
  g_object_get (G_OBJECT (message), "summary", &summary, NULL);
  folder = camel_folder_summary_get_folder (summary);
  fname = camel_folder_get_full_name (folder);
  action = g_action_map_lookup_action (G_ACTION_MAP (action_group), "edit");

  if (g_strrstr (fname, "Drafts") || g_strrstr (fname, "Entwürfe")) {
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), TRUE);
    draft_folder = TRUE;
  } else {
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), FALSE);
  }

  gtk_widget_set_visible (self->edit_button, draft_folder);

  gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "content");

  self->messages = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  gtk_label_set_text (GTK_LABEL (self->message_title), camel_message_info_get_subject (camel_folder_thread_node_get_item (node)));

  self->subject = g_strdup (camel_message_info_get_subject (camel_folder_thread_node_get_item (node)));

  item = stamp_message_list_item_new (self->account, node);
  if ((camel_message_info_get_flags (message) & CAMEL_MESSAGE_SEEN) == 0) {
    stamp_message_list_item_set_expanded (STAMP_MESSAGE_LIST_ITEM (item), TRUE);
  }
  gtk_list_box_append (GTK_LIST_BOX (self->list_box), GTK_WIDGET (item));

  g_hash_table_insert (self->messages, g_strdup (camel_message_info_get_uid (message)), g_object_ref (item));
  if (camel_folder_thread_node_get_child (node))
    go_down (self, camel_folder_thread_node_get_child (node));

  child = gtk_widget_get_last_child (self->list_box);
  if (child && STAMP_IS_MESSAGE_LIST_ITEM (child) && STAMP_MESSAGE_LIST_ITEM (child)) {
    StampMessageListItem *list_item = STAMP_MESSAGE_LIST_ITEM (child);

    stamp_message_list_item_set_expanded (list_item, TRUE);
    item = GTK_WIDGET (list_item);
  }

  g_idle_add_once (scroll_to_bottom, self);
}

void
stamp_message_list_hovering_over_link (StampMessageList *self,
                                       const char       *title,
                                       const char       *url)
{
  if (!url) {
    gtk_widget_set_visible (self->hover_url, FALSE);
  } else {
    gtk_label_set_text (GTK_LABEL (self->hover_url), url);
    gtk_widget_set_visible (self->hover_url, TRUE);
  }
}

typedef struct {
  StampMessageList *self;
  StampComposerType type;
  StampMessageListItem *item;
} ComposerData;

static void
on_message_body (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  GtkWidget *composer;
  ComposerData *data = user_data;
  g_autoptr (GError) error = NULL;
  char *body;

  body = stamp_message_list_item_get_message_body_html_finish (data->item, res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Could not get message body html: %s", error->message);
    return;
  }

  composer = stamp_composer_new_with_quote (data->type,
                                            stamp_message_list_item_get_uid (data->item),
                                            data->self->account,
                                            stamp_message_list_item_get_web_view (data->item),
                                            stamp_message_list_item_get_message_info (data->item),
                                            stamp_message_list_item_get_message (data->item),
                                            body);
  gtk_window_present (GTK_WINDOW (composer));

  g_clear_object (&data->self);
  g_clear_object (&data->item);
  g_clear_pointer (&data, g_free);
}

void
stamp_message_list_compose (StampMessageList  *self,
                            StampComposerType  type,
                            GVariant          *parameter)
{
  GtkWidget *child;
  ComposerData *data;
  const char *uid = NULL;

  if (type == STAMP_COMPOSER_NEW) {
    GtkWidget *composer;

    composer = stamp_composer_new (self->account);
    gtk_window_present (GTK_WINDOW (composer));
    return;
  }

  if (parameter)
    uid = g_variant_get_string (parameter, NULL);

  if (uid && strlen (uid)) {
    child = g_hash_table_lookup (self->messages, uid);
  } else {
    child = gtk_widget_get_last_child (self->list_box);
  }

  if (!child)
    return;

  data = g_new0 (ComposerData, 1);

  data->item = g_object_ref (STAMP_MESSAGE_LIST_ITEM (child));
  data->self = g_object_ref (self);
  data->type = type;

  stamp_message_list_item_get_message_body_html (data->item, NULL, on_message_body, data);
}

void
stamp_message_list_print (StampMessageList *self,
                          GVariant         *parameter)
{
  const char *uid = NULL;
  GtkWidget *child;
  StampMessageListItem *item;

  if (parameter)
    uid = g_variant_get_string (parameter, NULL);

  if (uid && strlen (uid)) {
    child = g_hash_table_lookup (self->messages, uid);
  } else {
    child = gtk_widget_get_last_child (self->list_box);
  }

  if (!child)
    return;

  item = STAMP_MESSAGE_LIST_ITEM (child);

  stamp_message_list_item_print (item);
}

void
stamp_message_list_view_source (StampMessageList *self,
                                GVariant         *parameter)
{
  GtkWidget *child;
  StampMessageListItem *item;

  child = gtk_widget_get_last_child (self->list_box);

  if (!child)
    return;

  item = STAMP_MESSAGE_LIST_ITEM (child);

  stamp_message_list_item_view_source (item);
}
