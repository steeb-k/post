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

#include "stamp-conversation-list.h"

#include "stamp-account.h"
#include "stamp-conversation-list-store.h"
#include "stamp-conversation-item.h"
#include "stamp-conversation-row.h"
#include "stamp-composer.h"
#include "stamp-item.h"
#include "stamp-message-list.h"

#include <camel/camel.h>
#include <glib/gi18n.h>

#define INITIAL_LOAD_COUNT 200
#define BATCH_LOAD_COUNT 100

enum FilterMode {
  FILTER_MODE_ALL,
  FILTER_MODE_STARRED,
  FILTER_MODE_ATTACHMENT,
  FILTER_MODE_UNREAD,
};

enum SortMode {
  SORT_MODE_NEWEST_FIRST,
  SORT_MODE_OLDEST_FIRST,
};

struct _StampConversationList {
  AdwBin parent_instance;

  GtkWidget *listview;
  GtkWidget *search_bar;
  GtkWidget *search_entry;
  GtkWidget *window_title;
  GtkWidget *scroll_to_top;
  GtkWidget *scrolled_window;
  GtkWidget *header_stack;
  GtkWidget *normal_headerbar;
  GtkWidget *selection_headerbar;
  GtkWidget *selection_label;
  GtkWidget *spinner;
  GtkAdjustment *vadj;
  GtkWidget *sort_button;
  GtkWidget *mail_list_stack;
  GtkBitset *selected;
  GtkToggleButton *sidebar_button;
  GtkBox *sort_is_active;

  GListStore *list_store;
  GtkSingleSelection *single_selection;
  GtkNoSelection *multi_selection;
  GCancellable *cancellable;
  GCancellable *transfer_cancellable;
  GHashTable *folders;
  GList *moved_messages;
  gint mark_read_timeout_id;
  gboolean is_pulling;
  GtkSorter *sorter;
  GtkFilter *filter;
  char *full_name;

  StampAccount *account;
  CamelFolder *folder;
  gboolean selection_mode;

  enum FilterMode filter_mode;
  enum SortMode sort_mode;
  gboolean claimed;

  CamelFolderThread *thread;
  GHashTable *thread_cache;
  GtkFilterListModel *filter_model;
  guint anchor_position;
  GHashTable *position_to_check;
  GPtrArray *trash_array;
  guint pending_load_count;

  GSimpleActionGroup *actions;
  GMenu *cat_menu;
};

G_DEFINE_FINAL_TYPE (StampConversationList, stamp_conversation_list, ADW_TYPE_BIN);

enum {
  PROP_0,
  PROP_STATE,
  LAST_PROP
};

static GParamSpec *properties[LAST_PROP];

enum {
  CONVERSATION_SELECTED,
  CONVERSATION_TRASH,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

static StampConversationItem *
stamp_conversation_item_find_item (GListStore *store,
                                   const char *uid)
{
  guint list_len = g_list_model_get_n_items (G_LIST_MODEL (store));

  for (guint idx = 0; idx < list_len; idx++) {
    g_autoptr (StampConversationItem) item = g_list_model_get_item (G_LIST_MODEL (store), idx);
    CamelFolderThreadNode *node;

    if (item && g_strcmp0 (stamp_conversation_item_get_uid (item), uid) == 0) {
      return g_steal_pointer (&item);
    }

    node = stamp_conversation_item_get_node (item);
    for (CamelFolderThreadNode *iter = camel_folder_thread_node_get_child (node); iter; iter = camel_folder_thread_node_get_next (iter)) {
      CamelMessageInfo *info = camel_folder_thread_node_get_item (iter);

      if (g_strcmp0 (camel_message_info_get_uid (info), uid) == 0)
        return g_steal_pointer (&item);
    }
  }
  return NULL;
}

static void
load_folder_idle (gpointer user_data);

static gboolean
load_more_items_idle (gpointer user_data);


static void
on_conversation_list_folder_changed (CamelFolder           *folder,
                                     CamelFolderChangeInfo *changes,
                                     gpointer               user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  if (changes->uid_changed && changes->uid_changed->len > 0) {
    GPtrArray *changed = changes->uid_changed;
    for (int idx = 0; idx < changed->len; idx++) {
      StampConversationItem *item = stamp_conversation_item_find_item (self->list_store, changed->pdata[idx]);
      CamelMessageInfo *message_info;
      char *uid = (char *)changed->pdata[idx];

      message_info = camel_folder_summary_get (camel_folder_get_folder_summary (folder), uid);

      if (item)
        stamp_conversation_item_update (item, message_info);
    }
  }

  if ((changes->uid_added && changes->uid_added->len > 0) || (changes->uid_removed && changes->uid_removed->len > 0)) {
    g_idle_add_once (load_folder_idle, self);
  }
}

static CamelFolderThread *
get_thread (StampConversationList *self,
            CamelFolder           *folder)
{
  const char *uri = camel_folder_get_full_name (folder);
  CamelFolderThread *thread = g_hash_table_lookup (self->thread_cache, uri);

  if (!thread) {
    g_autoptr (GPtrArray) uids = camel_folder_dup_uids (folder);

    thread = camel_folder_thread_new (folder, uids, CAMEL_FOLDER_THREAD_FLAG_SORT);
    if (thread)
      g_hash_table_insert (self->thread_cache, g_strdup (uri), g_object_ref (thread));
  }

  return thread;
}


static gint
sorter_func (gconstpointer a,
             gconstpointer b,
             gpointer      user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *item1 = (StampConversationItem *)(a);
  StampConversationItem *item2 = (StampConversationItem *)(b);
  guint timestamp1 = stamp_conversation_item_get_timestamp (item1);
  guint timestamp2 = stamp_conversation_item_get_timestamp (item2);
  g_autoptr (GSettings) settings = g_settings_new ("org.tabos.stamp.mail");
  gint64 uid1;
  gint64 uid2;

  if (g_settings_get_boolean (settings, "important-first")) {
    if (stamp_conversation_item_is_important (item1) && !stamp_conversation_item_is_important (item2)) {
      return -1;
    }
    if (!stamp_conversation_item_is_important (item1) && stamp_conversation_item_is_important (item2)) {
      return 1;
    }
  }

  if (G_UNLIKELY (timestamp1 != timestamp2)) {
    if (self->sort_mode == SORT_MODE_OLDEST_FIRST)
      return (timestamp1 - timestamp2);

    return -(timestamp1 - timestamp2);
  }

  uid1 = atoi (stamp_conversation_item_get_uid (item1));
  uid2 = atoi (stamp_conversation_item_get_uid (item2));
  return uid1 > uid2 ? -1 : 1;
}

static gboolean
filter_func (gpointer object,
             gpointer user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *item = STAMP_CONVERSATION_ITEM (object);
  const char *search_text = gtk_editable_get_text (GTK_EDITABLE (self->search_entry));
  int search_text_len = search_text ? strlen (search_text) : 0;

  if (stamp_conversation_item_get_hidden (item))
    return FALSE;

  if (search_text_len) {
    g_autofree char *search = g_utf8_strdown (search_text, search_text_len);
    gboolean found = FALSE;

    if (!found) {
      const char *item_from = stamp_conversation_item_get_from (item);

      if (item_from && strlen (item_from) > 0) {
        g_autofree char *from = g_utf8_strdown (item_from, -1);
        if (g_strstr_len (from, -1, search))
          found = TRUE;
      }
    }

    if (!found) {
      const char *item_subject = stamp_conversation_item_get_subject (item);

      if (item_subject && strlen (item_subject) > 0) {
        g_autofree char *subject = g_utf8_strdown (item_subject, -1);
        if (g_strstr_len (subject, -1, search))
          found = TRUE;
      }
    }

    if (!found) {
      const char *item_mail = stamp_conversation_item_get_mail (item);

      if (item_mail && strlen (item_mail) > 0) {
        g_autofree char *mail = g_utf8_strdown (item_mail, -1);

        if (g_strstr_len (mail, -1, search))
          found = TRUE;
      }
    }

    if (!found)
      return FALSE;
  }

  switch (self->filter_mode) {
    case FILTER_MODE_UNREAD:
      return stamp_conversation_item_get_unread (item) != 0;
    case FILTER_MODE_STARRED:
      return stamp_conversation_item_get_flagged (item);
    case FILTER_MODE_ATTACHMENT:
      return stamp_conversation_item_has_attachment (item);
    case FILTER_MODE_ALL:
    default:
      break;
  }

  return TRUE;
}

static void
on_single_selection_changed (GtkSelectionModel *model,
                             guint              position,
                             guint              n_items,
                             gpointer           user_data);

static void
on_category_toggled (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (self->single_selection));
  g_autoptr (GVariant) state = g_action_get_state (G_ACTION (action));
  gboolean active = g_variant_get_boolean (state);
  const char *action_name = g_action_get_name (G_ACTION (action));
  const char *id = action_name + 4;

