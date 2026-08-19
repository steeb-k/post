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

#include <camel/camel.h>
#include <glib/gi18n.h>

#include "stamp-account.h"
#include "stamp-composer.h"
#include "stamp-conversation-item.h"
#include "stamp-conversation-list-store.h"
#include "stamp-conversation-row.h"
#include "stamp-item.h"
#include "stamp-layout-picker.h"
#include "stamp-mail-view.h"
#include "stamp-message-list.h"
#include "stamp-settings.h"

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
  GtkWidget *sort_or_filter_button;
  GtkWidget *mail_list_stack;
  GtkBitset *selected;
  GtkToggleButton *sidebar_button;
  GtkWidget *view_button;
  GtkWidget *view_popover;
  GtkBox *sort_is_active;
  GtkWidget *selection_bottom_bar;
  GtkMenuButton *move_selection_button;
  GtkButton *trash_selection_button;
  GtkButton *mark_read_selection_button;

  GListStore *list_store;
  GtkSingleSelection *single_selection;
  GtkNoSelection *multi_selection;
  GCancellable *cancellable;
  GCancellable *transfer_cancellable;
  GHashTable *folders;
  GList *moved_messages;
  gboolean is_pulling;
  GtkSorter *sorter;
  GtkSorter *section_sorter;
  GtkSortListModel *sort_model;
  GtkListItemFactory *header_factory;
  GtkWidget *collapsed_starred_strip;
  GHashTable *promoted;
  guint starred_recompute_id;
  gboolean starred_expanded;
  GtkFilter *filter;
  gchar *full_name;
  GMenuModel *context_menu_model;

  StampAccount *account;
  CamelFolder *folder;
  gboolean selection_mode;

  enum FilterMode filter_mode;
  enum SortMode sort_mode;
  gboolean claimed;
  gdouble pull_offset;

  CamelFolderThread *thread;
  GHashTable *thread_cache;
  GtkFilterListModel *filter_model;
  guint anchor_position;
  GHashTable *position_to_check;
  GPtrArray *trash_array;
  GPtrArray *trash_uids;
  CamelFolder *trash_source_folder;
  guint pending_load_count;
  gboolean fetching;

  GSimpleActionGroup *actions;
  GMenu *move_menu;
  guint load_folder_handler;
  guint load_more_items_handler;
};

G_DEFINE_FINAL_TYPE (StampConversationList, stamp_conversation_list, ADW_TYPE_BIN);

typedef enum {
  PROP_STATE = 1,
} StampConversationListProps;

static GParamSpec *properties[PROP_STATE + 1];

enum {
  CONVERSATION_SELECTED,
  CONVERSATION_TRASH,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

static gboolean
find_uid_in_thread_node (CamelFolderThreadNode *node,
                         const gchar           *uid)
{
  CamelMessageInfo *info;

  if (!node || !uid)
    return FALSE;

  info = camel_folder_thread_node_get_item (node);
  if (info && g_strcmp0 (camel_message_info_get_uid (info), uid) == 0)
    return TRUE;

  for (CamelFolderThreadNode *child = camel_folder_thread_node_get_child (node); child; child = camel_folder_thread_node_get_next (child)) {
    if (find_uid_in_thread_node (child, uid))
      return TRUE;
  }

  return FALSE;
}

static StampConversationItem *
stamp_conversation_item_find_item (GListStore  *store,
                                   const gchar *uid)
{
  guint list_len = g_list_model_get_n_items (G_LIST_MODEL (store));

  for (guint idx = 0; idx < list_len; idx++) {
    g_autoptr (StampConversationItem) item = g_list_model_get_item (G_LIST_MODEL (store), idx);

    if (item && find_uid_in_thread_node (stamp_conversation_item_get_node (item), uid))
      return g_steal_pointer (&item);
  }
  return NULL;
}

static void
load_folder_idle (gpointer user_data);

static gboolean
load_more_items_idle (gpointer user_data);

static void
queue_starred_recompute (StampConversationList *self);

static gboolean
scroll_to_top_tick (GtkWidget     *widget,
                    GdkFrameClock *clock,
                    gpointer       user_data);


static void
on_conversation_list_folder_changed (CamelFolder           *folder,
                                     CamelFolderChangeInfo *changes,
                                     gpointer               user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  if (changes->uid_changed && changes->uid_changed->len > 0) {
    GPtrArray *changed = changes->uid_changed;
    for (gint idx = 0; idx < changed->len; idx++) {
      g_autoptr (StampConversationItem) item = stamp_conversation_item_find_item (self->list_store, changed->pdata[idx]);
      CamelMessageInfo *message_info;
      gchar *uid = (char *)changed->pdata[idx];

      message_info = camel_folder_summary_get (camel_folder_get_folder_summary (folder), uid);

      if (item)
        stamp_conversation_item_update (item, message_info);
    }

    queue_starred_recompute (self);
  }

  if ((changes->uid_added && changes->uid_added->len > 0) || (changes->uid_removed && changes->uid_removed->len > 0)) {
    g_clear_handle_id (&self->load_folder_handler, g_source_remove);
    self->load_folder_handler = g_idle_add_once (load_folder_idle, self);
  }
}

static CamelFolderThread *
get_thread (StampConversationList *self,
            CamelFolder           *folder)
{
  const gchar *uri = camel_folder_get_full_name (folder);
  CamelFolderThread *thread = g_hash_table_lookup (self->thread_cache, uri);

  if (!thread) {
    g_autoptr (GPtrArray) uids = camel_folder_dup_uids (folder);

    thread = camel_folder_thread_new (folder, uids, CAMEL_FOLDER_THREAD_FLAG_SORT);
    if (thread)
      g_hash_table_insert (self->thread_cache, g_strdup (uri), g_object_ref (thread));
  }

  return thread;
}


/* The starred section.
 *
 * With "starred-first" on, the starred mails that sort first are lifted
 * into a section of their own above the rest of the list. Only as many
 * as "starred-limit" allows go up there; starred mail past the limit
 * stays in the main group and sorts by date like anything else, which is
 * what Gmail does with its own starred section.
 *
 * Which mails are lifted is cached in self->promoted, because both the
 * sorter and the filter have to answer "is this one in the top section?"
 * for every item they touch, and working that out from scratch each time
 * would turn sorting quadratic.
 */
#define STARRED_SECTION 0
#define OTHERS_SECTION  1

static gboolean
has_starred_section (StampConversationList *self)
{
  return self->promoted && g_hash_table_size (self->promoted) > 0;
}

static gboolean
is_promoted (StampConversationList  *self,
             StampConversationItem  *item)
{
  const gchar *uid;

  if (!has_starred_section (self))
    return FALSE;

  uid = stamp_conversation_item_get_uid (item);

  return uid && g_hash_table_contains (self->promoted, uid);
}

static guint
section_of (StampConversationList *self,
            StampConversationItem *item)
{
  return is_promoted (self, item) ? STARRED_SECTION : OTHERS_SECTION;
}

/* Ordering inside one section, which is also the order the starred
 * section picks its top few from. */
static gint
compare_within_section (StampConversationList *self,
                        StampConversationItem *item1,
                        StampConversationItem *item2)
{
  guint timestamp1 = stamp_conversation_item_get_timestamp (item1);
  guint timestamp2 = stamp_conversation_item_get_timestamp (item2);
  gint64 uid1;
  gint64 uid2;

  if (g_settings_get_boolean (STAMP_SETTINGS_MAIL, "important-first")) {
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

static gint
sorter_func (gconstpointer a,
             gconstpointer b,
             gpointer      user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *item1 = (StampConversationItem *)(a);
  StampConversationItem *item2 = (StampConversationItem *)(b);

  if (has_starred_section (self)) {
    guint section1 = section_of (self, item1);
    guint section2 = section_of (self, item2);

    if (section1 != section2)
      return section1 < section2 ? -1 : 1;
  }

  return compare_within_section (self, item1, item2);
}

/* Tells the sort model where one section ends and the next begins, so
 * that the list view can ask for a header at the boundary. */
static gint
section_sorter_func (gconstpointer a,
                     gconstpointer b,
                     gpointer      user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  guint section1 = section_of (self, (StampConversationItem *)(a));
  guint section2 = section_of (self, (StampConversationItem *)(b));

  if (section1 == section2)
    return 0;

  return section1 < section2 ? -1 : 1;
}

/* Everything the search box and the filter menu have to say about an
 * item. Kept apart from filter_func because a collapsed starred section
 * must not change which mails belong to it. */
static gboolean
base_filter_matches (StampConversationList *self,
                     StampConversationItem *item)
{
  const gchar *search_text = gtk_editable_get_text (GTK_EDITABLE (self->search_entry));
  gint search_text_len = search_text ? strlen (search_text) : 0;

  if (stamp_conversation_item_get_hidden (item))
    return FALSE;

  if (search_text_len) {
    g_autofree char *search = g_utf8_strdown (search_text, search_text_len);
    g_auto (GStrv) words = g_strsplit (search, " ", -1);
    gboolean all_found = TRUE;

    for (gint i = 0; words[i] && all_found; i++) {
      gboolean word_found = FALSE;

      if (strlen (words[i]) == 0)
        continue;

      if (!word_found) {
        const gchar *item_from = stamp_conversation_item_get_from (item);

        if (item_from && strlen (item_from) > 0) {
          g_autofree char *from = g_utf8_strdown (item_from, -1);

          if (g_strstr_len (from, -1, words[i]))
            word_found = TRUE;
        }
      }

      if (!word_found) {
        const gchar *item_subject = stamp_conversation_item_get_subject (item);

        if (item_subject && strlen (item_subject) > 0) {
          g_autofree char *subject = g_utf8_strdown (item_subject, -1);

          if (g_strstr_len (subject, -1, words[i]))
            word_found = TRUE;
        }
      }

      if (!word_found) {
        const gchar *item_mail = stamp_conversation_item_get_mail (item);

        if (item_mail && strlen (item_mail) > 0) {
          g_autofree char *mail = g_utf8_strdown (item_mail, -1);

          if (g_strstr_len (mail, -1, words[i]))
            word_found = TRUE;
        }
      }

      if (!word_found) {
        const gchar *item_preview = stamp_conversation_item_get_preview (item);

        if (item_preview && strlen (item_preview) > 0) {
          g_autofree char *preview = g_utf8_strdown (item_preview, -1);

          if (g_strstr_len (preview, -1, words[i]))
            word_found = TRUE;
        }
      }

      if (!word_found)
        all_found = FALSE;
    }

    if (!all_found)
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

static gboolean
filter_func (gpointer object,
             gpointer user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *item = STAMP_CONVERSATION_ITEM (object);

  if (!base_filter_matches (self, item))
    return FALSE;

  /* A collapsed starred section drops its rows but keeps its header. */
  if (!self->starred_expanded && is_promoted (self, item))
    return FALSE;

  return TRUE;
}

static gint
starred_rank_compare (gconstpointer a,
                      gconstpointer b,
                      gpointer      user_data)
{
  return compare_within_section (STAMP_CONVERSATION_LIST (user_data),
                                 *(StampConversationItem **)(a),
                                 *(StampConversationItem **)(b));
}

static gboolean
promoted_sets_equal (GHashTable *one,
                     GHashTable *other)
{
  GHashTableIter iter;
  gpointer key;

  if (g_hash_table_size (one) != g_hash_table_size (other))
    return FALSE;

  g_hash_table_iter_init (&iter, one);
  while (g_hash_table_iter_next (&iter, &key, NULL)) {
    if (!g_hash_table_contains (other, key))
      return FALSE;
  }

  return TRUE;
}

static void
update_collapsed_starred_strip (StampConversationList *self);

/* Sections only exist while there is starred mail to put in them. A lone
 * "Everything else" header over an otherwise ordinary list would be
 * noise, so the headers come and go with the starred section. */
static void
update_sections (StampConversationList *self)
{
  gboolean have_section = has_starred_section (self);
  /* The list view holds its place by anchoring on a row that is already
   * on screen, so lifting mails into a new section above that row pushes
   * the section and its header off the top of the view. Someone sitting
   * at the top of the list meant to be at the top of it. */
  gboolean was_at_top = !self->vadj || gtk_adjustment_get_value (self->vadj) <= 1.0;

  gtk_sort_list_model_set_section_sorter (self->sort_model,
                                          have_section ? self->section_sorter : NULL);
  gtk_list_view_set_header_factory (GTK_LIST_VIEW (self->listview),
                                    have_section ? self->header_factory : NULL);

  gtk_sorter_changed (self->sorter, GTK_SORTER_CHANGE_DIFFERENT);
  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);

  if (was_at_top) {
    gtk_list_view_scroll_to (GTK_LIST_VIEW (self->listview), 0, GTK_LIST_SCROLL_NONE, NULL);
    gtk_widget_add_tick_callback (GTK_WIDGET (self), scroll_to_top_tick, self, NULL);
  }

  update_collapsed_starred_strip (self);
}

static void
starred_recompute (StampConversationList *self)
{
  g_autoptr (GPtrArray) starred = g_ptr_array_new_with_free_func (g_object_unref);
  GHashTable *promoted;
  guint limit = g_settings_get_uint (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_STARRED_LIMIT);
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));

  /* A starred section inside the starred-only filter would just repeat
   * the first few rows under a heading, so leave it out there. */
  if (g_settings_get_boolean (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_STARRED_FIRST) &&
      self->filter_mode != FILTER_MODE_STARRED) {
    for (guint idx = 0; idx < n_items; idx++) {
      StampConversationItem *item = g_list_model_get_item (G_LIST_MODEL (self->list_store), idx);

      /* base_filter_matches, not filter_func: collapsing the section
       * must not change what belongs in it. */
      if (stamp_conversation_item_get_flagged (item) && base_filter_matches (self, item))
        g_ptr_array_add (starred, item);
      else
        g_object_unref (item);
    }

    g_ptr_array_sort_with_data (starred, starred_rank_compare, self);
  }

  promoted = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (guint idx = 0; idx < starred->len && idx < limit; idx++) {
    const gchar *uid = stamp_conversation_item_get_uid (starred->pdata[idx]);

    if (uid)
      g_hash_table_add (promoted, g_strdup (uid));
  }

  /* Re-sorting the whole list on every folder change would make
   * scrolling stutter, so only disturb it when the section really moved. */
  if (self->promoted && promoted_sets_equal (self->promoted, promoted)) {
    g_hash_table_unref (promoted);
    return;
  }

  g_clear_pointer (&self->promoted, g_hash_table_unref);
  self->promoted = promoted;

  update_sections (self);
}

static void
starred_recompute_idle (gpointer user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  self->starred_recompute_id = 0;
  starred_recompute (self);
}

static void
queue_starred_recompute (StampConversationList *self)
{
  if (self->starred_recompute_id)
    return;

  self->starred_recompute_id = g_idle_add_once (starred_recompute_idle, self);
}

static void
on_starred_settings_changed (StampConversationList *self)
{
  queue_starred_recompute (self);
}

static void
on_important_first_changed (StampConversationList *self)
{
  gtk_sorter_changed (self->sorter, GTK_SORTER_CHANGE_DIFFERENT);
  queue_starred_recompute (self);
}

static void
on_starred_section_toggled (GtkButton *button,
                            gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  self->starred_expanded = !self->starred_expanded;
  g_settings_set_boolean (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_STARRED_EXPANDED, self->starred_expanded);

  gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);
  update_collapsed_starred_strip (self);
}

static GtkWidget *
build_section_header (StampConversationList *self,
                      guint                  section)
{
  GtkWidget *label;
  GtkWidget *box;

  label = gtk_label_new (section == STARRED_SECTION ? _("Starred") : _("Everything else"));
  gtk_widget_add_css_class (label, "heading");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);

