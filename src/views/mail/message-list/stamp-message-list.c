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

#include "stamp-conversation-item.h"
#include "stamp-conversation-list.h"
#include "stamp-mail-view.h"
#include "stamp-mime-parser.h"
#include "stamp-message-list-item.h"
#include "stamp-session.h"
#include "stamp-window.h"

#include <glib/gi18n.h>

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
  GtkWidget *external;

  GtkButton *unsubscribe_button;
  char *unsubscribe_sender;
  char *unsubscribe_url;
  CamelMimeMessage *unsubscribe_message;

  GCancellable *cancellable;

  GHashTable *messages;
  StampAccount *account;

  char *subject;

  GtkWidget *carousel;
  GtkWidget *prev_page;
  GtkWidget *next_page;
  GtkWidget *message_stack;
  gboolean rebuilding;
};

G_DEFINE_FINAL_TYPE (StampMessageList, stamp_message_list, ADW_TYPE_BREAKPOINT_BIN);

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
on_unsubscribe_response (GtkWidget *dialog,
                         char      *response,
                         gpointer   user_data)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (user_data);

  if (g_strcmp0 (response, "confirm") == 0) {
    g_autoptr (StampMimeParser) parser = NULL;

    parser = stamp_mime_parser_new (CAMEL_SESSION (stamp_session_get_default ()));
    stamp_mime_parser_parse (parser, self->unsubscribe_message, self->cancellable, NULL);
    stamp_mime_parser_send_unsubscribe (parser, NULL, self->cancellable);
    gtk_widget_set_visible (GTK_WIDGET (self->unsubscribe_button), FALSE);
  }
}

static void
on_unsubscribe_clicked (GtkWidget *button,
                        gpointer   user_data)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (user_data);
  AdwDialog *dialog;
  const char *sender = self->unsubscribe_sender;
  g_autofree char *body = g_strdup_printf (_("Are you sure you want to unsubscribe from the mailing list?\n\nSender: %s\nUnsubscribe URL: %s"), sender, self->unsubscribe_url);

  dialog = adw_alert_dialog_new (_("Unsubscribe"), body);

  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "cancel", _("Cancel"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "confirm", _("Unsubscribe"));
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "confirm", ADW_RESPONSE_DESTRUCTIVE);

  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
  g_signal_connect (dialog, "response", G_CALLBACK (on_unsubscribe_response), self);

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

enum {
  SWITCH_CONVERSATION,
  NAVIGATE_BACK,
  LAST_SIGNAL_SWITCH
};

static gint switch_signals[LAST_SIGNAL_SWITCH] = { 0 };

static void
on_carousel_page_changed (AdwCarousel *carousel,
                          guint        index,
                          gpointer     user_data)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (user_data);

  if (self->rebuilding)
    return;

  if (index == 0) {
    g_signal_emit (self, switch_signals[SWITCH_CONVERSATION], 0, -1);
  } else if (index == 2) {
    g_signal_emit (self, switch_signals[SWITCH_CONVERSATION], 0, 1);
  }
}

static void
emit_navigate_back_idle (gpointer user_data)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (user_data);

  g_signal_emit (self, switch_signals[NAVIGATE_BACK], 0);
  g_object_unref (self);
}

static void
on_drag_begin (GtkGestureDrag *gesture,
               double          start_x,
               double          start_y,
               gpointer        user_data)
{
  gboolean at_left_edge = start_x < 50;

  g_object_set_data (G_OBJECT (gesture), "triggered", GINT_TO_POINTER (FALSE));
  g_object_set_data (G_OBJECT (gesture), "edge-left", GINT_TO_POINTER (at_left_edge));

  if (!at_left_edge)
    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
}