  g_simple_action_set_state (action, g_variant_new_boolean (!active));
  stamp_conversation_item_set_label (item, id, !active);
}

static void
rebuild_category_actions (StampConversationList *self)
{
  GActionMap *map = G_ACTION_MAP (self->actions);
  char **old_names = g_object_get_data (G_OBJECT (self->actions), "cat-action-names");
  GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
  GList *categories;

  if (!self->account)
    return;

  categories = stamp_account_get_categories (self->account);

  if (old_names) {
    for (guint idx = 0; old_names[idx]; idx++)
      g_action_map_remove_action (map, old_names[idx]);
  }

  for (GList *iter = categories; iter && iter->data; iter = g_list_next (iter)) {
    StampCategory *cat = iter->data;
    g_autoptr (GString) name = g_string_new (stamp_category_get_name (cat));
    char *action_name;
    g_autoptr (GSimpleAction) action;

    g_string_replace (name, " ", "_", 0);

    action_name = g_strdup_printf ("cat-%s", name->str);
    action = g_simple_action_new_stateful (action_name, NULL, g_variant_new_boolean (FALSE));

    g_signal_connect (action, "activate", G_CALLBACK (on_category_toggled), self);
    g_action_map_add_action (map, G_ACTION (action));

    g_ptr_array_add (names, g_strdup (action_name));
  }

  g_ptr_array_add (names, NULL);
  g_object_set_data_full (G_OBJECT (self->actions), "cat-action-names", g_ptr_array_free (names, FALSE), (GDestroyNotify)g_strfreev);
}

static void
on_items_changed (GListModel *model,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  guint n_items = g_list_model_get_n_items (model);

  if (n_items == 0) {
    gtk_stack_set_visible_child_name (GTK_STACK (self->mail_list_stack), "empty");
  } else {
    gtk_stack_set_visible_child_name (GTK_STACK (self->mail_list_stack), "list");
  }

  rebuild_category_actions (self);
}

static void
on_get_folder (GObject      *source,
               GAsyncResult *res,
               gpointer      user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  CamelStore *store = CAMEL_STORE (source);
  g_autoptr (CamelFolder) folder = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) uids = NULL;
  g_autoptr (GList) folders = NULL;
  CamelFolderThread *thread;
  g_autoptr (GPtrArray) array = NULL;
  StampMailService *mail_service;

  if (self->folder) {
    g_signal_handlers_disconnect_by_func (self->folder, on_conversation_list_folder_changed, self);
    g_clear_object (&self->folder);
  }

  folder = camel_store_get_folder_finish (store, res, &error);
  if (error) {
    g_warning ("Could not get folder: %s", error->message);
    gtk_widget_set_visible (self->spinner, FALSE);
    gtk_widget_set_margin_top (self->spinner, 12);
    return;
  }

  self->folder = g_object_ref (folder);
  g_signal_connect_object (folder, "changed", G_CALLBACK (on_conversation_list_folder_changed), self, 0);

  mail_service = stamp_account_get_mail_service (self->account);
  g_hash_table_insert (self->folders, g_strdup (camel_service_get_uid (stamp_mail_service_get_service (mail_service))), g_object_ref (folder));

  thread = get_thread (self, folder);
  self->thread = thread;

  array = g_ptr_array_new_with_free_func (g_object_unref);

  if (thread) {
    CamelFolderThreadNode *child;
    const char *service_uid = camel_service_get_uid (stamp_mail_service_get_service (mail_service));
    /* guint loaded = 0; */

    child = camel_folder_thread_get_tree (thread);

    while (child /*&& loaded < INITIAL_LOAD_COUNT*/) {
      StampConversationItem *item = NULL;

      item = stamp_conversation_item_new (child, service_uid);

      g_ptr_array_add (array, item);
      child = camel_folder_thread_node_get_next (child);
/*      loaded++;
    }

    if (child) {
      guint remaining = 0;
      while (child) {
        remaining++;
        child = camel_folder_thread_node_get_next (child);
      }
      self->pending_load_count = remaining;*/
    }
  }

  g_list_store_splice (self->list_store, 0, g_list_model_get_n_items (G_LIST_MODEL (self->list_store)), (gpointer *)array->pdata, array->len);

  if (self->window_title) {
    adw_window_title_set_title (ADW_WINDOW_TITLE (self->window_title), camel_folder_get_display_name (folder));
    adw_window_title_set_subtitle (ADW_WINDOW_TITLE (self->window_title), stamp_account_get_name (self->account));
  }

  gtk_widget_set_visible (self->spinner, FALSE);
  gtk_widget_set_margin_top (self->spinner, 12);

  if (g_list_model_get_n_items (G_LIST_MODEL (self->list_store)) > 0)
    gtk_list_view_scroll_to (GTK_LIST_VIEW (self->listview), 0, GTK_LIST_SCROLL_FOCUS, NULL);

  if (self->pending_load_count > 0) {
    g_idle_add (load_more_items_idle, self);
  }
}

static gboolean
load_more_items_idle (gpointer user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  CamelFolderThreadNode *child;
  StampMailService *mail_service;
  g_autoptr (GPtrArray) new_items = NULL;
  guint current_count = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));
  guint skip;
  guint loaded = 0;
  const char *service_uid;

  if (!self->thread || self->pending_load_count == 0) {
    return G_SOURCE_REMOVE;
  }

  child = camel_folder_thread_get_tree (self->thread);
  skip = current_count;

  while (child && loaded < skip) {
    child = camel_folder_thread_node_get_next (child);
    loaded++;
  }

  loaded = 0;
  new_items = g_ptr_array_new_with_free_func (g_object_unref);
  mail_service = stamp_account_get_mail_service (self->account);
  service_uid = camel_service_get_uid (stamp_mail_service_get_service (mail_service));

  while (child && loaded < BATCH_LOAD_COUNT) {
    StampConversationItem *item = stamp_conversation_item_new (child, service_uid);

    g_ptr_array_add (new_items, item);
    child = camel_folder_thread_node_get_next (child);
    loaded++;
    self->pending_load_count--;
  }

  if (new_items->len > 0) {
    guint pos = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));
    g_list_store_splice (self->list_store, pos, 0, new_items->pdata, new_items->len);
  }

  if (self->pending_load_count > 0) {
    return G_SOURCE_CONTINUE;
  }

  return G_SOURCE_REMOVE;
}