  if (section != STARRED_SECTION) {
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class (box, "conversation-section-header");
    gtk_box_append (GTK_BOX (box), label);

    return box;
  }

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  /* A button centres its child, and a header centred over a full-width
   * row looks like a title bar rather than the start of a group. */
  gtk_widget_set_halign (box, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (box), gtk_image_new_from_icon_name (self->starred_expanded ? "pan-down-symbolic"
                                                                                     : "pan-end-symbolic"));
  gtk_box_append (GTK_BOX (box), label);

  /* The whole header is the target, not just the arrow: it is a wide,
   * thin row and aiming at the icon on a phone is no fun. */
  {
    GtkWidget *button = gtk_button_new ();

    gtk_button_set_child (GTK_BUTTON (button), box);
    gtk_widget_add_css_class (button, "flat");
    gtk_widget_add_css_class (button, "conversation-section-header");
    gtk_widget_set_tooltip_text (button, self->starred_expanded ? _("Hide starred mails")
                                                               : _("Show starred mails"));
    gtk_accessible_update_state (GTK_ACCESSIBLE (button), GTK_ACCESSIBLE_STATE_EXPANDED,
                                 self->starred_expanded, -1);
    g_signal_connect_object (button, "clicked", G_CALLBACK (on_starred_section_toggled), self, G_CONNECT_DEFAULT);

    return button;
  }
}

static void
remove_children (GtkWidget *box)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (box)))
    gtk_box_remove (GTK_BOX (box), child);
}

/* With the starred section collapsed and nothing left in the list to
 * hang a header off, the header gets pinned above the list instead --
 * otherwise there would be nothing to click to bring the mails back. */
static void
update_collapsed_starred_strip (StampConversationList *self)
{
  gboolean needed = has_starred_section (self) &&
                    !self->starred_expanded &&
                    g_list_model_get_n_items (G_LIST_MODEL (self->sort_model)) == 0;

  remove_children (self->collapsed_starred_strip);

  if (needed)
    gtk_box_append (GTK_BOX (self->collapsed_starred_strip), build_section_header (self, STARRED_SECTION));

  gtk_widget_set_visible (self->collapsed_starred_strip, needed);
}

static void
on_setup_section_header (GtkListItemFactory *factory,
                         GtkListHeader      *header,
                         gpointer            user_data)
{
  gtk_list_header_set_child (header, gtk_box_new (GTK_ORIENTATION_VERTICAL, 0));
}

static void
on_bind_section_header (GtkListItemFactory *factory,
                        GtkListHeader      *header,
                        gpointer            user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkWidget *box = gtk_list_header_get_child (header);
  StampConversationItem *item = gtk_list_header_get_item (header);
  guint section;

  remove_children (box);

  if (!item)
    return;

  section = section_of (self, item);

  /* A collapsed starred section has no rows of its own for a header to
   * sit above, so the group below it carries that header along too. */
  if (section == OTHERS_SECTION && !self->starred_expanded)
    gtk_box_append (GTK_BOX (box), build_section_header (self, STARRED_SECTION));

  gtk_box_append (GTK_BOX (box), build_section_header (self, section));
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
  const gchar *action_name = g_action_get_name (G_ACTION (action));
  const gchar *id = action_name + 4;

  g_simple_action_set_state (action, g_variant_new_boolean (!active));
  stamp_conversation_item_set_label (item, id, !active);
}