static void
on_drag_update (GtkGestureDrag *gesture,
                double          offset_x,
                double          offset_y,
                gpointer        user_data)
{
  StampMessageList *self = STAMP_MESSAGE_LIST (user_data);

  if (g_object_get_data (G_OBJECT (gesture), "triggered"))
    return;

  if (g_object_get_data (G_OBJECT (gesture), "edge-left") && offset_x > 20) {
    g_object_set_data (G_OBJECT (gesture), "triggered", GINT_TO_POINTER (TRUE));
    g_idle_add_once (emit_navigate_back_idle, g_object_ref (self));
  }
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
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, unsubscribe_button);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, external);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, carousel);
  gtk_widget_class_bind_template_child (widget_class, StampMessageList, message_stack);

  gtk_widget_class_bind_template_callback (widget_class, on_message_search_entry_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_unsubscribe_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_drag_begin);
  gtk_widget_class_bind_template_callback (widget_class, on_drag_update);
  gtk_widget_class_bind_template_callback (widget_class, on_carousel_page_changed);

  signals[HOVERING_OVER_LINK] = g_signal_new ("hovering-over-link", G_OBJECT_CLASS_TYPE (klass),
                                              G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL, NULL,
                                              G_TYPE_NONE,
                                              2, G_TYPE_STRING, G_TYPE_STRING);

  switch_signals[SWITCH_CONVERSATION] = g_signal_new ("switch-conversation", G_OBJECT_CLASS_TYPE (klass),
                                                      G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                                      0, NULL, NULL, NULL,
                                                      G_TYPE_NONE,
                                                      1, G_TYPE_INT);

  switch_signals[NAVIGATE_BACK] = g_signal_new ("navigate-back", G_OBJECT_CLASS_TYPE (klass),
                                                G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                                0, NULL, NULL, NULL,
                                                G_TYPE_NONE, 0);
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

  item = STAMP_MESSAGE_LIST_ITEM (row);
  subject = camel_message_info_get_subject (stamp_message_list_item_get_message_info (item));
  if (subject_changed (self->subject, (char *)subject)) {
    GtkWidget *label = gtk_label_new (subject);

    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_widget_add_css_class (label, "title-2");
    gtk_widget_add_css_class (label, "message-list-header");
    gtk_widget_add_css_class (label, "message-list-header-between");

    g_set_str (&self->subject, subject);

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

static GtkWidget *
find_last_of_type (GtkListBox *listbox,
                   GType       type)
{
  GtkWidget *result = NULL;

  for (GtkWidget *row = gtk_widget_get_first_child (GTK_WIDGET (listbox)); row != NULL; row = gtk_widget_get_next_sibling (row)) {
    if (G_TYPE_CHECK_INSTANCE_TYPE (row, type))
      result = row;
  }

  return result;
}

static void
populate_list_box_from_thread (StampAccount          *account,
                               GtkListBox            *list_box,
                               CamelFolderThreadNode *node)
{
  CamelFolderThreadNode *current_node = node;

  while (current_node) {
    GtkWidget *item = stamp_message_list_item_new (account, current_node);
    CamelMessageInfo *message = camel_folder_thread_node_get_item (current_node);

    gtk_list_box_append (list_box, item);
    if ((camel_message_info_get_flags (message) & CAMEL_MESSAGE_SEEN) == 0) {
      stamp_message_list_item_set_expanded (STAMP_MESSAGE_LIST_ITEM (item), TRUE);
    }

    if (camel_folder_thread_node_get_next (current_node))
      populate_list_box_from_thread (account, list_box, camel_folder_thread_node_get_next (current_node));

    current_node = camel_folder_thread_node_get_child (current_node);
  }
}

static GtkWidget *
create_carousel_page_view (StampAccount          *account,
                           CamelFolderThreadNode *node)
{
  GtkWidget *toolbar_view;
  GtkWidget *header_bar;
  GtkWidget *scrolled;
  GtkWidget *list_box;
  GtkWidget *subject_label;
  CamelMessageInfo *message;

  toolbar_view = adw_toolbar_view_new ();
  gtk_widget_set_hexpand (toolbar_view, TRUE);
  gtk_widget_set_vexpand (toolbar_view, TRUE);

  header_bar = adw_header_bar_new ();
  adw_header_bar_set_show_title (ADW_HEADER_BAR (header_bar), FALSE);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar_view), header_bar);

  message = camel_folder_thread_node_get_item (node);

  subject_label = gtk_label_new (camel_message_info_get_subject (message));
  gtk_label_set_xalign (GTK_LABEL (subject_label), 0);
  gtk_label_set_wrap (GTK_LABEL (subject_label), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (subject_label), PANGO_WRAP_WORD_CHAR);
  gtk_widget_add_css_class (subject_label, "title-2");

  {
    GtkWidget *content_box;
    GtkWidget *header_box;

    content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand (content_box, TRUE);
    gtk_widget_set_vexpand (content_box, TRUE);
    gtk_widget_add_css_class (content_box, "message-list-conversation");

    header_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append (GTK_BOX (header_box), subject_label);
    gtk_box_append (GTK_BOX (content_box), header_box);

    scrolled = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand (scrolled, TRUE);
    gtk_widget_set_vexpand (scrolled, TRUE);

    list_box = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class (list_box, "message-list");
    gtk_widget_add_css_class (list_box, "background");
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list_box);

    {
      CamelMessageInfo *root_msg = camel_folder_thread_node_get_item (node);
      GtkWidget *item = stamp_message_list_item_new (account, node);
      gtk_list_box_append (GTK_LIST_BOX (list_box), item);
      if ((camel_message_info_get_flags (root_msg) & CAMEL_MESSAGE_SEEN) == 0)
        stamp_message_list_item_set_expanded (STAMP_MESSAGE_LIST_ITEM (item), TRUE);
    }
    if (camel_folder_thread_node_get_child (node))
      populate_list_box_from_thread (account, GTK_LIST_BOX (list_box), camel_folder_thread_node_get_child (node));

    gtk_box_append (GTK_BOX (content_box), scrolled);

    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), content_box);
  }

  return toolbar_view;
}