void
stamp_conversation_list_load_folder (StampConversationList *self,
                                     StampAccount          *account,
                                     char                  *full_name)
{
  StampMailService *mail_service;

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  g_hash_table_insert (self->thread_cache, g_strdup (full_name), NULL);

  if (!account)
    return;

  self->cancellable = g_cancellable_new ();

  mail_service = stamp_account_get_mail_service (account);
  self->account = account;

  rebuild_category_actions (self);

  g_set_str (&self->full_name, full_name);

  camel_store_get_folder (CAMEL_STORE (stamp_mail_service_get_service (mail_service)), full_name, CAMEL_STORE_FOLDER_NONE, G_PRIORITY_DEFAULT, self->cancellable, on_get_folder, self);
}

static void
on_mail_search_entry_changed (GtkWidget *search_entry,
                              gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  if (self->filter)
    gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
on_new_message (GObject  *button,
                gpointer  user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkWidget *composer = stamp_composer_new (self->account);
  gtk_window_present (GTK_WINDOW (composer));
}

static void
on_row_mark_read (GtkWidget *widget,
                  gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationRow *row = STAMP_CONVERSATION_ROW (widget);
  StampConversationItem *item = stamp_conversation_row_get_item (row);
  CamelFolderThreadNode *node;

  node = stamp_conversation_item_get_node (item);

  stamp_conversation_list_mark_read (self, node);
}

static void
on_row_mark_unread (GtkWidget *widget,
                    gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationRow *row = STAMP_CONVERSATION_ROW (widget);
  StampConversationItem *item = stamp_conversation_row_get_item (row);
  CamelFolderThreadNode *node;

  node = stamp_conversation_item_get_node (item);

  stamp_conversation_list_mark_unread (self, node);
}

static void
on_row_trash (GtkWidget *widget,
              gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationRow *row = STAMP_CONVERSATION_ROW (widget);
  StampConversationItem *item = stamp_conversation_row_get_item (row);

  g_signal_emit (self, signals[CONVERSATION_TRASH], 0, item);
}

typedef struct {
  StampConversationList *self;
  GtkListItem *list_item;
  double start_x;
  double start_y;
} RowData;

static void
handle_popover (RowData *row_data,
                gdouble  x,
                gdouble  y)
{
  GtkWidget *popover;
  g_autoptr (GMenu) menu = NULL;
  g_autoptr (GMenu) sub_menu = NULL;
  GdkRectangle rect;
  StampConversationRow *child = STAMP_CONVERSATION_ROW (gtk_list_item_get_child (row_data->list_item));
  graphene_point_t src_point = { (float)x, (float)y };
  graphene_point_t dst_point;
  guint position = gtk_list_item_get_position (row_data->list_item);
  GtkSelectionModel *model = gtk_list_view_get_model (GTK_LIST_VIEW (row_data->self->listview));
  GList *categories;
  GPtrArray *labels;

  gtk_selection_model_select_item (model, position, TRUE);

  menu = g_menu_new ();
  g_menu_append (menu, _("Reply"), "mail.reply-current");
  g_menu_append (menu, _("Reply All"), "mail.reply-all-current");
  g_menu_append (menu, _("Forward"), "mail.forward-current");

  g_menu_append (menu, _("Mark Read"), "mail.mark-read-current");
  g_menu_append (menu, _("Mark Unread"), "mail.mark-unread-current");

  labels = stamp_conversation_item_get_labels (STAMP_CONVERSATION_ITEM (gtk_list_item_get_item (row_data->list_item)));
  categories = stamp_account_get_categories (row_data->self->account);
  if (categories) {
    sub_menu = g_menu_new ();

    for (GList *iter = categories; iter && iter->data; iter = g_list_next (iter)) {
      StampCategory *cat = iter->data;
      g_autoptr (GString) name = g_string_new (stamp_category_get_name (cat));
      GMenuItem *item;
      g_autofree char *action_name = NULL;
      g_autofree char *full_action_name = NULL;
      GAction *action;

      g_string_replace (name, " ", "_", 0);
      action_name = g_strdup_printf ("cat-%s", name->str);
      full_action_name = g_strdup_printf ("conversation-list.%s", action_name);
      item = g_menu_item_new (stamp_category_get_name (cat), full_action_name);
      g_menu_append_item (sub_menu, item);

      action = g_action_map_lookup_action (G_ACTION_MAP (row_data->self->actions), action_name);
      g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (FALSE));
      for (int idx = 0; idx < labels->len; idx++) {
        char *cat_name = labels->pdata[idx];

        if (g_strcmp0 (cat_name, action_name + 4) == 0) {
          g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (TRUE));
          break;
        }
      }
    }

    g_menu_append_submenu (menu, _("Category"), G_MENU_MODEL (sub_menu));
  }

  popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
  gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);
  gtk_widget_set_parent (popover, row_data->self->listview);

  if (!gtk_widget_compute_point (GTK_WIDGET (child), row_data->self->listview, &src_point, &dst_point)) {
    g_warning ("%s: compute point failed", G_STRFUNC);
    return;
  }

  rect.x = dst_point.x;
  rect.y = dst_point.y;
  rect.width = 1;
  rect.height = 1;
  gtk_popover_set_pointing_to (GTK_POPOVER (popover), &rect);

  gtk_popover_popup (GTK_POPOVER (popover));
}

static void
on_released (GtkEventController *controller,
             gint                n_press,
             gdouble             x,
             gdouble             y,
             gpointer            user_data)
{
  RowData *row_data = g_object_get_data (G_OBJECT (controller), "row-data");

  handle_popover (row_data, x, y);
}

static void
set_selection_active (StampConversationList *self,
                      gboolean               selection_active)
{
  self->selection_mode = selection_active;

  /* gtk_widget_set_visible (self->listview, FALSE); */
  if (self->selection_mode) {
    gtk_stack_set_visible_child (GTK_STACK (self->header_stack), self->selection_headerbar);
    gtk_list_view_set_model (GTK_LIST_VIEW (self->listview), GTK_SELECTION_MODEL (self->multi_selection));
  } else {
    gtk_stack_set_visible_child (GTK_STACK (self->header_stack), self->normal_headerbar);
    gtk_list_view_set_model (GTK_LIST_VIEW (self->listview), GTK_SELECTION_MODEL (self->single_selection));
    stamp_conversation_list_unselect (self);
  }
  /* gtk_widget_set_visible (self->listview, TRUE); */

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

static void
on_selection_changed (GtkCheckButton *check,
                      GParamSpec     *pspec,
                      GtkListItem    *list_item);

static void
refresh_checkboxes (StampConversationList *self)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, self->position_to_check);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    guint pos = GPOINTER_TO_UINT (key);
    GtkWidget *check = GTK_WIDGET (value);

    gtk_check_button_set_active (GTK_CHECK_BUTTON (check),
                                 gtk_bitset_contains (self->selected, pos));
  }
}