static void
rebuild_category_actions (StampConversationList *self)
{
  GActionMap *map = G_ACTION_MAP (self->actions);
  gchar **old_names = g_object_get_data (G_OBJECT (self->actions), "cat-action-names");
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
    gchar *action_name;
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

/* An empty list means one of two things. Either the folder really has
 * no mail, or its contents have not been fetched yet, which happens on
 * a fresh account and can take a while. Saying "No Mails Found" during
 * the fetch is wrong, so show that we are working instead. */
static void
stamp_conversation_list_update_state (StampConversationList *self)
{
  const gchar *page;

  if (g_list_model_get_n_items (G_LIST_MODEL (self->list_store)) > 0)
    page = "list";
  else if (self->fetching)
    page = "loading";
  else
    page = "empty";

  gtk_stack_set_visible_child_name (GTK_STACK (self->mail_list_stack), page);
}

static void
on_initial_fetch_done (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  g_autoptr (GError) error = NULL;

  if (!camel_folder_refresh_info_finish (CAMEL_FOLDER (source), res, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("%s: Could not fetch mail: %s", G_STRFUNC, error ? error->message : "");

  self->fetching = FALSE;
  stamp_conversation_list_update_state (self);
}

static void
on_items_changed (GListModel *model,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  stamp_conversation_list_update_state (self);
  update_collapsed_starred_strip (self);
  queue_starred_recompute (self);

  rebuild_category_actions (self);
}

static void
append_folder_items (GMenu           *menu,
                     CamelFolderInfo *fi,
                     const gchar     *current_folder)
{
  GMenu *section = g_menu_new ();

  while (fi) {
    if (fi->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL)) {
      fi = fi->next;
      continue;
    }

    if (current_folder && g_strcmp0 (fi->full_name, current_folder) == 0) {
      if (fi->child)
        append_folder_items (menu, fi->child, current_folder);

      fi = fi->next;
      continue;
    }

    if (fi->child) {
      GMenu *submenu = g_menu_new ();

      append_folder_items (submenu, fi->child, current_folder);
      g_menu_append_submenu (section, fi->display_name, G_MENU_MODEL (submenu));
    } else {
      g_autofree gchar *action = g_strdup_printf ("conversation-list.move-folder::%s", fi->full_name);

      g_menu_append (section, fi->display_name, action);
    }

    fi = fi->next;
  }

  if (g_menu_model_get_n_items (G_MENU_MODEL (section)) > 0)
    g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
  else
    g_clear_object (&section);
}

static void
on_folder_info_for_move_menu (GObject      *src,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelFolderInfo) root = camel_store_get_folder_info_finish (CAMEL_STORE (src), res, &error);

  if (error) {
    g_warning ("Could not get folder info: %s", error->message);

    self->move_menu = g_menu_new ();
    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->move_selection_button), G_MENU_MODEL (self->move_menu));
    return;
  }

  self->move_menu = g_menu_new ();

  if (root)
    append_folder_items (self->move_menu, root, self->full_name);

  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->move_selection_button), G_MENU_MODEL (self->move_menu));
}

static void
rebuild_move_menu (StampConversationList *self)
{
  StampMailService *mail_service;
  CamelStore *store;

  if (!self->account)
    return;

  g_clear_object (&self->move_menu);

  mail_service = stamp_account_get_mail_service (self->account);
  store = CAMEL_STORE (stamp_mail_service_get_service (mail_service));

  camel_store_get_folder_info (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL, G_PRIORITY_DEFAULT, NULL, on_folder_info_for_move_menu, self);
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

    rebuild_category_actions (self);
    return;
  }

  self->folder = g_object_ref (folder);
  g_signal_connect_object (folder, "changed", G_CALLBACK (on_conversation_list_folder_changed), self, G_CONNECT_DEFAULT);

  mail_service = stamp_account_get_mail_service (self->account);
  g_hash_table_insert (self->folders, g_strdup (camel_service_get_uid (stamp_mail_service_get_service (mail_service))), g_object_ref (folder));

  thread = get_thread (self, folder);
  self->thread = thread;

  array = g_ptr_array_new_with_free_func (g_object_unref);

  if (thread) {
    CamelFolderThreadNode *child;
    const gchar *service_uid = camel_service_get_uid (stamp_mail_service_get_service (mail_service));
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

  if (g_list_model_get_n_items (G_LIST_MODEL (self->list_store)) > 0) {
    gtk_list_view_scroll_to (GTK_LIST_VIEW (self->listview), 0, GTK_LIST_SCROLL_FOCUS, NULL);
  } else {
    /* Nothing stored locally for this folder yet, so fetch it now
     * instead of waiting for the folder list to get around to it. */
    self->fetching = TRUE;
    camel_folder_refresh_info (folder, G_PRIORITY_DEFAULT, self->cancellable, on_initial_fetch_done, self);
  }

  stamp_conversation_list_update_state (self);

  if (self->pending_load_count > 0) {
    g_clear_handle_id (&self->load_more_items_handler, g_source_remove);
    self->load_more_items_handler = g_idle_add (load_more_items_idle, self);
  }

  rebuild_move_menu (self);
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
  const gchar *service_uid;

  if (!self->thread || self->pending_load_count == 0) {
    self->load_more_items_handler = 0;
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

  self->load_more_items_handler = 0;
  return G_SOURCE_REMOVE;
}

/*
 * Write out any star still sitting on its delay. The mails on screen
 * are about to lose the thread they would be written through -- a
 * folder reload, or the list going away -- and a star the user clicked
 * should reach the server either way.
 */
static void
flush_pending_stars (StampConversationList *self)
{
  guint n_items;

  if (!self->list_store)
    return;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));

  for (guint idx = 0; idx < n_items; idx++) {
    g_autoptr (StampConversationItem) item = g_list_model_get_item (G_LIST_MODEL (self->list_store), idx);

    stamp_conversation_item_flush_star (item);
  }
}

void
stamp_conversation_list_load_folder (StampConversationList *self,
                                     StampAccount          *account,
                                     gchar                 *full_name)
{
  StampMailService *mail_service;

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  flush_pending_stars (self);

  g_hash_table_insert (self->thread_cache, g_strdup (full_name), NULL);

  if (!account)
    return;

  self->cancellable = g_cancellable_new ();
  self->fetching = FALSE;

  mail_service = stamp_account_get_mail_service (account);
  self->account = account;

  g_set_str (&self->full_name, full_name);

  camel_store_get_folder (CAMEL_STORE (stamp_mail_service_get_service (mail_service)), full_name, CAMEL_STORE_FOLDER_NONE, G_PRIORITY_DEFAULT, self->cancellable, on_get_folder, self);
}

/**
 * stamp_conversation_list_clear:
 * @self: a #StampConversationList
 *
 * Drops whatever folder is on screen, without loading another. Used
 * when the active profile stops showing the account the current folder
 * belongs to: leaving its mail visible would defeat the point.
 */
void
stamp_conversation_list_clear (StampConversationList *self)
{
  g_return_if_fail (STAMP_IS_CONVERSATION_LIST (self));

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  self->account = NULL;
  g_clear_pointer (&self->full_name, g_free);

  flush_pending_stars (self);

  g_list_store_remove_all (self->list_store);
}