void
stamp_message_list_set_conversation (StampMessageList      *self,
                                     StampAccount          *account,
                                     CamelFolderThreadNode *node)
{
  GtkWidget *item;
  GtkWidget *child;
  CamelMessageInfo *message;
  GAction *action;
  GSimpleActionGroup *action_group;
  StampWindow *window = STAMP_WINDOW (stamp_get_main_window ());
  StampMailView *mail_view = stamp_window_get_mail_view (window);
  CamelFolderSummary *summary;
  CamelFolder *folder;
  const char *fname;
  gboolean draft_folder = FALSE;
  StampConversationList *conv_list;
  StampConversationItem *adjacent;

  self->account = account;

  self->rebuilding = TRUE;

  if (!self->prev_page) {
    self->prev_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand (self->prev_page, TRUE);
    gtk_widget_set_vexpand (self->prev_page, TRUE);
    adw_carousel_prepend (ADW_CAROUSEL (self->carousel), self->prev_page);
  }
  if (!self->next_page) {
    self->next_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand (self->next_page, TRUE);
    gtk_widget_set_vexpand (self->next_page, TRUE);
    adw_carousel_append (ADW_CAROUSEL (self->carousel), self->next_page);
  }

  adw_carousel_reorder (ADW_CAROUSEL (self->carousel), self->prev_page, 0);
  adw_carousel_reorder (ADW_CAROUSEL (self->carousel), self->message_stack, 1);
  adw_carousel_reorder (ADW_CAROUSEL (self->carousel), self->next_page, 2);

  conv_list = stamp_mail_view_get_conversation_list (mail_view);

  adjacent = stamp_conversation_list_get_adjacent_item (conv_list, -1);
  if (adjacent) {
    GtkWidget *prev_child = gtk_widget_get_first_child (self->prev_page);
    while (prev_child) {
      GtkWidget *next = gtk_widget_get_next_sibling (prev_child);
      gtk_box_remove (GTK_BOX (self->prev_page), prev_child);
      prev_child = next;
    }
    gtk_box_append (GTK_BOX (self->prev_page),
                    create_carousel_page_view (self->account, stamp_conversation_item_get_node (adjacent)));
    g_object_unref (adjacent);
  }

  adjacent = stamp_conversation_list_get_adjacent_item (conv_list, 1);
  if (adjacent) {
    GtkWidget *next_child = gtk_widget_get_first_child (self->next_page);
    while (next_child) {
      GtkWidget *next = gtk_widget_get_next_sibling (next_child);
      gtk_box_remove (GTK_BOX (self->next_page), next_child);
      next_child = next;
    }
    gtk_box_append (GTK_BOX (self->next_page),
                    create_carousel_page_view (self->account, stamp_conversation_item_get_node (adjacent)));
    g_object_unref (adjacent);
  }

  adw_carousel_scroll_to (ADW_CAROUSEL (self->carousel), self->message_stack, FALSE);
  self->rebuilding = FALSE;

  gtk_list_box_remove_all (GTK_LIST_BOX (self->list_box));

  g_clear_pointer (&self->messages, g_hash_table_unref);
  action_group = stamp_mail_view_get_action_group (mail_view);

  action = g_action_map_lookup_action (G_ACTION_MAP (action_group), "reply-current");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), node != NULL);

  action = g_action_map_lookup_action (G_ACTION_MAP (action_group), "reply-all-current");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), node != NULL);

  action = g_action_map_lookup_action (G_ACTION_MAP (action_group), "forward-current");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), node != NULL);

  if (!node) {
    /* Show empty stack */
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");
    return;
  }

  /* E-D-S does not mark messages as DRAFT nor the folder as TYPE_DRAFTS for MS365…, check full name for the moment
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
  gtk_widget_set_visible (GTK_WIDGET (self->unsubscribe_button), FALSE);

  self->subject = g_strdup (camel_message_info_get_subject (camel_folder_thread_node_get_item (node)));

  item = stamp_message_list_item_new (self->account, node);
  if ((camel_message_info_get_flags (message) & CAMEL_MESSAGE_SEEN) == 0) {
    stamp_message_list_item_set_expanded (STAMP_MESSAGE_LIST_ITEM (item), TRUE);
  }
  gtk_list_box_append (GTK_LIST_BOX (self->list_box), GTK_WIDGET (item));

  g_hash_table_insert (self->messages, g_strdup (camel_message_info_get_uid (message)), g_object_ref (item));
  if (camel_folder_thread_node_get_child (node))
    go_down (self, camel_folder_thread_node_get_child (node));

  child = find_last_of_type (GTK_LIST_BOX (self->list_box), STAMP_TYPE_MESSAGE_LIST_ITEM);
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
    g_clear_object (&data->self);
    g_clear_object (&data->item);
    g_clear_pointer (&data, g_free);
    return;
  }

  if (!body) {
    g_warning ("Could not get message body html: empty response");
    g_clear_object (&data->self);
    g_clear_object (&data->item);
    g_clear_pointer (&data, g_free);
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

  /* Ensure that last item is actually a message list item and not a header */
  while (child && !STAMP_IS_MESSAGE_LIST_ITEM (child))
    child = gtk_widget_get_prev_sibling (child);

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

void
stamp_message_list_set_unsubscribe (StampMessageList *self,
                                    const char       *sender,
                                    const char       *url,
                                    CamelMimeMessage *message)
{
  gtk_widget_set_visible (GTK_WIDGET (self->unsubscribe_button), TRUE);
  g_set_str (&self->unsubscribe_sender, sender);
  g_set_str (&self->unsubscribe_url, url);
  g_set_object (&self->unsubscribe_message, message);
}

void
stamp_message_list_set_external (StampMessageList *self,
                                 gboolean          is_external)
{
  gtk_widget_set_visible (self->external, is_external);
}