static void
update_selection_title (StampConversationList *self)
{
  guint n_selected = gtk_bitset_get_size (self->selected);
  char buf[64];

  g_snprintf (buf, sizeof buf, _("%u selected"), n_selected);

  adw_window_title_set_title (ADW_WINDOW_TITLE (self->selection_label), buf);
}

static void
on_long_press_pressed (GtkGestureLongPress *controller,
                       gdouble              x,
                       gdouble              y,
                       gpointer             user_data)
{
  RowData *row_data = g_object_get_data (G_OBJECT (controller), "row-data");
  StampConversationList *self = STAMP_CONVERSATION_LIST (row_data->self);
  guint position = gtk_list_item_get_position (row_data->list_item);

  set_selection_active (self, TRUE);

  gtk_bitset_add (self->selected, position);
  update_selection_title (self);
  refresh_checkboxes (self);
}

static void
on_row_pressed (GtkGestureClick *gesture,
                int              n_press,
                double           x,
                double           y,
                gpointer         user_data)
{
  RowData *data = g_object_get_data (G_OBJECT (gesture), "row-data");
  StampConversationList *self = data->self;
  GtkListItem *list_item = data->list_item;
  guint position = gtk_list_item_get_position (list_item);
  GdkEvent *event = gtk_gesture_get_last_event (GTK_GESTURE (gesture), NULL);
  GdkModifierType state;
  gboolean ctrl;
  gboolean shift;

  if (!event || gdk_event_get_event_type (event) != GDK_BUTTON_PRESS)
    return;

  if (!self->selection_mode) {
    if (n_press == 2) {
      GtkWidget *window = adw_window_new ();
      GtkWidget *message_list = stamp_message_list_new ();
      StampConversationItem *item = STAMP_CONVERSATION_ITEM (gtk_list_item_get_item (list_item));

      stamp_message_list_set_conversation (STAMP_MESSAGE_LIST (message_list), self->account, stamp_conversation_item_get_node (item));
      adw_window_set_content (ADW_WINDOW (window), message_list);
      gtk_window_set_default_size (GTK_WINDOW (window), 800, 600);
      gtk_window_present (GTK_WINDOW (window));
    }
    return;
  }

  state = gdk_event_get_modifier_state (event);
  ctrl = (state & GDK_CONTROL_MASK) != 0;
  shift = (state & GDK_SHIFT_MASK) != 0;

  if (!self->selection_mode)
    return;

  if (ctrl && !shift) {
    /* Ctrl: einzelne Row toggeln, Anchor setzen */
    if (gtk_bitset_contains (self->selected, position))
      gtk_bitset_remove (self->selected, position);
    else
      gtk_bitset_add (self->selected, position);
    self->anchor_position = position;
  } else if (shift && !ctrl) {
    guint from;
    guint to;
    /* Shift: Bereich vom Anchor bis hier auswählen */
    gtk_bitset_remove_all (self->selected);
    from = MIN (self->anchor_position, position);
    to = MAX (self->anchor_position, position);
    gtk_bitset_add_range (self->selected, from, to - from + 1);
  } else if (ctrl && shift) {
    /* Ctrl+Shift: Bereich hinzufügen ohne bestehende zu verlieren */
    guint from = MIN (self->anchor_position, position);
    guint to = MAX (self->anchor_position, position);
    gtk_bitset_add_range (self->selected, from, to - from + 1);
  } else {
    /* Kein Modifier: nur diese Row, Anchor setzen */
    if (gtk_bitset_contains (self->selected, position)) {
      /* Nochmal klicken → abwählen */
      gtk_bitset_remove (self->selected, position);
    } else {
      /* gtk_bitset_remove_all(self->selected); */
      gtk_bitset_add (self->selected, position);
    }
    self->anchor_position = position;
  }

  /* if (gtk_bitset_contains(self->selected, position)) { */
  /*     gtk_bitset_remove(self->selected, position); */
  /*     gtk_check_button_set_active(GTK_CHECK_BUTTON(check), FALSE); */
  /* } else { */
  /*     gtk_bitset_add(self->selected, position); */
  /*     gtk_check_button_set_active(GTK_CHECK_BUTTON(check), TRUE); */
  /* } */

  /* Callback benachrichtigen falls gewünscht */
  /* on_selection_changed(GTK_CHECK_BUTTON (check), NULL, list_item); */
  update_selection_title (self);

  refresh_checkboxes (self);
}

static void
on_touch_begin (GtkGestureClick *gesture,
                int              n_press,
                double           x,
                double           y,
                gpointer         user_data)
{
  RowData *data = g_object_get_data (G_OBJECT (gesture), "row-data");
  data->start_x = x;
  data->start_y = y;
}

static void
on_touch_released (GtkGestureClick *gesture,
                   int              n_press,
                   double           x,
                   double           y,
                   gpointer         user_data)
{
  RowData *data = g_object_get_data (G_OBJECT (gesture), "row-data");
  StampConversationList *self = data->self;
  GtkListItem *list_item = data->list_item;

  /* Bewegung prüfen – wenn zu viel bewegt wurde war es ein Scroll */
  double dx = x - data->start_x;
  double dy = y - data->start_y;
  guint position;
  if (dx * dx + dy * dy > 10 * 10)
    return;

  position = gtk_list_item_get_position (list_item);

  if (gtk_bitset_contains (self->selected, position))
    gtk_bitset_remove (self->selected, position);
  else
    gtk_bitset_add (self->selected, position);

  self->anchor_position = position;
  refresh_checkboxes (self);
}

static void
on_setup_list_item (GtkListItemFactory *factory,
                    GtkListItem        *list_item,
                    gpointer            user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkWidget *row = stamp_conversation_row_new ();
  GtkGesture *gesture = gtk_gesture_click_new ();
  GtkGesture *long_press = gtk_gesture_long_press_new ();
  GtkGesture *press_gesture;
  RowData *row_data;

  GtkGesture *touch = gtk_gesture_click_new ();
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (touch), TRUE);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (touch));
  row_data = g_new (RowData, 1);
  row_data->self = self;
  row_data->list_item = list_item;
  g_object_set_data_full (G_OBJECT (touch), "row-data", row_data, g_free);
  g_signal_connect (touch, "pressed", G_CALLBACK (on_touch_begin), NULL);
  g_signal_connect (touch, "released", G_CALLBACK (on_touch_released), NULL);

  row_data = g_new (RowData, 1);
  row_data->self = self;
  row_data->list_item = list_item;
  press_gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (press_gesture), GDK_BUTTON_PRIMARY);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (press_gesture),
                                              GTK_PHASE_BUBBLE);
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (press_gesture), FALSE);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (press_gesture));
  g_object_set_data_full (G_OBJECT (press_gesture), "row-data", row_data, g_free);
  g_signal_connect (press_gesture, "pressed", G_CALLBACK (on_row_pressed), NULL);

  /* Secondary button click */
  row_data = g_new (RowData, 1);
  row_data->self = self;
  row_data->list_item = list_item;
  g_object_set_data_full (G_OBJECT (gesture), "row-data", row_data, g_free);
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect (gesture, "released", G_CALLBACK (on_released), NULL);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (gesture));

  /* Long press gesture */
  row_data = g_new (RowData, 1);
  row_data->self = self;
  row_data->list_item = list_item;
  g_object_set_data_full (G_OBJECT (long_press), "row-data", row_data, g_free);
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (long_press), TRUE);
  g_signal_connect (long_press, "pressed", G_CALLBACK (on_long_press_pressed), self);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (long_press));

  g_signal_connect_object (row, "mark-read", G_CALLBACK (on_row_mark_read), self, 0);
  g_signal_connect_object (row, "mark-unread", G_CALLBACK (on_row_mark_unread), self, 0);
  g_signal_connect_object (row, "trash", G_CALLBACK (on_row_trash), self, 0);

  g_object_bind_property (list_item, "selected", row, "selected", G_BINDING_SYNC_CREATE);
  g_object_set_data (G_OBJECT (list_item), "model", self->multi_selection);
  gtk_list_item_set_child (list_item, row);
}