static void
on_mail_search_entry_changed (GtkWidget *search_entry,
                              gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  if (self->filter)
    gtk_filter_changed (self->filter, GTK_FILTER_CHANGE_DIFFERENT);

  queue_starred_recompute (self);
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
  gdouble start_x;
  gdouble start_y;
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
  StampConversationItem *conversation_item;

  gtk_selection_model_select_item (model, position, TRUE);

  menu = g_menu_new ();
  g_menu_append (menu, _("Reply"), "mail.reply-current");
  g_menu_append (menu, _("Reply All"), "mail.reply-all-current");
  g_menu_append (menu, _("Forward"), "mail.forward-current");

  conversation_item = stamp_conversation_row_get_item (child);

  if (stamp_conversation_item_get_unread (conversation_item))
    g_menu_append (menu, _("Mark Read"), "conversation-list.mark-read");
  else
    g_menu_append (menu, _("Mark Unread"), "conversation-list.mark-unread");

  if (stamp_conversation_item_get_flagged (conversation_item))
    g_menu_append (menu, _("Mark Unstarred"), "conversation-list.mark-unstarred");
  else
    g_menu_append (menu, _("Mark Starred"), "conversation-list.mark-starred");

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
      for (gint idx = 0; idx < labels->len; idx++) {
        gchar *cat_name = labels->pdata[idx];

        if (g_strcmp0 (cat_name, action_name + 4) == 0) {
          g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (TRUE));
          break;
        }
      }
    }

    g_menu_append_submenu (menu, _("Category"), G_MENU_MODEL (sub_menu));
  }

  popover = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
  gtk_widget_set_halign (popover, GTK_ALIGN_START);
  gtk_widget_set_valign (popover, GTK_ALIGN_START);
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
update_selection_title (StampConversationList *self)
{
  guint n_selected = gtk_bitset_get_size (self->selected);
  gchar buf[64];

  if (n_selected) {
    g_autofree char *tooltip = g_strdup_printf (g_dngettext (NULL, "Move Selected Message to Trash", "Move %u Selected Messages to Trash", n_selected), n_selected);

    gtk_widget_set_tooltip_text (GTK_WIDGET (self->trash_selection_button), tooltip);
    gtk_widget_set_sensitive (GTK_WIDGET (self->trash_selection_button), TRUE);
  } else {
    gtk_widget_set_tooltip_text (GTK_WIDGET (self->trash_selection_button), NULL);
    gtk_widget_set_sensitive (GTK_WIDGET (self->trash_selection_button), FALSE);
  }

  if (n_selected) {
    g_autofree char *tooltip = g_strdup_printf (g_dngettext (NULL, "Mark Selected Message as Read", "Mark %u Selected Messages as Read", n_selected), n_selected);

    gtk_widget_set_tooltip_text (GTK_WIDGET (self->mark_read_selection_button), tooltip);
    gtk_widget_set_sensitive (GTK_WIDGET (self->mark_read_selection_button), TRUE);
  } else {
    gtk_widget_set_tooltip_text (GTK_WIDGET (self->mark_read_selection_button), NULL);
    gtk_widget_set_sensitive (GTK_WIDGET (self->mark_read_selection_button), FALSE);
  }

  if (n_selected) {
    g_autofree char *tooltip = g_strdup_printf (g_dngettext (NULL, "Move Selected Message…", "Move %u Selected Messages…", n_selected), n_selected);

    gtk_widget_set_tooltip_text (GTK_WIDGET (self->move_selection_button), tooltip);
    gtk_widget_set_sensitive (GTK_WIDGET (self->move_selection_button), TRUE);
  } else {
    gtk_widget_set_tooltip_text (GTK_WIDGET (self->move_selection_button), NULL);
    gtk_widget_set_sensitive (GTK_WIDGET (self->move_selection_button), FALSE);
  }

  g_snprintf (buf, sizeof buf, _("%u selected"), n_selected);
  adw_window_title_set_title (ADW_WINDOW_TITLE (self->selection_label), buf);
}

static void
set_selection_active (StampConversationList *self,
                      gboolean               selection_active)
{
  self->selection_mode = selection_active;

  if (self->selection_mode) {
    gtk_bitset_remove_all (self->selected);
    update_selection_title (self);
    gtk_stack_set_visible_child (GTK_STACK (self->header_stack), self->selection_headerbar);
    gtk_list_view_set_model (GTK_LIST_VIEW (self->listview), GTK_SELECTION_MODEL (self->multi_selection));
    gtk_widget_set_visible (self->selection_bottom_bar, TRUE);
  } else {
    gtk_stack_set_visible_child (GTK_STACK (self->header_stack), self->normal_headerbar);
    gtk_list_view_set_model (GTK_LIST_VIEW (self->listview), GTK_SELECTION_MODEL (self->single_selection));
    gtk_widget_set_visible (self->selection_bottom_bar, FALSE);
    stamp_conversation_list_unselect (self);
  }

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
on_long_press_pressed (GtkGestureLongPress *controller,
                       gdouble              x,
                       gdouble              y,
                       gpointer             user_data)
{
  RowData *row_data = g_object_get_data (G_OBJECT (controller), "row-data");
  StampConversationList *self = STAMP_CONVERSATION_LIST (row_data->self);
  guint position = gtk_list_item_get_position (row_data->list_item);

  /* Entering selection mode clears the selection, so only do it once */
  if (!self->selection_mode)
    set_selection_active (self, TRUE);

  gtk_bitset_add (self->selected, position);
  self->anchor_position = position;
  update_selection_title (self);
  refresh_checkboxes (self);
}

static void
on_row_pressed (GtkGestureClick *gesture,
                gint             n_press,
                gdouble          x,
                gdouble          y,
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

  state = gdk_event_get_modifier_state (event);
  ctrl = (state & GDK_CONTROL_MASK) != 0;
  shift = (state & GDK_SHIFT_MASK) != 0;

  if (!self->selection_mode) {
    if (!ctrl && !shift) {
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
    } else {
      /* A modifier click enters selection mode, using the currently shown
       * conversation as the anchor of the new selection */
      guint anchor = gtk_single_selection_get_selected (self->single_selection);

      set_selection_active (self, TRUE);

      if (anchor != GTK_INVALID_LIST_POSITION && anchor != position) {
        self->anchor_position = anchor;
        gtk_bitset_add (self->selected, anchor);
      } else {
        self->anchor_position = position;
      }
    }
  }

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
                gint             n_press,
                gdouble          x,
                gdouble          y,
                gpointer         user_data)
{
  RowData *data = g_object_get_data (G_OBJECT (gesture), "row-data");
  data->start_x = x;
  data->start_y = y;
}

static void
on_touch_released (GtkGestureClick *gesture,
                   gint             n_press,
                   gdouble          x,
                   gdouble          y,
                   gpointer         user_data)
{
  RowData *data = g_object_get_data (G_OBJECT (gesture), "row-data");
  StampConversationList *self = data->self;
  GtkListItem *list_item = data->list_item;

  /* Bewegung prüfen – wenn zu viel bewegt wurde war es ein Scroll */
  gdouble dx = x - data->start_x;
  gdouble dy = y - data->start_y;
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

static GdkContentProvider *
on_conversation_drag_prepare (GtkDragSource *source,
                              gdouble        x,
                              gdouble        y,
                              gpointer       user_data)
{
  GdkEvent *event;
  GtkListItem *list_item;
  StampConversationItem *item;

  event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (source));
  if (event) {
    GdkDevice *device;

    device = gdk_event_get_device (event);
    if (device && gdk_device_get_source (device) == GDK_SOURCE_TOUCHSCREEN)
      return NULL;
  }

  list_item = GTK_LIST_ITEM (user_data);
  item = STAMP_CONVERSATION_ITEM (gtk_list_item_get_item (list_item));

  if (!item)
    return NULL;

  g_type_ensure (STAMP_TYPE_CONVERSATION_ITEM);
  return gdk_content_provider_new_typed (STAMP_TYPE_CONVERSATION_ITEM, item);
}

static void
on_conversation_drag_begin (GtkDragSource *source,
                            GdkDrag       *drag,
                            GtkWidget     *row)
{
  GtkWidget *parent;
  graphene_size_t size;
  g_autoptr (GtkSnapshot) snapshot = NULL;
  g_autoptr (GdkPaintable) paintable = NULL;

  parent = gtk_widget_get_parent (row);
  if (!parent)
    return;

  size = GRAPHENE_SIZE_INIT (gtk_widget_get_width (row),
                             gtk_widget_get_height (row));
  snapshot = gtk_snapshot_new ();
  gtk_widget_snapshot_child (parent, row, snapshot);
  paintable = gtk_snapshot_free_to_paintable (snapshot, &size);

  if (paintable)
    gtk_drag_source_set_icon (source, paintable, 0, 0);
}

static void
on_conversation_drag_cancel (GtkDragSource *source,
                             GdkDrag       *drag,
                             GdkDragAction  action,
                             gpointer       user_data)
{
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
  GtkDragSource *drag_source;

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
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (long_press), FALSE);
  g_signal_connect (long_press, "pressed", G_CALLBACK (on_long_press_pressed), self);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (long_press));

  g_signal_connect_object (row, "mark-read", G_CALLBACK (on_row_mark_read), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (row, "mark-unread", G_CALLBACK (on_row_mark_unread), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (row, "trash", G_CALLBACK (on_row_trash), self, G_CONNECT_DEFAULT);

  drag_source = gtk_drag_source_new ();
  gtk_drag_source_set_actions (drag_source, GDK_ACTION_MOVE);
  g_signal_connect (drag_source, "prepare", G_CALLBACK (on_conversation_drag_prepare), list_item);
  g_signal_connect (drag_source, "drag-begin", G_CALLBACK (on_conversation_drag_begin), row);
  g_signal_connect (drag_source, "drag-cancel", G_CALLBACK (on_conversation_drag_cancel), row);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (drag_source));

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

  stamp_conversation_row_bind_mail (STAMP_CONVERSATION_ROW (row), model, self->account,
                                    self->folder ? camel_folder_get_full_name (self->folder) : NULL);
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

  if (conversation_item) {
    g_signal_emit (self, signals[CONVERSATION_SELECTED], 0, stamp_conversation_item_get_node (conversation_item));
  } else {
    g_signal_emit (self, signals[CONVERSATION_SELECTED], 0, 0);
  }
}

static void
on_select_all (GtkWidget *button,
               gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self->multi_selection));

  gtk_bitset_add_range (self->selected, 0, n_items);
  update_selection_title (self);
  refresh_checkboxes (self);
}

static void
on_unselect_all (GtkWidget *button,
                 gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  gtk_bitset_remove_all (self->selected);
  update_selection_title (self);
  refresh_checkboxes (self);
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                gpointer               user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkWidget *focus;
  gboolean shift;

  if (!self->selection_mode)
    return GDK_EVENT_PROPAGATE;

  focus = gtk_root_get_focus (gtk_widget_get_root (GTK_WIDGET (self)));
  if (focus && (focus == self->search_entry || gtk_widget_is_ancestor (focus, self->search_entry)))
    return GDK_EVENT_PROPAGATE;

  if (keyval == GDK_KEY_Escape) {
    set_selection_active (self, FALSE);
    return GDK_EVENT_STOP;
  }

  if ((keyval != GDK_KEY_a && keyval != GDK_KEY_A) || (state & GDK_CONTROL_MASK) == 0 || (state & (GDK_ALT_MASK | GDK_META_MASK)) != 0)
    return GDK_EVENT_PROPAGATE;

  shift = (state & GDK_SHIFT_MASK) != 0;

  if (shift) {
    on_unselect_all (NULL, self);
  } else {
    on_select_all (NULL, self);
  }

  return GDK_EVENT_STOP;
}

static void
on_cancel_selection_clicked (GtkWidget *button,
                             gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  set_selection_active (self, FALSE);
}

static gboolean
scroll_to_top_tick (GtkWidget     *widget,
                    GdkFrameClock *clock,
                    gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));

  if (gtk_adjustment_get_value (adj) > 1.0) {
    gtk_adjustment_set_value (adj, 0.0);
    return G_SOURCE_CONTINUE;
  }

  gtk_widget_set_visible (self->scroll_to_top, FALSE);
  return G_SOURCE_REMOVE;
}

static void
on_scroll_to_top (GtkButton *button,
                  gpointer   user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  gtk_list_view_scroll_to (GTK_LIST_VIEW (self->listview), 0, GTK_LIST_SCROLL_NONE, NULL);
  gtk_widget_add_tick_callback (GTK_WIDGET (self), scroll_to_top_tick, self, NULL);
}

static void
stamp_conversation_list_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (object);

  switch ((StampConversationListProps)property_id) {
    case PROP_STATE:
      g_value_set_boolean (value, self->selection_mode);
      break;
  }
}

static void
stamp_conversation_list_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  switch ((StampConversationListProps)property_id) {
    case PROP_STATE:
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

  g_clear_handle_id (&self->load_folder_handler, g_source_remove);
  g_clear_handle_id (&self->load_more_items_handler, g_source_remove);
  g_clear_handle_id (&self->starred_recompute_id, g_source_remove);

  flush_pending_stars (self);

  g_clear_object (&self->section_sorter);
  g_clear_object (&self->header_factory);
  g_clear_object (&self->sort_model);
  g_clear_pointer (&self->promoted, g_hash_table_unref);

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
  g_clear_pointer (&self->trash_array, g_ptr_array_unref);
  g_clear_pointer (&self->trash_uids, g_ptr_array_unref);
  g_clear_object (&self->trash_source_folder);

  gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_CONVERSATION_LIST);

  G_OBJECT_CLASS (stamp_conversation_list_parent_class)->dispose (object);
}

static void
load_folder_idle (gpointer user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  stamp_conversation_list_load_folder (self, self->account, self->full_name);

  self->load_folder_handler = 0;
}

/* Pull to refresh.
 *
 * Two very different devices end up here: a pointer or a finger dragging
 * the list down past its top edge, and a trackpad scrolling past it. They
 * differ only in how far the list has been pulled, so each one measures
 * that and hands it to pull_update (); everything after that — the spinner,
 * the distance that arms a refresh, the release — is shared.
 */
#define PULL_MAX      150.0  /* the spinner stops following past this   */
#define PULL_ARMED    125.0  /* released past this, the list refreshes  */
#define PULL_START     10.0  /* a drag shorter than this is not a pull  */
#define PULL_SPINNER   32.0  /* the spinner's full size                 */
#define SPINNER_MARGIN 12    /* where the spinner rests when idle       */

static gboolean
list_is_at_top (StampConversationList *self)
{
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));

  return gtk_adjustment_get_value (adj) <= 0.0;
}

/* Hangs the spinner off the top edge, @offset pixels down. */
static void
pull_update (StampConversationList *self,
             gdouble                offset)
{
  gdouble clean_y = CLAMP (offset, 0.0, PULL_MAX);

  self->pull_offset = clean_y;
  self->is_pulling = TRUE;

  gtk_widget_set_visible (self->spinner, TRUE);

  /* Up to its full size the spinner grows in place; past that it keeps its
   * size and follows the pull down instead. */
  if (clean_y > PULL_SPINNER) {
    gtk_widget_set_margin_top (self->spinner, clean_y);
    gtk_widget_set_size_request (self->spinner, PULL_SPINNER, PULL_SPINNER);
  } else {
    gtk_widget_set_margin_top (self->spinner, SPINNER_MARGIN);
    gtk_widget_set_size_request (self->spinner, clean_y, clean_y);
  }
}

/* Puts the spinner back where an untouched list keeps it. */
static void
pull_reset (StampConversationList *self)
{
  self->pull_offset = 0.0;
  self->is_pulling = FALSE;

  gtk_widget_set_visible (self->spinner, FALSE);
  gtk_widget_set_margin_top (self->spinner, SPINNER_MARGIN);
  gtk_widget_set_size_request (self->spinner, PULL_SPINNER, PULL_SPINNER);
}

/*
 * Let go of the pull: refresh if it got far enough, otherwise retract. A
 * refresh leaves the spinner up to spin, and the reload hides it once it
 * lands.
 */
static void
pull_release (StampConversationList *self)
{
  gboolean armed = self->pull_offset >= PULL_ARMED;

  g_debug ("%s: released at %.0fpx, %s", G_STRFUNC, self->pull_offset,
           armed ? "refreshing" : "retracting");

  if (!armed) {
    pull_reset (self);
    return;
  }

  self->pull_offset = 0.0;
  self->is_pulling = FALSE;

  g_clear_handle_id (&self->load_folder_handler, g_source_remove);
  self->load_folder_handler = g_idle_add_once (load_folder_idle, self);
}

/*
 * Input 1: a pointer or finger dragging the list.
 *
 * The gesture is a plain drag rather than a pan, because a pan gesture
 * gives up on the whole sequence when the first few pixels of movement are
 * not clearly along its own axis — which is exactly what the start of a
 * trackpad drag looks like. Whether this drag is the downwards one we want
 * is decided here instead, once per drag.
 */
static void
on_drag_update (GtkGestureDrag *gesture,
                gdouble         offset_x,
                gdouble         offset_y,
                gpointer        user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  if (!self->claimed) {
    /* Sideways or upwards belongs to whatever the drag started on. */
    if (offset_y <= 0.0 || ABS (offset_x) > offset_y) {
      gtk_widget_set_visible (self->scroll_to_top, FALSE);
      return;
    }

    /* Dragging down with the list scrolled offers the way back up. */
    if (!list_is_at_top (self)) {
      gtk_widget_set_visible (self->scroll_to_top, TRUE);
      return;
    }

    gtk_widget_set_visible (self->scroll_to_top, FALSE);

    if (offset_y < PULL_START)
      return;

    /* Claiming takes the sequence off the rows underneath, which would
     * otherwise treat the drag as a press. */
    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_widget_set_sensitive (GTK_WIDGET (self->scrolled_window), FALSE);
    self->claimed = TRUE;

    /* An interrupted trackpad pull must not count towards this drag. */
    self->pull_offset = 0.0;
  }

  pull_update (self, offset_y);
}

static void
drag_finished (StampConversationList *self,
               gboolean               released)
{
  if (!self->claimed)
    return;

  if (released)
    pull_release (self);
  else
    pull_reset (self);

  gtk_widget_set_sensitive (GTK_WIDGET (self->scrolled_window), TRUE);
  self->claimed = FALSE;
}

static void
on_drag_end (GtkGestureDrag *gesture,
             gdouble         offset_x,
             gdouble         offset_y,
             gpointer        user_data)
{
  drag_finished (STAMP_CONVERSATION_LIST (user_data), TRUE);
}

/* Something else took the sequence over; drop the pull rather than act on it. */
static void
on_drag_cancel (GtkGesture       *gesture,
                GdkEventSequence *sequence,
                gpointer          user_data)
{
  drag_finished (STAMP_CONVERSATION_LIST (user_data), FALSE);
}

/*
 * Input 2: a trackpad scrolling past the top edge.
 *
 * A two-finger swipe produces scroll deltas rather than a drag, so the
 * overscroll is accumulated here and handed to the same pull. Only
 * pixel-precise devices take part: a mouse wheel spun at the top of the
 * list should not refresh.
 */
static gboolean
scroll_is_pixel_precise (GtkEventControllerScroll *controller)
{
  return gtk_event_controller_scroll_get_unit (controller) == GDK_SCROLL_UNIT_SURFACE;
}

/*
 * A trackpad can report a drag as scroll deltas as well as motion, so a
 * scroll that arrives with the button down belongs to the drag underway and
 * must not start a pull of its own.
 */
static gboolean
scroll_is_dragging (GtkEventControllerScroll *controller)
{
  GdkEvent *event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (controller));

  return event && (gdk_event_get_modifier_state (event) & GDK_BUTTON1_MASK) != 0;
}

static void
on_scroll_begin (GtkEventControllerScroll *controller,
                 gpointer                  user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  /* A sequence that never reported its end would leave the spinner hanging,
   * so retract it here rather than counting it towards this pull. */
  if (!self->claimed && self->pull_offset > 0.0)
    pull_reset (self);
}

static void
on_scroll_end (GtkEventControllerScroll *controller,
               gpointer                  user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  /* Taking the scrolled window out of a drag's way resets this controller,
   * which ends the scroll; that must not be mistaken for fingers lifting. */
  if (self->claimed || self->pull_offset <= 0.0)
    return;

  pull_release (self);
}