static void
on_check_toggled (GtkCheckButton *check,
                  gpointer        user_data)
{
  GtkListItem *list_item = GTK_LIST_ITEM (user_data);
  GtkWidget *list_view = gtk_widget_get_ancestor (GTK_WIDGET (check), GTK_TYPE_LIST_VIEW);
  GtkSelectionModel *model;
  guint position;

  if (!list_view)
    return;

  model = gtk_list_view_get_model (GTK_LIST_VIEW (list_view));
  position = gtk_list_item_get_position (list_item);

  if (gtk_check_button_get_active (check))
    gtk_selection_model_select_item (model, position, FALSE);
  else
    gtk_selection_model_unselect_item (model, position);
}

static void
on_selection_changed (GtkCheckButton *check,
                      GParamSpec     *pspec,
                      GtkListItem    *list_item)
{
  g_signal_handlers_block_by_func (check, on_check_toggled, list_item);
  gtk_check_button_set_active (check, gtk_list_item_get_selected (list_item));
  g_signal_handlers_unblock_by_func (check, on_check_toggled, list_item);
}

static void
on_bind_list_item (GtkListItemFactory *factory,
                   GtkListItem        *list_item,
                   gpointer            user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkWidget *row;
  StampConversationItem *model;
  GtkCheckButton *check_button;
  guint pos = gtk_list_item_get_position (list_item);

  row = gtk_list_item_get_child (list_item);
  model = STAMP_CONVERSATION_ITEM (gtk_list_item_get_item (list_item));

  stamp_conversation_row_bind_mail (STAMP_CONVERSATION_ROW (row), model, self->account);
  stamp_conversation_row_set_selection_visible (STAMP_CONVERSATION_ROW (row), self->selection_mode);

  check_button = stamp_conversation_row_get_check_button (STAMP_CONVERSATION_ROW (row));
  gtk_check_button_set_active (check_button, gtk_list_item_get_selected (list_item));
  g_signal_connect (check_button, "toggled", G_CALLBACK (on_check_toggled), list_item);

  g_hash_table_insert (self->position_to_check, GUINT_TO_POINTER (pos), check_button);

  g_signal_connect_swapped (list_item, "notify::selected", G_CALLBACK (on_selection_changed), check_button);
}

static void
on_unbind_list_item (GtkListItemFactory *factory,
                     GtkListItem        *list_item,
                     gpointer            user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkWidget *row;
  GtkCheckButton *check_button;
  StampConversationItem *model;
  guint pos = gtk_list_item_get_position (list_item);
  g_hash_table_remove (self->position_to_check, GUINT_TO_POINTER (pos));

  row = gtk_list_item_get_child (list_item);
  model = STAMP_CONVERSATION_ITEM (gtk_list_item_get_item (list_item));

  stamp_conversation_row_unbind_mail (STAMP_CONVERSATION_ROW (row), model);
  check_button = stamp_conversation_row_get_check_button (STAMP_CONVERSATION_ROW (row));
  g_signal_handlers_disconnect_by_func (list_item, on_selection_changed, check_button);
}

typedef struct {
  StampConversationList *self;
  StampConversationItem *item;
} MarkReadData;

static void
mark_read_data_free (MarkReadData *data)
{
  g_clear_object (&data->self);
  g_clear_object (&data->item);
  g_clear_pointer (&data, g_free);
}

static void
set_thread_flag (CamelFolderThreadNode *node,
                 CamelMessageFlags      flag)
{
  if (!node)
    return;

  if (!(flag & camel_message_info_get_flags (camel_folder_thread_node_get_item (node)))) {
    camel_message_info_set_flags (CAMEL_MESSAGE_INFO (camel_folder_thread_node_get_item (node)), flag, ~0);
  }

  for (CamelFolderThreadNode *child = camel_folder_thread_node_get_child (node); child != NULL; child = camel_folder_thread_node_get_next (child)) {
    set_thread_flag (child, flag);
  }
}

static gboolean
mark_read (gpointer user_data)
{
  MarkReadData *data = user_data;

  set_thread_flag (stamp_conversation_item_get_node (data->item), CAMEL_MESSAGE_SEEN);
  data->self->mark_read_timeout_id = 0;
  stamp_conversation_item_notify_unread (data->item);

  return G_SOURCE_REMOVE;
}

static void
on_single_selection_changed (GtkSelectionModel *model,
                             guint              position,
                             guint              n_items,
                             gpointer           user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *conversation_item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (model)));

  if (self->selection_mode)
    return;

  g_clear_handle_id (&self->mark_read_timeout_id, g_source_remove);

  if (conversation_item) {
    if (stamp_conversation_item_get_unread (conversation_item)) {
      g_autoptr (GSettings) settings = g_settings_new ("org.tabos.stamp.mail");
      MarkReadData *data = g_new (MarkReadData, 1);
      gdouble mark_timeout = g_settings_get_double (settings, "mark-read-timeout");
      int timeout = mark_timeout * 1000;

      data->self = g_object_ref (self);
      data->item = g_object_ref (conversation_item);

      self->mark_read_timeout_id = g_timeout_add_full (G_PRIORITY_HIGH_IDLE, timeout, mark_read, data, (GDestroyNotify)mark_read_data_free);
    }

    g_signal_emit (self, signals[CONVERSATION_SELECTED], 0, stamp_conversation_item_get_node (conversation_item));
  } else {
    g_signal_emit (self, signals[CONVERSATION_SELECTED], 0, 0);
  }
}

static void
on_selection_button_clicked (GtkWidget *button,
                             gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  set_selection_active (self, !self->selection_mode);
}

static void
scroll_to_top_idle (gpointer user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  gtk_list_view_scroll_to (GTK_LIST_VIEW (self->listview), 0, GTK_LIST_SCROLL_NONE, NULL);
  gtk_widget_set_visible (self->scroll_to_top, FALSE);
}

static void
on_scroll_to_top (GtkButton *button,
                  gpointer   user_data)
{
  g_idle_add_once (scroll_to_top_idle, user_data);
}