static gboolean
on_scroll (GtkEventControllerScroll *controller,
           gdouble                   dx,
           gdouble                   dy,
           gpointer                  user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));
  gdouble adjustment = gtk_adjustment_get_value (adj);

  if (adjustment > 200 && dy < 0) {
    gtk_widget_set_visible (self->scroll_to_top, TRUE);
  } else {
    gtk_widget_set_visible (self->scroll_to_top, FALSE);
  }

  /* A drag is already pulling, or about to; stay out of its way. */
  if (self->claimed || scroll_is_dragging (controller) || !scroll_is_pixel_precise (controller))
    return FALSE;

  /* Start pulling once the list has nowhere left to scroll, and keep
   * following the fingers back up until the pull is fully retracted. */
  if (self->pull_offset > 0.0 || (dy < 0.0 && list_is_at_top (self))) {
    pull_update (self, self->pull_offset - dy);

    /* Swallow the event while pulling, so the list itself stays put. */
    if (self->pull_offset > 0.0)
      return TRUE;

    pull_reset (self);
  }

  return FALSE;
}

/*
 * Picking a tile leaves the popover open, since nothing closes it the way
 * using a menu item would.
 */
static void
on_layout_selected (StampLayoutPicker *picker G_GNUC_UNUSED,
                    gpointer                  user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  gtk_popover_popdown (GTK_POPOVER (self->view_popover));
}

static void
stamp_conversation_list_class_init (StampConversationListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/mail/conversation-list/stamp-conversation-list.ui");

  object_class->get_property = stamp_conversation_list_get_property;
  object_class->set_property = stamp_conversation_list_set_property;
  object_class->dispose = stamp_conversation_list_dispose;

  g_type_ensure (STAMP_TYPE_LAYOUT_PICKER);

  gtk_widget_class_bind_template_child (widget_class, StampConversationList, listview);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, search_bar);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, search_entry);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, window_title);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, selection_bottom_bar);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, scroll_to_top);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, scrolled_window);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, header_stack);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, normal_headerbar);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, selection_headerbar);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, selection_label);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, spinner);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, sort_or_filter_button);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, mail_list_stack);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, sidebar_button);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, view_button);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, view_popover);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, sort_is_active);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, collapsed_starred_strip);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, context_menu_model);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, move_selection_button);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, trash_selection_button);
  gtk_widget_class_bind_template_child (widget_class, StampConversationList, mark_read_selection_button);

  gtk_widget_class_bind_template_callback (widget_class, on_mail_search_entry_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_setup_list_item);
  gtk_widget_class_bind_template_callback (widget_class, on_bind_list_item);
  gtk_widget_class_bind_template_callback (widget_class, on_unbind_list_item);
  gtk_widget_class_bind_template_callback (widget_class, on_cancel_selection_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_scroll_to_top);
  gtk_widget_class_bind_template_callback (widget_class, on_scroll);
  gtk_widget_class_bind_template_callback (widget_class, on_scroll_begin);
  gtk_widget_class_bind_template_callback (widget_class, on_scroll_end);
  gtk_widget_class_bind_template_callback (widget_class, on_select_all);
  gtk_widget_class_bind_template_callback (widget_class, on_unselect_all);
  gtk_widget_class_bind_template_callback (widget_class, on_key_pressed);
  gtk_widget_class_bind_template_callback (widget_class, on_layout_selected);

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

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);
}

static void
on_multi_selection_changed (GtkSelectionModel *model,
                            guint              pos,
                            guint              n_items,
                            gpointer           user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  guint n_selected = gtk_bitset_get_size (self->selected);
  gchar buf[64];

  g_snprintf (buf, sizeof buf, _("%u selected"), n_selected);

  adw_window_title_set_title (ADW_WINDOW_TITLE (self->selection_label), buf);
}

static void
on_filter_activate (GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  const gchar *value;

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

  queue_starred_recompute (self);
}

static void
on_sort_activate (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  const gchar *value;

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
  queue_starred_recompute (self);
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
  const gchar *value;

  if (!parameter)
    return;

  value = g_variant_get_string (parameter, NULL);

  item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));

  stamp_conversation_item_set_label (item, value, TRUE);
}

static void
update_actions (StampConversationList *self)
{
  /* GAction *action; */
  /* guint32 flags = 0; */
  /* guint32 flags = camel_message_info_get_flags (self->message_info); */
  /* gboolean flagged = (flags & CAMEL_MESSAGE_FLAGGED) != 0; */
  /* gboolean read = (flags & CAMEL_MESSAGE_SEEN) != 0; */

  /* return; */

  /* action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-unflag"); */
  /* g_simple_action_set_enabled (G_SIMPLE_ACTION (action), flagged); */
  /* action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-flag"); */
  /* g_simple_action_set_enabled (G_SIMPLE_ACTION (action), !flagged); */

  /* action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-unread"); */
  /* g_simple_action_set_enabled (G_SIMPLE_ACTION (action), read); */
  /* action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-read"); */
  /* g_simple_action_set_enabled (G_SIMPLE_ACTION (action), !read); */
}

static void
on_mark_read (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  stamp_conversation_list_mark_read (self, NULL);
}

static void
on_mark_unread (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  stamp_conversation_list_mark_unread (self, NULL);
}

static void
on_mark_starred (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *item = NULL;

  item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));

  stamp_conversation_item_set_flagged (item, TRUE);
  update_actions (self);
}

static void
on_mark_unstarred (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  StampConversationItem *item = NULL;

  item = STAMP_CONVERSATION_ITEM (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (self->single_selection)));

  stamp_conversation_item_set_flagged (item, FALSE);
  update_actions (self);
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
stamp_conversation_list_move_conversation (StampConversationList *self,
                                           StampConversationItem *item,
                                           const gchar           *target_full_name)
{
  StampMailService *mail_service;
  CamelStore *store;
  CamelFolder *target;
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) array = NULL;
  g_autoptr (GPtrArray) uid_array = NULL;

  g_return_if_fail (STAMP_IS_CONVERSATION_LIST (self));
  g_return_if_fail (STAMP_IS_CONVERSATION_ITEM (item));

  stamp_conversation_item_set_hidden (item, TRUE);

  array = collect_messages (stamp_conversation_item_get_node (item), NULL);

  g_list_model_items_changed (G_LIST_MODEL (self->list_store),
                              0,
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)),
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)));

  uid_array = g_ptr_array_new ();
  for (gint idx = array->len - 1; idx >= 0; idx--) {
    CamelFolderThreadNode *child_node = array->pdata[idx];
    const CamelMessageInfo *info = camel_folder_thread_node_get_item (child_node);

    g_ptr_array_add (uid_array, g_strdup (camel_message_info_get_uid (info)));
  }

  mail_service = stamp_account_get_mail_service (self->account);
  store = CAMEL_STORE (stamp_mail_service_get_service (mail_service));
  target = camel_store_get_folder_sync (store, target_full_name, 0, NULL, &error);
  if (error) {
    g_warning ("%s: Could not open target folder %s: %s", G_STRFUNC, target_full_name, error->message);
    return;
  }

  if (self->transfer_cancellable) {
    g_cancellable_cancel (self->transfer_cancellable);
    g_clear_object (&self->transfer_cancellable);
  }
  self->transfer_cancellable = g_cancellable_new ();

  camel_folder_transfer_messages_to (self->folder, uid_array, target, TRUE,
                                     G_PRIORITY_DEFAULT, self->transfer_cancellable,
                                     on_transfer_messages_to, self);

  g_object_unref (target);
}

static void
on_move_folder (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  const gchar *target_full_name = g_variant_get_string (parameter, NULL);
  StampMailService *mail_service;
  CamelStore *store;
  CamelFolder *target;
  g_autoptr (GError) error = NULL;
  g_autoptr (GPtrArray) node_array = g_ptr_array_new_with_free_func (g_object_unref);
  g_autoptr (GPtrArray) array = NULL;
  g_autoptr (GPtrArray) uid_array = NULL;

  if (self->selection_mode) {
    GtkBitset *selected_items = self->selected;
    GtkBitsetIter iter;
    guint32 pos;

    gtk_bitset_iter_init_first (&iter, selected_items, &pos);
    while (gtk_bitset_iter_is_valid (&iter)) {
      g_autoptr (StampConversationItem) item = STAMP_CONVERSATION_ITEM (
        g_list_model_get_item (G_LIST_MODEL (self->multi_selection), pos));

      g_ptr_array_add (node_array, g_object_ref (item));
      gtk_bitset_iter_next (&iter, &pos);
    }
  }

  for (gint idx = 0; idx < node_array->len; idx++) {
    StampConversationItem *item = g_ptr_array_index (node_array, idx);

    stamp_conversation_item_set_hidden (item, TRUE);
    array = collect_messages (stamp_conversation_item_get_node (item), array);
  }

  g_list_model_items_changed (G_LIST_MODEL (self->list_store),
                              0,
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)),
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)));

  if (!array) {
    g_warning ("%s: No array, abort", G_STRFUNC);
    return;
  }

  uid_array = g_ptr_array_new ();
  for (gint idx = array->len - 1; idx >= 0; idx--) {
    CamelFolderThreadNode *child_node = array->pdata[idx];
    const CamelMessageInfo *info = camel_folder_thread_node_get_item (child_node);

    g_ptr_array_add (uid_array, g_strdup (camel_message_info_get_uid (info)));
  }

  mail_service = stamp_account_get_mail_service (self->account);
  store = CAMEL_STORE (stamp_mail_service_get_service (mail_service));
  target = camel_store_get_folder_sync (store, target_full_name, 0, NULL, &error);
  if (error) {
    g_warning ("Could not open target folder %s: %s", target_full_name, error->message);
    return;
  }

  if (self->transfer_cancellable) {
    g_cancellable_cancel (self->transfer_cancellable);
    g_clear_object (&self->transfer_cancellable);
  }
  self->transfer_cancellable = g_cancellable_new ();

  camel_folder_transfer_messages_to (self->folder, uid_array, target, TRUE,
                                     G_PRIORITY_DEFAULT, self->transfer_cancellable,
                                     on_transfer_messages_to, self);

  gtk_bitset_remove_all (self->selected);
  update_selection_title (self);
  refresh_checkboxes (self);
  set_selection_active (self, FALSE);

  g_object_unref (target);
}

static void
on_trash_folder (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);
  g_autoptr (GPtrArray) array = NULL;
  g_autoptr (GPtrArray) uid_array = NULL;
  guint n_items;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));
  if (n_items == 0)
    return;

  for (guint i = 0; i < n_items; i++) {
    StampConversationItem *item = STAMP_CONVERSATION_ITEM (g_list_model_get_item (G_LIST_MODEL (self->list_store), i));
    CamelFolderThreadNode *node = stamp_conversation_item_get_node (item);

    array = collect_messages (node, array);
  }

  uid_array = g_ptr_array_new_with_free_func (g_free);

  for (guint i = 0; i < array->len; i++) {
    CamelFolderThreadNode *child_node = array->pdata[i];
    const CamelMessageInfo *info = camel_folder_thread_node_get_item (child_node);
    g_ptr_array_add (uid_array, g_strdup (camel_message_info_get_uid (info)));
  }

  if (uid_array->len > 0) {
    g_autoptr (GError) error = NULL;

    for (guint i = 0; i < uid_array->len; i++)
      camel_folder_set_message_flags (self->folder, uid_array->pdata[i], CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);

    camel_folder_expunge_sync (self->folder, NULL, &error);
    if (error) {
      g_warning ("%s: Could not expunge folder: %s", G_STRFUNC, error->message);
    }
  }
}

static void
on_trash (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
  StampConversationList *self = STAMP_CONVERSATION_LIST (user_data);

  stamp_conversation_list_trash (self, NULL);
}

static const GActionEntry stamp_conversation_list_action_entries[] = {
  { .name = "mark-category", .activate = on_mark_category, .parameter_type = "s" },
  { .name = "mark-read", .activate = on_mark_read },
  { .name = "mark-unread", .activate = on_mark_unread },
  { .name = "mark-starred", .activate = on_mark_starred },
  { .name = "mark-unstarred", .activate = on_mark_unstarred },
  { .name = "move-folder", .activate = on_move_folder, .parameter_type = "s" },
  { .name = "trash-folder", .activate = on_trash_folder },
  { .name = "trash", .activate = on_trash },
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

  g_signal_connect_object (all_mails_action, "activate", G_CALLBACK (on_filter_activate), self, G_CONNECT_DEFAULT);
  g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (all_mails_action));
  g_signal_connect_object (sort_action, "activate", G_CALLBACK (on_sort_activate), self, G_CONNECT_DEFAULT);
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
  gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (self->sort_or_filter_button), G_MENU_MODEL (menu));

  self->is_pulling = FALSE;

  self->folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  self->list_store = g_list_store_new (STAMP_TYPE_CONVERSATION_ITEM);

  /* Both the filter and the sorter consult the starred set, so it has to
   * exist before either of them can run. */
  self->promoted = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->starred_expanded = g_settings_get_boolean (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_STARRED_EXPANDED);

  self->filter = GTK_FILTER (gtk_custom_filter_new (filter_func, self, NULL));
  self->filter_model = gtk_filter_list_model_new (G_LIST_MODEL (self->list_store), self->filter);
  /* self->filter_model = gtk_filter_list_model_new (NULL, self->filter); */

  self->sorter = GTK_SORTER (gtk_custom_sorter_new (sorter_func, self, NULL));
  sort_model = gtk_sort_list_model_new (G_LIST_MODEL (self->filter_model), self->sorter);
  self->sort_model = sort_model;

  self->section_sorter = GTK_SORTER (gtk_custom_sorter_new (section_sorter_func, self, NULL));

  self->header_factory = gtk_signal_list_item_factory_new ();
  g_signal_connect_object (self->header_factory, "setup", G_CALLBACK (on_setup_section_header), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (self->header_factory, "bind", G_CALLBACK (on_bind_section_header), self, G_CONNECT_DEFAULT);

  g_signal_connect_object (STAMP_SETTINGS_MAIL, "changed::" STAMP_PREFS_MAIL_STARRED_FIRST,
                           G_CALLBACK (on_starred_settings_changed), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (STAMP_SETTINGS_MAIL, "changed::" STAMP_PREFS_MAIL_STARRED_LIMIT,
                           G_CALLBACK (on_starred_settings_changed), self, G_CONNECT_SWAPPED);
  /* Which starred mails are the top few depends on this ordering too. */
  g_signal_connect_object (STAMP_SETTINGS_MAIL, "changed::" STAMP_PREFS_MAIL_IMPORTANT_FIRST,
                           G_CALLBACK (on_important_first_changed), self, G_CONNECT_SWAPPED);

  g_signal_connect (G_LIST_MODEL (sort_model), "items-changed", G_CALLBACK (on_items_changed), self);

  rebuild_category_actions (self);

  self->single_selection = gtk_single_selection_new (g_object_ref (G_LIST_MODEL (sort_model)));
  g_signal_connect_object (self->single_selection, "selection-changed", G_CALLBACK (on_single_selection_changed), self, G_CONNECT_DEFAULT);
  gtk_list_view_set_model (GTK_LIST_VIEW (self->listview), GTK_SELECTION_MODEL (self->single_selection));

  gtk_single_selection_set_autoselect (self->single_selection, FALSE);
  gtk_single_selection_set_can_unselect (self->single_selection, TRUE);

  self->multi_selection = gtk_no_selection_new (g_object_ref (G_LIST_MODEL (sort_model)));
  g_signal_connect_object (self->multi_selection, "selection-changed", G_CALLBACK (on_multi_selection_changed), self, G_CONNECT_DEFAULT);

  gtk_search_bar_connect_entry (GTK_SEARCH_BAR (self->search_bar), GTK_EDITABLE (self->search_entry));

  /* Capture phase, so the pull is decided before the rows see the drag. */
  drag = gtk_gesture_drag_new ();
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (drag), FALSE);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (drag), GTK_PHASE_CAPTURE);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (drag));
  self->vadj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scrolled_window));

  g_signal_connect_object (drag, "drag-update", G_CALLBACK (on_drag_update), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (drag, "drag-end", G_CALLBACK (on_drag_end), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (drag, "cancel", G_CALLBACK (on_drag_cancel), self, G_CONNECT_DEFAULT);

  self->selected = gtk_bitset_new_empty ();
  self->anchor_position = 0;
  self->position_to_check = g_hash_table_new (g_direct_hash, g_direct_equal);
}

GtkWidget *
stamp_conversation_list_new (void)
{
  return g_object_new (STAMP_TYPE_CONVERSATION_LIST, NULL);
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

  for (gint idx = 0; idx < node_array->len; idx++) {
    CamelFolderThreadNode *child_node = node_array->pdata[idx];

    array = collect_messages (child_node, array);
  }

  if (!array) {
    g_warning ("%s: No array, abort", G_STRFUNC);
    return;
  }

  for (gint idx = array->len - 1; idx >= 0; idx--) {
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

  for (gint idx = 0; idx < node_array->len; idx++) {
    CamelFolderThreadNode *child_node = node_array->pdata[idx];

    array = collect_messages (child_node, array);
  }

  if (!array) {
    g_warning ("%s: No array, abort", G_STRFUNC);
    return;
  }

  for (gint idx = array->len - 1; idx >= 0; idx--) {
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

gboolean
stamp_conversation_list_trash (StampConversationList *self,
                               StampConversationItem *item)
{
  g_autoptr (GPtrArray) node_array = g_ptr_array_new ();
  g_autoptr (GPtrArray) array = NULL;
  g_autoptr (GPtrArray) uid_array = NULL;
  CamelFolder *folder;
  CamelFolder *trash_folder;

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
      return FALSE;

    g_ptr_array_add (node_array, item);
  }

  folder = self->folder;
  trash_folder = stamp_account_get_mail_trash_folder (self->account);

  if (folder == trash_folder || folder == stamp_account_get_mail_junk_folder (self->account)) {
    g_autoptr (GError) error = NULL;

    for (gint idx = 0; idx < node_array->len; idx++) {
      CamelFolderThreadNode *child_node = stamp_conversation_item_get_node (STAMP_CONVERSATION_ITEM (node_array->pdata[idx]));
      array = collect_messages (child_node, array);
    }

    if (self->transfer_cancellable) {
      g_cancellable_cancel (self->transfer_cancellable);
      g_clear_object (&self->transfer_cancellable);
    }

    if (!array) {
      g_warning ("%s: No array, abort", G_STRFUNC);
      return FALSE;
    }

    for (gint idx = array->len - 1; idx >= 0; idx--) {
      const CamelMessageInfo *info = camel_folder_thread_node_get_item (array->pdata[idx]);
      camel_folder_set_message_flags (folder, camel_message_info_get_uid (info), CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED);
    }

    camel_folder_synchronize_sync (folder, FALSE, NULL, &error);
    if (error)
      g_warning ("%s: Could not synchronize folder: %s", G_STRFUNC, error->message);

    for (gint idx = 0; idx < node_array->len; idx++)
      stamp_conversation_item_set_hidden (STAMP_CONVERSATION_ITEM (node_array->pdata[idx]), TRUE);

    g_list_model_items_changed (G_LIST_MODEL (self->list_store),
                                0,
                                g_list_model_get_n_items (G_LIST_MODEL (self->list_store)),
                                g_list_model_get_n_items (G_LIST_MODEL (self->list_store)));

    stamp_conversation_list_unselect (self);
    return FALSE;
  }

  if (self->transfer_cancellable) {
    g_cancellable_cancel (self->transfer_cancellable);
    g_clear_object (&self->transfer_cancellable);
  }

  g_clear_pointer (&self->trash_array, g_ptr_array_unref);
  g_clear_pointer (&self->trash_uids, g_ptr_array_unref);
  g_clear_object (&self->trash_source_folder);

  self->trash_array = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < node_array->len; i++)
    g_ptr_array_add (self->trash_array, g_object_ref (node_array->pdata[i]));

  for (gint idx = 0; idx < node_array->len; idx++) {
    StampConversationItem *child_item = STAMP_CONVERSATION_ITEM (node_array->pdata[idx]);
    CamelFolderThreadNode *child_node = stamp_conversation_item_get_node (child_item);

    stamp_conversation_item_set_hidden (child_item, TRUE);
    array = collect_messages (child_node, array);
  }

  g_list_model_items_changed (G_LIST_MODEL (self->list_store),
                              0,
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)),
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)));

  if (!array) {
    g_warning ("%s: No array, abort", G_STRFUNC);
    return FALSE;
  }

  uid_array = g_ptr_array_new_with_free_func (g_free);

  for (gint idx = array->len - 1; idx >= 0; idx--) {
    const CamelMessageInfo *info = camel_folder_thread_node_get_item (array->pdata[idx]);
    g_ptr_array_add (uid_array, g_strdup (camel_message_info_get_uid (info)));
  }

  self->trash_uids = g_ptr_array_new_with_free_func (g_free);
  for (guint idx_u = 0; idx_u < uid_array->len; idx_u++)
    g_ptr_array_add (self->trash_uids, g_strdup (g_ptr_array_index (uid_array, idx_u)));

  self->trash_source_folder = g_object_ref (folder);

  return TRUE;
}