static void
stamp_conversation_list_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (object);

  switch (property_id) {
    case PROP_STATE:
      g_value_set_boolean (value, self->selection_mode);
      break;
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_conversation_list_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  switch (property_id) {
    case PROP_STATE:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_conversation_list_dispose (GObject *object)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_cancellable_cancel (self->transfer_cancellable);
  g_clear_object (&self->transfer_cancellable);

  g_clear_object (&self->thread);
  g_clear_object (&self->list_store);
  g_clear_pointer (&self->full_name, g_free);
  g_clear_pointer (&self->thread_cache, g_hash_table_unref);
  g_clear_pointer (&self->folders, g_hash_table_unref);
  g_clear_list (&self->moved_messages, g_object_unref);
  g_clear_object (&self->account);
  g_clear_object (&self->folder);

  g_clear_pointer (&self->selected, gtk_bitset_unref);
  g_clear_pointer (&self->position_to_check, g_hash_table_unref);

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_CONVERSATION_LIST);

  G_OBJECT_CLASS (stamp_conversation_list_parent_class)->dispose (object);
}

static gboolean
on_scroll (GtkEventControllerScroll *controller,
           double                    dx,
           double                    dy,
           gpointer                  user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));
  double adjustment = gtk_adjustment_get_value (adj);

  if (adjustment > 200 && dy < 0) {
    gtk_widget_set_visible (self->scroll_to_top, TRUE);
  } else {
    gtk_widget_set_visible (self->scroll_to_top, FALSE);
  }

  return FALSE;
}

static void
stamp_conversation_list_class_init (StampConversationListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/conversation-list/stamp-conversation-list.ui");

  object_class->get_property = stamp_conversation_list_get_property;
  object_class->set_property = stamp_conversation_list_set_property;
  object_class->dispose = stamp_conversation_list_dispose;

  gtk_widget_class_bind_template_child (widget_class, StampConversationList, listview);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, search_bar);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, search_entry);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, window_title);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, scroll_to_top);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, header_stack);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, normal_headerbar);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, selection_headerbar);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, selection_label);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, spinner);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, sort_button);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, mail_list_stack);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, sidebar_button);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, sort_is_active);

  gtk_widget_class_bind_template_callback (widget_class, on_mail_search_entry_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_new_message);
  gtk_widget_class_bind_template_callback (widget_class, on_setup_list_item);
  gtk_widget_class_bind_template_callback (widget_class, on_bind_list_item);
  gtk_widget_class_bind_template_callback (widget_class, on_unbind_list_item);
  gtk_widget_class_bind_template_callback (widget_class, on_selection_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_scroll_to_top);
  gtk_widget_class_bind_template_callback (widget_class, on_scroll);

  signals[CONVERSATION_SELECTED] = g_signal_new ("conversation-selected", G_OBJECT_CLASS_TYPE (klass),
                                                 G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                                 0, NULL, NULL, NULL,
                                                 G_TYPE_NONE,
                                                 1, G_TYPE_POINTER);
  signals[CONVERSATION_TRASH] = g_signal_new ("conversation-trash", G_OBJECT_CLASS_TYPE (klass),
                                              G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL, NULL,
                                              G_TYPE_NONE,
                                              1, STAMP_TYPE_CONVERSATION_ITEM);

  properties[PROP_STATE] = g_param_spec_boolean ("state",
                                                 NULL,
                                                 NULL,
                                                 FALSE,
                                                 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
on_multi_selection_changed (GtkSelectionModel *model,
                            guint              pos,
                            guint              n_items,
                            gpointer           user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  guint n_selected = gtk_bitset_get_size (self->selected);
  char buf[64];

  g_snprintf (buf, sizeof buf, _("%u selected"), n_selected);

  adw_window_title_set_title (ADW_WINDOW_TITLE (self->selection_label), buf);
}

static void
on_drag_update (GtkGesturePan   *gesture,
                GtkPanDirection  direction,
                gdouble          offset,
                gpointer         user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  double clean_y = offset;
  GValue value = G_VALUE_INIT;
  g_value_init (&value, G_TYPE_INT);

  if (!self->claimed) {
    if (direction != GTK_PAN_DIRECTION_DOWN) {
      gtk_widget_set_visible (self->scroll_to_top, FALSE);
      return;
    }

    if (gtk_adjustment_get_value (self->vadj) > 0.0) {
      gtk_widget_set_visible (self->scroll_to_top, TRUE);
      return;
    }
    gtk_widget_set_visible (self->scroll_to_top, FALSE);

    if (offset < 10)
      return;

    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    gtk_widget_set_sensitive (GTK_WIDGET (self->scrolled_window), FALSE);
    self->claimed = TRUE;
  }

  gtk_widget_set_visible (self->spinner, TRUE);


  if (offset > 150)
    clean_y = 150;

  if (clean_y < 0)
    clean_y = 0;

  if (clean_y > 32) {
    gtk_widget_set_margin_top (GTK_WIDGET (self->spinner), clean_y);
    g_value_set_int (&value, 32);
    gtk_widget_set_size_request (GTK_WIDGET (self->spinner), 32, 32);
  } else if (clean_y > 0) {
    g_value_set_int (&value, clean_y);
    gtk_widget_set_size_request (GTK_WIDGET (self->spinner), clean_y, clean_y);
  } else {
    gtk_widget_set_margin_top (GTK_WIDGET (self->spinner), 32);
    g_value_set_int (&value, 0);
    gtk_widget_set_size_request (GTK_WIDGET (self->spinner), 0, 0);
  }

  self->is_pulling = TRUE;
}

static void
load_folder_idle (gpointer user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  stamp_conversation_list_load_folder (self, self->account, self->full_name);
}

static void
on_drag_end (GtkGestureDrag *gesture,
             double          dx,
             double          dy,
             gpointer        user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  if (gtk_widget_get_margin_top (GTK_WIDGET (self->spinner)) >= 125) {
    g_idle_add_once (load_folder_idle, self);
  } else {
    gtk_widget_set_visible (self->spinner, FALSE);
    gtk_widget_set_margin_top (self->spinner, 12);
  }

  gtk_widget_set_sensitive (GTK_WIDGET (self->scrolled_window), TRUE);
  self->claimed = FALSE;
}

static void
on_filter_activate (GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  const char *value;

  if (!parameter)
    return;

  /* neuen Zustand setzen */
  g_simple_action_set_state (action, parameter);

  value = g_variant_get_string (parameter, NULL);

  if (g_strcmp0 (value, "all-mails") == 0)
    self->filter_mode = FILTER_MODE_ALL;
  else if (g_strcmp0 (value, "starred") == 0)
    self->filter_mode = FILTER_MODE_STARRED;
  else if (g_strcmp0 (value, "attachment") == 0)
    self->filter_mode = FILTER_MODE_ATTACHMENT;
  else if (g_strcmp0 (value, "unread") == 0)
    self->filter_mode = FILTER_MODE_UNREAD;

  gtk_widget_set_visible (GTK_WIDGET (self->sort_is_active), g_strcmp0 (value, "all-mails") != 0);

  if (self->filter)
    gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void
on_sort_activate (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  const char *value;

  if (!parameter)
    return;

  /* neuen Zustand setzen */
  g_simple_action_set_state (action, parameter);

  value = g_variant_get_string (parameter, NULL);

  if (g_strcmp0 (value, "newest-first") == 0)
    self->sort_mode = SORT_MODE_NEWEST_FIRST;
  else
    self->sort_mode = SORT_MODE_OLDEST_FIRST;

  gtk_sorter_changed (self->sorter, GTK_SORTER_CHANGE_INVERTED);
}

static void
thread_unref (gpointer user_data)
{
  if (user_data)
    g_object_unref (user_data);
}

static void
on_mark_category (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *item = NULL;
  const char *value;

  if (!parameter)
    return;

  value = g_variant_get_string (parameter, NULL);

  item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));

  stamp_conversation_item_set_label (item, value, TRUE);
}