void
stamp_conversation_list_finalize_trash (StampConversationList *self)
{
  CamelFolder *trash_folder;
  g_autoptr (GPtrArray) uid_array = NULL;

  if (!self->trash_array || !self->trash_uids)
    return;

  trash_folder = stamp_account_get_mail_trash_folder (self->account);
  if (!self->trash_source_folder || !trash_folder) {
    g_clear_pointer (&self->trash_array, g_ptr_array_unref);
    g_clear_pointer (&self->trash_uids, g_ptr_array_unref);
    g_clear_object (&self->trash_source_folder);
    return;
  }

  uid_array = g_ptr_array_new_with_free_func (g_free);
  for (guint i = 0; i < self->trash_uids->len; i++)
    g_ptr_array_add (uid_array, g_strdup (g_ptr_array_index (self->trash_uids, i)));

  for (gint idx = uid_array->len - 1; idx >= 0; idx--)
    camel_folder_set_message_flags (self->trash_source_folder,
                                    g_ptr_array_index (uid_array, idx),
                                    CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

  self->transfer_cancellable = g_cancellable_new ();
  camel_folder_transfer_messages_to (self->trash_source_folder, uid_array,
                                     trash_folder, TRUE,
                                     G_PRIORITY_DEFAULT, self->transfer_cancellable,
                                     on_transfer_messages_to, self);

  if (self->selection_mode)
    stamp_conversation_list_unselect (self);
  else
    stamp_conversation_list_select_relative (self, 1);

  g_clear_pointer (&self->trash_array, g_ptr_array_unref);
  g_clear_pointer (&self->trash_uids, g_ptr_array_unref);
  g_clear_object (&self->trash_source_folder);
}

void
stamp_mail_conversation_list_search_contact (StampConversationList *self,
                                             const gchar           *mail)
{
  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), mail);
  gtk_search_bar_set_search_mode (GTK_SEARCH_BAR (self->search_bar), TRUE);
}

void
stamp_conversation_list_undo_trash (StampConversationList *self)
{
  if (!self->trash_array)
    return;

  for (gint idx = 0; idx < self->trash_array->len; idx++) {
    StampConversationItem *item = STAMP_CONVERSATION_ITEM (self->trash_array->pdata[idx]);

    stamp_conversation_item_set_hidden (item, FALSE);
  }

  g_list_model_items_changed (G_LIST_MODEL (self->list_store),
                              0,
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)),
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)));

  g_clear_pointer (&self->trash_array, g_ptr_array_unref);
  g_clear_pointer (&self->trash_uids, g_ptr_array_unref);
  g_clear_object (&self->trash_source_folder);
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

/*
 * Which layout the mail view uses is only a choice where there is more
 * than one pane to arrange. A phone shows one per page, so the button
 * stands down there.
 */
void
stamp_conversation_list_set_show_view_button (StampConversationList *self,
                                              gboolean               show)
{
  g_return_if_fail (STAMP_IS_CONVERSATION_LIST (self));

  gtk_widget_set_visible (self->view_button, show);
}

/* Rows worth their padding beside the reading pane are worth less of it
 * above one, where the list only has a few rows' worth of height. */
void
stamp_conversation_list_set_compact (StampConversationList *self,
                                     gboolean               compact)
{
  if (compact)
    gtk_widget_add_css_class (GTK_WIDGET (self), "compact-rows");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (self), "compact-rows");
}

void
stamp_conversation_list_select_relative (StampConversationList *self,
                                         gint                   direction)
{
  GtkSelectionModel *model = GTK_SELECTION_MODEL (self->single_selection);
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (model));
  guint selected;

  if (self->selection_mode || n_items == 0)
    return;

  selected = gtk_single_selection_get_selected (self->single_selection);

  if (selected == GTK_INVALID_LIST_POSITION) {
    selected = (direction > 0) ? 0 : n_items - 1;
  } else {
    gint new_pos = (int)selected + direction;
    selected = CLAMP (new_pos, 0, (int)n_items - 1);
  }

  gtk_selection_model_select_item (model, selected, TRUE);
}

StampConversationItem *
stamp_conversation_list_get_adjacent_item (StampConversationList *self,
                                           gint                   offset)
{
  GListModel *model = G_LIST_MODEL (self->single_selection);
  guint n_items = g_list_model_get_n_items (model);
  guint selected;
  gint new_pos;

  if (n_items == 0)
    return NULL;

  selected = gtk_single_selection_get_selected (self->single_selection);

  if (selected == GTK_INVALID_LIST_POSITION)
    return NULL;

  new_pos = (int)selected + offset;

  if (new_pos < 0 || new_pos >= (int)n_items)
    return NULL;

  return g_list_model_get_item (model, new_pos);
}

void
stamp_conversation_list_junk (StampConversationList *self,
                              StampConversationItem *item)
{
  g_autoptr (GPtrArray) node_array = g_ptr_array_new ();
  g_autoptr (GPtrArray) array = NULL;
  CamelFolder *folder;
  CamelFolder *junk_folder;
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

  for (gint idx = 0; idx < node_array->len; idx++) {
    StampConversationItem *child_item = STAMP_CONVERSATION_ITEM (node_array->pdata[idx]);
    CamelFolderThreadNode *child_node = stamp_conversation_item_get_node (child_item);

    stamp_conversation_item_set_hidden (child_item, TRUE);
    array = collect_messages (child_node, array);
  }

  g_list_model_items_changed (G_LIST_MODEL (self->list_store),
                              0,
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)),
                              g_list_model_get_n_items (G_LIST_MODEL (self->list_store)));

  if (!array) {
    g_warning ("%s: No array, abort", G_STRFUNC);
    return;
  }

  folder = self->folder;
  junk_folder = stamp_account_get_mail_junk_folder (self->account);

  uid_array = g_ptr_array_new ();
  for (gint idx = array->len - 1; idx >= 0; idx--) {
    CamelFolderThreadNode *child_node = array->pdata[idx];
    const CamelMessageInfo *info;

    info = camel_folder_thread_node_get_item (child_node);
    g_ptr_array_add (uid_array, g_strdup (camel_message_info_get_uid (info)));
  }

  if (self->transfer_cancellable) {
    g_cancellable_cancel (self->transfer_cancellable);
    g_clear_object (&self->transfer_cancellable);
  }
  self->transfer_cancellable = g_cancellable_new ();

  camel_folder_transfer_messages_to (folder, uid_array, junk_folder, TRUE, G_PRIORITY_DEFAULT, self->transfer_cancellable, on_transfer_messages_to, self);

  stamp_conversation_list_unselect (self);
}