static const GActionEntry stamp_conversation_list_action_entries[] = {
  { .name = "mark-category", .activate = on_mark_category, .parameter_type = "s" },
};

static void
stamp_conversation_list_init (StampConversationList *self)
{
  GtkSortListModel *sort_model;
  GtkGesture *drag;
  GSimpleActionGroup *actions = g_simple_action_group_new ();
  GtkApplication *app = GTK_APPLICATION (g_application_get_default ());
  GMenu *menu;
  GMenu *filter_menu;
  GMenu *sort_menu;
  GSimpleAction *all_mails_action = g_simple_action_new_stateful ("mail-filter", G_VARIANT_TYPE_STRING, g_variant_new_string ("all-mails"));
  GSimpleAction *sort_action = g_simple_action_new_stateful ("mail-sort", G_VARIANT_TYPE_STRING, g_variant_new_string ("newest-first"));

  gtk_widget_init_template (GTK_WIDGET (self));

  self->actions = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->actions),
                                   stamp_conversation_list_action_entries,
                                   G_N_ELEMENTS (stamp_conversation_list_action_entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "conversation-list", G_ACTION_GROUP (self->actions));

  self->thread_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, thread_unref);

  g_signal_connect_object (all_mails_action, "activate", G_CALLBACK (on_filter_activate), self, 0);
  g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (all_mails_action));
  g_signal_connect_object (sort_action, "activate", G_CALLBACK (on_sort_activate), self, 0);
  g_action_map_add_action (G_ACTION_MAP (actions), G_ACTION (sort_action));
  gtk_widget_insert_action_group (GTK_WIDGET (self), "conversation", G_ACTION_GROUP (actions));

  menu = g_menu_new ();
  filter_menu = g_menu_new ();
  g_menu_append (filter_menu, _("All Mails"), "app.mail-filter::all-mails");
  g_menu_append (filter_menu, _("Starred"), "app.mail-filter::starred");
  g_menu_append (filter_menu, _("Attachment"), "app.mail-filter::attachment");
  g_menu_append (filter_menu, _("Unread"), "app.mail-filter::unread");
  g_menu_append_section (menu, _("Filter"), G_MENU_MODEL (filter_menu));

  sort_menu = g_menu_new ();
  g_menu_append_section (menu, "Sort Order", G_MENU_MODEL (sort_menu));
  g_menu_append (sort_menu, _("Newest First"), "conversation.mail-sort::newest-first");
  g_menu_append (sort_menu, _("Oldest First"), "conversation.mail-sort::oldest-first");
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->sort_button), G_MENU_MODEL (menu));

  self->is_pulling = FALSE;

  self->folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  self->list_store = g_list_store_new (STAMP_TYPE_CONVERSATION_ITEM);

  self->filter = GTK_FILTER (gtk_custom_filter_new (filter_func, self, NULL));
  self->filter_model = gtk_filter_list_model_new (G_LIST_MODEL (self->list_store), self->filter);
  /* self->filter_model = gtk_filter_list_model_new (NULL, self->filter); */

  self->sorter = GTK_SORTER (gtk_custom_sorter_new (sorter_func, self, NULL));
  sort_model = gtk_sort_list_model_new (G_LIST_MODEL (self->filter_model), self->sorter);

  g_signal_connect (G_LIST_MODEL (sort_model), "items-changed", G_CALLBACK (on_items_changed), self);

  rebuild_category_actions (self);

  self->single_selection = gtk_single_selection_new (G_LIST_MODEL (sort_model));
  g_signal_connect_object (self->single_selection, "selection-changed", G_CALLBACK (on_single_selection_changed), self, 0);
  gtk_list_view_set_model (GTK_LIST_VIEW (self->listview), GTK_SELECTION_MODEL (self->single_selection));

  gtk_single_selection_set_autoselect (self->single_selection, FALSE);
  gtk_single_selection_set_can_unselect (self->single_selection, TRUE);

  self->multi_selection = gtk_no_selection_new (G_LIST_MODEL (sort_model));
  g_signal_connect_object (self->multi_selection, "selection-changed", G_CALLBACK (on_multi_selection_changed), self, 0);

  gtk_search_bar_connect_entry (GTK_SEARCH_BAR (self->search_bar), GTK_EDITABLE (self->search_entry));

  drag = gtk_gesture_pan_new (GTK_ORIENTATION_VERTICAL);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (drag), GTK_PHASE_CAPTURE);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drag));
  self->vadj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));

  g_signal_connect_object (drag, "pan", G_CALLBACK (on_drag_update), self, 0);
  g_signal_connect_object (drag, "drag-end", G_CALLBACK (on_drag_end), self, 0);

  self->selected = gtk_bitset_new_empty ();
  self->anchor_position = 0;
  self->position_to_check = g_hash_table_new (g_direct_hash, g_direct_equal);
}

GtkWidget *
stamp_conversation_list_new (void)
{
  return g_object_new (STAMP_TYPE_CONVERSATION_LIST, NULL);
}

static GPtrArray *
collect_messages (CamelFolderThreadNode *node,
                  GPtrArray             *array)
{
  CamelFolderThreadNode *child = camel_folder_thread_node_get_child (node);

  if (!array) {
    array = g_ptr_array_new ();
  }

  g_ptr_array_add (array, node);

  while (child) {
    array = collect_messages (child, array);
    child = camel_folder_thread_node_get_next (child);
  }

  return array;
}


void
stamp_conversation_list_mark_read (StampConversationList *self,
                                   CamelFolderThreadNode *node)
{
  g_autoptr (GPtrArray) node_array = g_ptr_array_new ();
  g_autoptr (GPtrArray) array = NULL;

  if (self->selection_mode) {
    GtkBitset *selected_items = self->selected;
    GtkBitsetIter iter;
    guint32 current_item_position;

    gtk_bitset_iter_init_first (&iter, selected_items, &current_item_position);

    while (gtk_bitset_iter_is_valid (&iter)) {
      g_autoptr (StampConversationItem) item = STAMP_CONVERSATION_ITEM (g_list_model_get_item (G_LIST_MODEL (self->multi_selection), current_item_position));

      node = stamp_conversation_item_get_node (item);
      g_ptr_array_add (node_array, node);
      gtk_bitset_iter_next (&iter, &current_item_position);
    }

    gtk_bitset_remove_all (self->selected);
    update_selection_title (self);
    refresh_checkboxes (self);
  } else {
    if (!node) {
      StampConversationItem *item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));
      node = stamp_conversation_item_get_node (item);
    }
    g_ptr_array_add (node_array, node);
  }

  for (int idx = 0; idx < node_array->len; idx++) {
    CamelFolderThreadNode *child_node = node_array->pdata[idx];

    array = collect_messages (child_node, array);
  }

  for (int idx = array->len - 1; idx >= 0; idx--) {
    CamelFolderThreadNode *child_node = array->pdata[idx];
    camel_message_info_set_flags (CAMEL_MESSAGE_INFO (camel_folder_thread_node_get_item (child_node)), CAMEL_MESSAGE_SEEN, ~0);
  }
}

void
stamp_conversation_list_mark_unread (StampConversationList *self,
                                     CamelFolderThreadNode *node)
{
  g_autoptr (GPtrArray) node_array = g_ptr_array_new ();
  g_autoptr (GPtrArray) array = NULL;

  if (self->selection_mode) {
    GtkBitset *selected_items = self->selected;
    GtkBitsetIter iter;
    guint32 current_item_position;

    gtk_bitset_iter_init_first (&iter, selected_items, &current_item_position);

    while (gtk_bitset_iter_is_valid (&iter)) {
      g_autoptr (StampConversationItem) item = STAMP_CONVERSATION_ITEM (g_list_model_get_item (G_LIST_MODEL (self->multi_selection), current_item_position));

      node = stamp_conversation_item_get_node (item);
      g_ptr_array_add (node_array, node);
      gtk_bitset_iter_next (&iter, &current_item_position);
    }

    gtk_bitset_remove_all (self->selected);
    update_selection_title (self);
    refresh_checkboxes (self);
  } else {
    if (!node) {
      StampConversationItem *item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));
      node = stamp_conversation_item_get_node (item);
    }
    g_ptr_array_add (node_array, node);
  }

  for (int idx = 0; idx < node_array->len; idx++) {
    CamelFolderThreadNode *child_node = node_array->pdata[idx];

    array = collect_messages (child_node, array);
  }

  for (int idx = array->len - 1; idx >= 0; idx--) {
    CamelFolderThreadNode *child_node = array->pdata[idx];
    camel_message_info_set_flags (CAMEL_MESSAGE_INFO (camel_folder_thread_node_get_item (child_node)), CAMEL_MESSAGE_SEEN, 0);
  }
}

void
stamp_conversation_list_mark_unflag_selected_messages (StampConversationList *self)
{
  StampConversationItem *item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));
  CamelFolderThreadNode *node;

  node = stamp_conversation_item_get_node (item);
  camel_message_info_set_flags (CAMEL_MESSAGE_INFO (camel_folder_thread_node_get_item (node)), CAMEL_MESSAGE_FLAGGED, 0);
}

void
stamp_conversation_list_mark_flag_selected_messages (StampConversationList *self)
{
  StampConversationItem *item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));
  CamelFolderThreadNode *node;

  node = stamp_conversation_item_get_node (item);
  camel_message_info_set_flags (CAMEL_MESSAGE_INFO (camel_folder_thread_node_get_item (node)), CAMEL_MESSAGE_FLAGGED, ~0);
}

void
stamp_conversation_list_unselect (StampConversationList *self)
{
  gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (self->single_selection));
  gtk_bitset_remove_all (self->selected);
}

static void
on_transfer_messages_to (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  CamelFolder *folder = CAMEL_FOLDER (source);
  g_autoptr (GError) error = NULL;

  if (!camel_folder_transfer_messages_to_finish (folder, res, NULL, &error)) {
    g_warning ("%s: Could not move message: %s", G_STRFUNC, error->message);
    return;
  }

  camel_folder_synchronize_sync (folder, FALSE, NULL, &error);
  if (error) {
    g_warning ("%s: Could not synchronize folder: %s", G_STRFUNC, error->message);
  }
}

void
stamp_conversation_list_trash (StampConversationList *self,
                               StampConversationItem *item)
{
  g_autoptr (GPtrArray) node_array = g_ptr_array_new ();
  g_autoptr (GPtrArray) array = NULL;
  CamelFolder *folder;
  CamelFolder *trash_folder;
  g_autoptr (GError) error = NULL;
  GPtrArray *uid_array;

  if (self->selection_mode) {
    GtkBitset *selected_items = self->selected;
    GtkBitsetIter iter;
    guint32 current_item_position;

    gtk_bitset_iter_init_first (&iter, selected_items, &current_item_position);

    while (gtk_bitset_iter_is_valid (&iter)) {
      g_autoptr (StampConversationItem) child_item = STAMP_CONVERSATION_ITEM (g_list_model_get_item (G_LIST_MODEL (self->multi_selection), current_item_position));

      g_ptr_array_add (node_array, child_item);
      gtk_bitset_iter_next (&iter, &current_item_position);
    }

    gtk_bitset_remove_all (self->selected);
    update_selection_title (self);
    refresh_checkboxes (self);
  } else {
    if (!item)
      item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));

    if (!item)
      return;

    g_ptr_array_add (node_array, item);
  }

  self->trash_array = g_ptr_array_ref (node_array);

  for (int idx = 0; idx < node_array->len; idx++) {
    StampConversationItem *child_item = STAMP_CONVERSATION_ITEM (node_array->pdata[idx]);
    CamelFolderThreadNode *child_node = stamp_conversation_item_get_node (child_item);

    stamp_conversation_item_set_hidden (child_item, TRUE);
    array = collect_messages (child_node, array);
  }

  g_list_model_items_changed (G_LIST_MODEL (self->list_store),
                              0,
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)),
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)));

  folder = self->folder;
  trash_folder = stamp_account_get_mail_trash_folder (self->account);

  /* Mark all items as read */
  uid_array = g_ptr_array_new ();
  for (int idx = array->len - 1; idx >= 0; idx--) {
    CamelFolderThreadNode *child_node = array->pdata[idx];
    const CamelMessageInfo *info;

    info = camel_folder_thread_node_get_item (child_node);
    g_ptr_array_add (uid_array, g_strdup (camel_message_info_get_uid (info)));

    camel_folder_set_message_flags (folder, camel_message_info_get_uid (info), CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
  }

  if (self->transfer_cancellable) {
    g_cancellable_cancel (self->transfer_cancellable);
    g_clear_object (&self->transfer_cancellable);
  }
  self->transfer_cancellable = g_cancellable_new ();

  camel_folder_transfer_messages_to (folder, uid_array, trash_folder, TRUE, G_PRIORITY_DEFAULT, self->transfer_cancellable, on_transfer_messages_to, uid_array);

  stamp_conversation_list_unselect (self);
}

void
stamp_mail_conversation_list_search_contact (StampConversationList *self,
                                             const char            *mail)
{
  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), mail);
  gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (self->search_bar), TRUE);
}

void
stamp_conversation_list_undo_trash (StampConversationList *self)
{
  if (!self->trash_array)
    return;

  for (int idx = 0; idx < self->trash_array->len; idx++) {
    StampConversationItem *item = STAMP_CONVERSATION_ITEM (self->trash_array->pdata[idx]);

    stamp_conversation_item_set_hidden (item, FALSE);
  }

  g_list_model_items_changed (G_LIST_MODEL (self->list_store),
                              0,
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)),
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)));
}

GtkToggleButton *
stamp_conversation_list_get_sidebar_button (StampConversationList *self)
{
  return self->sidebar_button;
}

void
stamp_consersation_list_set_show_buttons (StampConversationList *self,
                                          gboolean               show)
{
  adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (self->normal_headerbar), show);
  adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (self->selection_headerbar), show);
}
