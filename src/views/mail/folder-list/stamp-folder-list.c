/*
 * Copyright 2025-2026 Jan-Michael Brummer
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

#include "stamp-folder-list.h"

#include "stamp-account-item.h"
#include "stamp-conversation-item.h"
#include "stamp-conversation-list.h"
#include "stamp-folder-item.h"
#include "stamp-folder-row.h"
#include "stamp-helper.h"
#include "stamp-profile-manager.h"
#include "stamp-session.h"
#include "stamp-settings.h"

/* Width of one nesting level, matching the expander icon plus its spacing.
 * Folders are children of their account, so the depth is offset by one to
 * keep top level folders flush with the account they belong to. */
#define FOLDER_ROW_INDENT 20

struct _StampFolderList {
  AdwBin parent_instance;

  GtkCustomSorter *sorter;
  GtkSortListModel *sort_list_model;
  GtkSingleSelection *selection;

  gboolean already_selected;
  GListStore *list_store;
  GtkCustomFilter *account_filter;
  GPtrArray *expand_queue;
  guint expand_handler;
  guint restore_handler;

  /* The folder to land on while nothing is selected yet: the one this
   * profile was last reading, or the one from the last run. */
  gchar *restore_account;
  gchar *restore_folder;

  /* Rows collapse as the profile filter takes their account out of the
   * tree. That is the model tidying up, not the user folding anything,
   * so it must not be written down as a preference. */
  gboolean refiltering;

  StampConversationList *conversation_list;
};

G_DEFINE_FINAL_TYPE (StampFolderList, stamp_folder_list, ADW_TYPE_BIN);

enum {
  FOLDER_SELECTED,
  FOLDER_CLEARED,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

/* "" is the no-profile case, which is a profile as far as this is
 * concerned: it was reading something too. */
static const gchar *
active_profile_id (void)
{
  StampProfile *active = stamp_profile_manager_get_active (stamp_profile_manager_get_default ());

  return active ? stamp_profile_get_id (active) : "";
}

static void
store_profile_folder (StampAccount *account,
                      const gchar  *full_name)
{
  const gchar *id = active_profile_id ();
  g_autoptr (GVariant) stored = g_settings_get_value (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_PROFILE_FOLDERS);
  GVariantBuilder builder;
  GVariantIter iter;
  const gchar *key;
  GVariant *value;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{s(ss)}"));

  g_variant_iter_init (&iter, stored);
  while (g_variant_iter_next (&iter, "{&s@(ss)}", &key, &value)) {
    if (g_strcmp0 (key, id) != 0)
      g_variant_builder_add (&builder, "{s@(ss)}", key, value);

    g_variant_unref (value);
  }

  g_variant_builder_add (&builder, "{s(ss)}", id, stamp_account_get_name (account), full_name);

  g_settings_set_value (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_PROFILE_FOLDERS, g_variant_builder_end (&builder));
}

/*
 * Where the profile that just became active was last reading. Falls
 * back to the folder from the last run, which is all there is to go on
 * the first time a profile is used.
 */
static void
load_restore_target (StampFolderList *self)
{
  g_autoptr (GVariant) stored = g_settings_get_value (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_PROFILE_FOLDERS);
  const gchar *account_name = NULL;
  const gchar *folder_name = NULL;

  if (!g_variant_lookup (stored, active_profile_id (), "(&s&s)", &account_name, &folder_name)) {
    g_autofree gchar *saved_account = NULL;
    g_autofree gchar *saved_folder = NULL;

    g_settings_get (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_SELECTED_FOLDER, "(ss)", &saved_account, &saved_folder);
    g_set_str (&self->restore_account, saved_account);
    g_set_str (&self->restore_folder, saved_folder);
    return;
  }

  g_set_str (&self->restore_account, account_name);
  g_set_str (&self->restore_folder, folder_name);
}

static gboolean
is_restore_target (StampFolderList *self,
                   StampItem       *item)
{
  StampAccount *account;

  if (!STAMP_IS_FOLDER_ITEM (item) || !self->restore_folder)
    return FALSE;

  account = stamp_item_get_account (item);

  return g_strcmp0 (stamp_account_get_name (account), self->restore_account) == 0 &&
         g_strcmp0 (stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (item)), self->restore_folder) == 0;
}

static void
on_selection_changed (GtkSelectionModel *selection,
                      guint              position,
                      guint              n_items,
                      gpointer           user_data)
{
  gpointer selected_item = gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (selection));
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  GtkTreeListRow *row;
  StampItem *item;

  /* Whatever the model settles on while it is being refiltered is not a
   * choice anyone made, and must not be recorded as one. */
  if (!selected_item || self->refiltering)
    return;

  row = GTK_TREE_LIST_ROW (selected_item);
  item = STAMP_ITEM (gtk_tree_list_row_get_item (row));

  if (STAMP_IS_FOLDER_ITEM (item)) {
    StampAccount *account = stamp_item_get_account (item);
    const gchar *full_name = stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (item));

    g_settings_set (STAMP_SETTINGS_MAIL, STAMP_PREFS_MAIL_SELECTED_FOLDER, "(ss)", stamp_account_get_name (account), full_name);
    store_profile_folder (account, full_name);

    g_set_str (&self->restore_account, stamp_account_get_name (account));
    g_set_str (&self->restore_folder, full_name);

    g_signal_emit (self, signals[FOLDER_SELECTED], 0, account, full_name);
  }
}

static void
insert_item (StampFolderList  *self,
             StampAccountItem *item)
{
  gchar **saved_order = g_settings_get_strv (STAMP_SETTINGS, STAMP_PREFS_ACCOUNT_ORDER);
  gint pos = -1;
  gint saved_pos = 0;
  const gchar *name = stamp_item_get_name (STAMP_ITEM (item));

  while (saved_order[saved_pos]) {
    if (g_strcmp0 (name, saved_order[saved_pos]) == 0) {
      pos = saved_pos;
      break;
    }
    saved_pos++;
  }

  if (pos == -1) {
    g_list_store_append (self->list_store, item);
  } else {
    gint insert_index = 0;
    guint list_len = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));

    for (guint idx = 0; idx < list_len; idx++) {
      StampItem *cur_item = g_list_model_get_item (G_LIST_MODEL (self->list_store), idx);
      const gchar *cur_name;

      if (!STAMP_IS_ACCOUNT_ITEM (cur_item))
        continue;

      cur_name = stamp_item_get_name (cur_item);

      for (gint j = 0; j < pos; j++)
        if (g_strcmp0 (saved_order[j], cur_name) == 0)
          insert_index++;
    }

    g_list_store_insert (self->list_store, insert_index, item);
  }
}

static void
on_mail_added (GtkWidget        *object,
               StampAccount     *account,
               StampMailService *service,
               gpointer          user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  g_autoptr (StampAccountItem) item = NULL;
  guint len = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));

  /* Safety: Do not account twice, shouldn't happen at all... */
  for (guint idx = 0; idx < len; idx++) {
    g_autoptr (StampAccountItem) check_item = STAMP_ACCOUNT_ITEM (g_list_model_get_item (G_LIST_MODEL (self->list_store), idx));

    if (stamp_item_get_account (STAMP_ITEM (check_item)) == account) {
      return;
    }
  }

  item = stamp_account_item_new (account);
  stamp_account_item_load (item);
  insert_item (self, item);
}

static void
on_mail_removed (GtkWidget        *object,
                 StampAccount     *account,
                 StampMailService *service,
                 gpointer          user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  guint len = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));

  for (guint idx = 0; idx < len; idx++) {
    g_autoptr (StampAccountItem) item = STAMP_ACCOUNT_ITEM (g_list_model_get_item (G_LIST_MODEL (self->list_store), idx));

    if (stamp_item_get_account (STAMP_ITEM (item)) == account) {
      g_list_store_remove (self->list_store, idx);
      return;
    }
  }
}

static void
on_stamp_folder_list_account_added (GObject      *object,
                                    StampAccount *account,
                                    gpointer      user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  g_autoptr (StampAccountItem) account_item = NULL;
  StampMailService *mail_service;

  g_signal_connect_object (account, "mail-added", G_CALLBACK (on_mail_added), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (account, "mail-removed", G_CALLBACK (on_mail_removed), self, G_CONNECT_DEFAULT);

  mail_service = stamp_account_get_mail_service (account);
  if (!mail_service || !stamp_mail_service_get_enabled (mail_service))
    return;

  account_item = stamp_account_item_new (account);
  stamp_account_item_load (account_item);
  insert_item (self, account_item);
}

static void
on_stamp_folder_list_account_removed (GObject      *object,
                                      StampAccount *account,
                                      gpointer      user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  gint len = g_list_model_get_n_items (G_LIST_MODEL (self->list_store));

  for (gint idx = 0; idx < len; idx++) {
    g_autoptr (StampItem) item = STAMP_ITEM (g_list_model_get_item (G_LIST_MODEL (self->list_store), idx));

    if (!STAMP_IS_ACCOUNT_ITEM (item))
      continue;

    if (stamp_item_get_account (item) == account) {
      g_list_store_remove (self->list_store, idx);
      break;
    }
  }
}

static void
on_row_expanded (GtkTreeListRow *row,
                 GParamSpec     *pspec,
                 gpointer        user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  g_autoptr (StampItem) item = STAMP_ITEM (gtk_tree_list_row_get_item (row));
  StampAccount *account;
  g_autofree char *settings_path = NULL;
  g_autoptr (GSettings) account_settings = NULL;
  g_auto (GStrv) folders = NULL;
  g_auto (GStrv) new_folders = NULL;
  const gchar *full_name;

  /* A row whose account the filter just dropped reports itself
   * collapsed on the way out, and a row already torn down has no item
   * left to ask. Neither is the user folding anything away. */
  if (self->refiltering || !item)
    return;

  account = stamp_item_get_account (item);
  full_name = stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (item));

  settings_path = g_strconcat ("/io/github/steeb_k/Post/mail/accounts/", stamp_account_get_uid (account), "/", NULL);
  account_settings = g_settings_new_with_path ("io.github.steeb_k.Post.mail.accounts", settings_path);
  folders = g_settings_get_strv (account_settings, "expanded-folders");

  if (gtk_tree_list_row_get_expanded (row)) {
    new_folders = g_strv_append ((const char * const *)folders, full_name);
  } else {
    new_folders = g_strv_remove ((const char * const *)folders, full_name);
  }

  g_settings_set_strv (account_settings, "expanded-folders", (const char * const *)new_folders);
}

static void
on_account_row_expanded (GtkTreeListRow *row,
                         GParamSpec     *pspec,
                         gpointer        user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  g_autoptr (StampItem) item = STAMP_ITEM (gtk_tree_list_row_get_item (row));
  g_autofree char *settings_path = NULL;
  g_autoptr (GSettings) account_settings = NULL;
  StampAccount *account;

  if (self->refiltering || !item)
    return;

  account = stamp_item_get_account (item);
  settings_path = g_strconcat ("/io/github/steeb_k/Post/mail/accounts/", stamp_account_get_uid (account), "/", NULL);
  account_settings = g_settings_new_with_path ("io.github.steeb_k.Post.mail.accounts", settings_path);

  g_settings_set_boolean (account_settings, "expanded", gtk_tree_list_row_get_expanded (row));
}

static void
expand_idle (gpointer user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);

  for (guint i = 0; i < self->expand_queue->len; i++)
    gtk_tree_list_row_set_expanded (self->expand_queue->pdata[i], TRUE);

  g_ptr_array_set_size (self->expand_queue, 0);
  self->expand_handler = 0;
}

static gboolean
item_is_inbox (StampItem *item)
{
  const gchar *full_name;

  if (!STAMP_IS_FOLDER_ITEM (item))
    return FALSE;

  full_name = stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (item));

  return g_strcmp0 (full_name, "INBOX") == 0 ||
         g_strcmp0 (full_name, "Inbox") == 0 ||
         g_strcmp0 (full_name, "Posteingang") == 0;
}

/*
 * A folder inside a collapsed account has no row to select, so the tree
 * has nothing to offer and the mail list stays empty. Open the account
 * the remembered folder belongs to -- only that one, so an account
 * deliberately folded away stays folded.
 */
static void
expand_restore_account (StampFolderList *self)
{
  GListModel *model = G_LIST_MODEL (self->selection);
  guint n_items = g_list_model_get_n_items (model);

  if (!self->restore_account)
    return;

  for (guint i = 0; i < n_items; i++) {
    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (model, i);
    g_autoptr (StampItem) item = row ? STAMP_ITEM (gtk_tree_list_row_get_item (row)) : NULL;

    if (!item || STAMP_IS_FOLDER_ITEM (item))
      continue;

    if (g_strcmp0 (stamp_account_get_name (stamp_item_get_account (item)), self->restore_account) == 0)
      gtk_tree_list_row_set_expanded (row, TRUE);
  }
}

/*
 * Land somewhere: the folder this profile was last reading if it is on
 * screen, and otherwise the first inbox, because starting on an empty
 * mail list is a poor first impression -- and after a profile switch it
 * reads as a broken window rather than a different view.
 */
static void
restore_selection_idle (gpointer user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  GListModel *model = G_LIST_MODEL (self->selection);
  guint n_items;

  self->restore_handler = 0;

  if (self->already_selected)
    return;

  expand_restore_account (self);

  n_items = g_list_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++) {
    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (model, i);
    g_autoptr (StampItem) item = row ? STAMP_ITEM (gtk_tree_list_row_get_item (row)) : NULL;

    if (item && is_restore_target (self, item)) {
      gtk_single_selection_set_selected (self->selection, i);
      self->already_selected = TRUE;
      return;
    }
  }

  for (guint i = 0; i < n_items; i++) {
    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (model, i);
    g_autoptr (StampItem) item = row ? STAMP_ITEM (gtk_tree_list_row_get_item (row)) : NULL;

    if (item && item_is_inbox (item)) {
      gtk_single_selection_set_selected (self->selection, i);
      self->already_selected = TRUE;
      return;
    }
  }
}

static void
schedule_restore (StampFolderList *self)
{
  if (self->restore_handler == 0)
    self->restore_handler = g_idle_add_once (restore_selection_idle, self);
}

static void
on_bind_folder (GtkListItemFactory *factory,
                GtkListItem        *list_item,
                gpointer            user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  StampFolderRow *folder_row;
  StampItem *item;
  StampAccount *account;
  GtkTreeListRow *row;
  GtkWidget *expander;
  guint depth;
  g_autoptr (GSettings) account_settings = NULL;
  g_autofree char *settings_path = NULL;

  expander = gtk_list_item_get_child (list_item);
  row = GTK_TREE_LIST_ROW (gtk_list_item_get_item (list_item));
  item = STAMP_ITEM (gtk_tree_list_row_get_item (row));

  gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), row);

  depth = gtk_tree_list_row_get_depth (row);
  gtk_widget_set_margin_start (expander, depth > 0 ? (depth - 1) * FOLDER_ROW_INDENT : 0);

  folder_row = STAMP_FOLDER_ROW (gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander)));

  stamp_folder_row_bind (folder_row, item);

  /* FIXME: Optimize the following code */
  account = stamp_item_get_account (STAMP_ITEM (item));

  settings_path = g_strconcat ("/io/github/steeb_k/Post/mail/accounts/", stamp_account_get_uid (account), "/", NULL);
  account_settings = g_settings_new_with_path ("io.github.steeb_k.Post.mail.accounts", settings_path);

  if (STAMP_IS_FOLDER_ITEM (item)) {
    const gchar *full_name = stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (item));
    g_auto (GStrv) expanded_folders = g_settings_get_strv (account_settings, "expanded-folders");

    if (!self->already_selected) {
      if (is_restore_target (self, item)) {
        gtk_single_selection_set_selected (self->selection, gtk_list_item_get_position (list_item));
        self->already_selected = TRUE;
      } else {
        /* Also covers a saved folder whose account is gone. */
        schedule_restore (self);
      }
    }

    if (g_strv_contains ((const char **)expanded_folders, full_name)) {
      g_ptr_array_add (self->expand_queue, g_object_ref (row));

      if (self->expand_handler == 0)
        self->expand_handler = g_idle_add_once (expand_idle, self);
    }

    g_signal_connect_object (row, "notify::expanded", G_CALLBACK (on_row_expanded), self, G_CONNECT_DEFAULT);
  } else {
    /* Restore the account's own state, and write it back by hand. A
     * two-way g_settings_bind() here would record the collapse the
     * model performs while filtering a profile's accounts out of the
     * tree, so every account came back collapsed.
     *
     * Expanding waits for an idle, as the folders above do. Expanding a
     * row inserts its children into the model, and doing that from
     * inside a bind -- while the list view is walking that very model
     * -- leaves the row looking expanded with nothing under it until it
     * is toggled by hand. */
    if (g_settings_get_boolean (account_settings, "expanded")) {
      g_ptr_array_add (self->expand_queue, g_object_ref (row));

      if (self->expand_handler == 0)
        self->expand_handler = g_idle_add_once (expand_idle, self);
    }

    g_signal_connect_object (row, "notify::expanded", G_CALLBACK (on_account_row_expanded), self, G_CONNECT_DEFAULT);

    /* With every account collapsed there are no folder rows to trigger
     * the restore, and the mail list would sit empty. */
    if (!self->already_selected)
      schedule_restore (self);
  }
}

/* Only the accounts sit at the root of the tree, so filtering the root
 * takes their folders with them. */
static gboolean
account_is_in_profile (gpointer item,
                       gpointer user_data)
{
  StampAccount *account;

  if (!STAMP_IS_ITEM (item))
    return TRUE;

  account = stamp_item_get_account (STAMP_ITEM (item));
  if (!account)
    return TRUE;

  return stamp_profile_shows_account (stamp_account_get_uid (account));
}

static void
on_profile_changed (StampProfileManager *manager,
                    gpointer             user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  GtkTreeListRow *selected;

  /* Each profile remembers its own folder, so switching to one goes
   * back to what it was reading rather than to whatever the profile
   * before it happened to leave open. Read it before the tree churns,
   * so nothing the churn selects can overwrite it first. */
  load_restore_target (self);

  /* Accounts leaving and rejoining the tree collapse on the way, which
   * is the model's doing and must not be saved as the user's choice. */
  self->refiltering = TRUE;
  gtk_filter_changed (GTK_FILTER (self->account_filter), GTK_FILTER_CHANGE_DIFFERENT);
  self->refiltering = FALSE;

  selected = GTK_TREE_LIST_ROW (gtk_single_selection_get_selected_item (self->selection));

  if (selected) {
    g_autoptr (StampItem) item = STAMP_ITEM (gtk_tree_list_row_get_item (selected));

    /* Already on this profile's folder, or on one it shows and has no
     * memory of: either way there is nothing worth disturbing. */
    if (item && (is_restore_target (self, item) || !self->restore_folder))
      return;
  }

  /* Whatever is on screen belongs to the profile before this one, and
   * may be from an account this one hides. Empty it, then land. */
  self->already_selected = FALSE;
  g_signal_emit (self, signals[FOLDER_CLEARED], 0);
  schedule_restore (self);
}

static GListModel *
get_child (void     *item,
           gpointer  user_data)
{
  if (STAMP_IS_ITEM (item)) {
    GListModel *model = G_LIST_MODEL (stamp_item_get_list_store (STAMP_ITEM (item)));

    return model ? g_object_ref (model) : NULL;
  }

  return NULL;
}

static gint
folders_sorter (gconstpointer a,
                gconstpointer b,
                gpointer      user_data)
{
  StampItem *item_a = (StampItem *)(a);
  StampItem *item_b = (StampItem *)(b);

  if (STAMP_IS_FOLDER_ITEM (item_a) && STAMP_IS_FOLDER_ITEM (item_b)) {
    gint flags_a = stamp_folder_item_get_flags (STAMP_FOLDER_ITEM (item_a));
    gint flags_b = stamp_folder_item_get_flags (STAMP_FOLDER_ITEM (item_b));
    gint type_a = flags_a & CAMEL_FOLDER_TYPE_MASK;
    gint type_b = flags_b & CAMEL_FOLDER_TYPE_MASK;

    if (type_a == type_b) {
      const gchar *name_a = stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (item_a));
      const gchar *name_b = stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (item_b));

      return g_strcmp0 (name_a, name_b);
    }

    if (type_a == CAMEL_FOLDER_TYPE_NORMAL)
      return 1;

    if (type_b == CAMEL_FOLDER_TYPE_NORMAL)
      return -1;

    if (type_a < type_b)
      return -1;

    return 1;
  } else if (STAMP_IS_ACCOUNT_ITEM (item_a) && STAMP_IS_ACCOUNT_ITEM (item_b)) {
    /* const char *name_a = stamp_item_get_name (item_a); */
    /* const char *name_b = stamp_item_get_name (item_b); */

    /* return g_strcmp0 (name_a, name_b); */
    return 0;
  } else {
    g_debug ("%s: Failed to compare unknown combination", G_STRFUNC);
  }

  return 0;
}

static void
on_row_released (GtkGestureClick *gesture,
                 gint             n_press,
                 gdouble          x,
                 gdouble          y,
                 gpointer         user_data)
{
  GtkListItem *list_item = GTK_LIST_ITEM (user_data);
  GtkTreeListRow *row = gtk_list_item_get_item (list_item);
  StampItem *item;

  if (!row)
    return;

  item = STAMP_ITEM (gtk_tree_list_row_get_item (row));
  if (!STAMP_IS_ACCOUNT_ITEM (item))
    return;

  if (gtk_tree_list_row_is_expandable (row)) {
    gboolean expanded = gtk_tree_list_row_get_expanded (row);
    gtk_tree_list_row_set_expanded (row, !expanded);
  }
}

static GdkContentProvider *
on_drag_prepare (GtkDragSource *source,
                 gdouble        x,
                 gdouble        y,
                 gpointer       user_data)
{
  GtkListItem *list_item = GTK_LIST_ITEM (user_data);
  GtkTreeExpander *expander = GTK_TREE_EXPANDER (gtk_list_item_get_child (list_item));
  StampItem *item = STAMP_ITEM (gtk_tree_expander_get_item (expander));

  if (STAMP_IS_ACCOUNT_ITEM (item)) {
    g_type_ensure (STAMP_TYPE_ITEM);
    return gdk_content_provider_new_typed (STAMP_TYPE_ITEM, item);
  }

  return NULL;
}

static guint
find_position (GListStore *store,
               gpointer    item)
{
  guint n = g_list_model_get_n_items (G_LIST_MODEL (store));

  for (guint i = 0; i < n; i++) {
    gpointer it = g_list_model_get_item (G_LIST_MODEL (store), i);

    if (it == item) {
      g_object_unref (it);
      return i;
    }

    g_object_unref (it);
  }

  return GTK_INVALID_LIST_POSITION;
}

static void
save_account_order (StampFolderList *self)
{
  g_autoptr (GSettings) settings = g_settings_new ("io.github.steeb_k.Post");
  GListStore *store = self->list_store;
  guint n = g_list_model_get_n_items (G_LIST_MODEL (store));
  g_auto (GStrv) arr = g_new (gchar *, n + 1);
  gint array_index = 0;

  for (guint i = 0; i < n; i++) {
    gpointer it = g_list_model_get_item (G_LIST_MODEL (store), i);

    if (STAMP_IS_ACCOUNT_ITEM (it)) {
      arr[array_index++] = g_strdup (stamp_item_get_name (STAMP_ITEM (it)));
    }

    g_object_unref (it);
  }

  arr[array_index] = NULL;
  g_settings_set_strv (settings, "account-order", (const gchar * const *)arr);
}

static gboolean
on_drop (GtkDropTarget *target,
         const GValue  *value,
         gdouble        x,
         gdouble        y,
         gpointer       user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  GListStore *store = G_LIST_STORE (self->list_store);
  StampItem *src_item = g_value_get_object (value);
  StampFolderRow *row = STAMP_FOLDER_ROW (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (target)));
  GtkListItem *list_item = g_object_get_data (G_OBJECT (row), "list-item");
  GtkTreeExpander *expander = GTK_TREE_EXPANDER (gtk_list_item_get_child (list_item));
  StampItem *dest_item = STAMP_ITEM (gtk_tree_expander_get_item (expander));
  guint src_pos;
  guint dest_pos;
  gpointer item;

  if (!STAMP_IS_ACCOUNT_ITEM (dest_item))
    return FALSE;

  src_pos = find_position (store, src_item);
  dest_pos = find_position (store, dest_item);

  if (src_pos == GTK_INVALID_LIST_POSITION ||
      dest_pos == GTK_INVALID_LIST_POSITION)
    return FALSE;

  item = g_list_model_get_item (G_LIST_MODEL (store), src_pos);

  g_list_store_remove (store, src_pos);
  g_list_store_insert (store, dest_pos, item);

  g_object_unref (item);

  save_account_order (self);

  return TRUE;
}

static gboolean
on_accept (GtkDropTarget *self,
           GdkDrop       *drop,
           gpointer       user_data)
{
  GtkListItem *list_item = GTK_LIST_ITEM (user_data);
  GtkTreeExpander *expander = GTK_TREE_EXPANDER (gtk_list_item_get_child (list_item));
  StampItem *item = STAMP_ITEM (gtk_tree_expander_get_item (expander));

  if (STAMP_IS_ACCOUNT_ITEM (item))
    return TRUE;

  return FALSE;
}

static void
on_drag_begin (GtkDragSource  *source,
               GdkDrag        *drag,
               StampFolderRow *self)
{
  /* Set the widget as the drag icon */
  g_autoptr (GdkPaintable) paintable = gtk_widget_paintable_new (GTK_WIDGET (self));

  gtk_drag_source_set_icon (source, paintable, 0, 0);
}

static gboolean
on_conversation_accept (GtkDropTarget *target,
                        GdkDrop       *drop,
                        gpointer       user_data)
{
  GtkListItem *list_item = GTK_LIST_ITEM (user_data);
  GtkTreeExpander *expander = GTK_TREE_EXPANDER (gtk_list_item_get_child (list_item));
  StampItem *item = STAMP_ITEM (gtk_tree_expander_get_item (expander));

  return STAMP_IS_FOLDER_ITEM (item);
}

static gboolean
on_conversation_drop (GtkDropTarget *target,
                      const GValue  *value,
                      gdouble        x,
                      gdouble        y,
                      gpointer       user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  StampConversationItem *conv_item = g_value_get_object (value);
  StampFolderRow *row = STAMP_FOLDER_ROW (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (target)));
  GtkListItem *list_item = g_object_get_data (G_OBJECT (row), "list-item");
  GtkTreeExpander *expander = GTK_TREE_EXPANDER (gtk_list_item_get_child (list_item));
  StampItem *folder_item = STAMP_ITEM (gtk_tree_expander_get_item (expander));

  if (!STAMP_IS_FOLDER_ITEM (folder_item) || !self->conversation_list)
    return FALSE;

  stamp_conversation_list_move_conversation (self->conversation_list,
                                             conv_item,
                                             stamp_folder_item_get_full_name (STAMP_FOLDER_ITEM (folder_item)));

  return TRUE;
}

static void
on_setup_folder (GtkListItemFactory *factory,
                 GtkListItem        *list_item,
                 gpointer            user_data)
{
  StampFolderList *self = STAMP_FOLDER_LIST (user_data);
  GtkWidget *row;
  GtkWidget *expander;
  GtkGesture *click = gtk_gesture_click_new ();
  GtkDragSource *drag = gtk_drag_source_new ();
  GtkDropTarget *drop = gtk_drop_target_new (STAMP_TYPE_ITEM, GDK_ACTION_MOVE);
  GtkDropTarget *conv_drop = gtk_drop_target_new (STAMP_TYPE_CONVERSATION_ITEM, GDK_ACTION_MOVE);

  gtk_list_item_set_activatable (list_item, TRUE);

  expander = gtk_tree_expander_new ();
  gtk_tree_expander_set_indent_for_depth (GTK_TREE_EXPANDER (expander), FALSE);
  row = stamp_folder_row_new ();
  gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), row);

  gtk_list_item_set_child (list_item, expander);

  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_PRIMARY);
  g_signal_connect (click, "released", G_CALLBACK (on_row_released), list_item);
  gtk_widget_add_controller (expander, GTK_EVENT_CONTROLLER (click));

  gtk_drag_source_set_actions (drag, GDK_ACTION_MOVE);
  g_signal_connect (drag, "prepare", G_CALLBACK (on_drag_prepare), list_item);
  g_signal_connect (drag, "drag-begin", G_CALLBACK (on_drag_begin), row);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (drag));

  g_object_set_data (G_OBJECT (row), "list-item", list_item);
  g_signal_connect (drop, "accept", G_CALLBACK (on_accept), list_item);
  g_signal_connect (drop, "drop", G_CALLBACK (on_drop), self);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (drop));

  g_signal_connect (conv_drop, "accept", G_CALLBACK (on_conversation_accept), list_item);
  g_signal_connect (conv_drop, "drop", G_CALLBACK (on_conversation_drop), self);
  gtk_widget_add_controller (row, GTK_EVENT_CONTROLLER (conv_drop));
}

void
stamp_folder_list_set_conversation_list (StampFolderList       *self,
                                         StampConversationList *conversation_list)
{
  g_return_if_fail (STAMP_IS_FOLDER_LIST (self));

  self->conversation_list = conversation_list;
}

static void
stamp_folder_list_dispose (GObject *object)
{
  StampFolderList *self = STAMP_FOLDER_LIST (object);

  g_clear_object (&self->list_store);
  g_clear_object (&self->account_filter);

  g_clear_handle_id (&self->expand_handler, g_source_remove);
  g_clear_handle_id (&self->restore_handler, g_source_remove);
  g_clear_pointer (&self->restore_account, g_free);
  g_clear_pointer (&self->restore_folder, g_free);
  g_clear_pointer (&self->expand_queue, g_ptr_array_unref);

  self->conversation_list = NULL;

  G_OBJECT_CLASS (stamp_folder_list_parent_class)->dispose (object);
}

static void
stamp_folder_list_class_init (StampFolderListClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/steeb_k/Post/views/mail/folder-list/stamp-folder-list.ui");

  object_class->dispose = stamp_folder_list_dispose;

  gtk_widget_class_bind_template_child (widget_class, StampFolderList, selection);
  gtk_widget_class_bind_template_child (widget_class, StampFolderList, sorter);
  gtk_widget_class_bind_template_child (widget_class, StampFolderList, sort_list_model);

  gtk_widget_class_bind_template_callback (widget_class, on_selection_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_setup_folder);
  gtk_widget_class_bind_template_callback (widget_class, on_bind_folder);

  signals[FOLDER_SELECTED] = g_signal_new ("folder-selected", G_OBJECT_CLASS_TYPE (klass),
                                           G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                           0, NULL, NULL, NULL,
                                           G_TYPE_NONE,
                                           2, G_TYPE_OBJECT, G_TYPE_STRING);

  /* The selected folder went out of view because the active profile
   * stopped showing its account. */
  signals[FOLDER_CLEARED] = g_signal_new ("folder-cleared", G_OBJECT_CLASS_TYPE (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0, NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);
}

static void
stamp_folder_list_init (StampFolderList *self)
{
  StampSession *session = NULL;
  StampProfileManager *profiles = stamp_profile_manager_get_default ();
  GtkTreeListModel *tree;
  GtkFilterListModel *filtered;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->list_store = g_list_store_new (STAMP_TYPE_ITEM);
  self->account_filter = gtk_custom_filter_new (account_is_in_profile, NULL, NULL);

  filtered = gtk_filter_list_model_new (g_object_ref (G_LIST_MODEL (self->list_store)),
                                        GTK_FILTER (g_object_ref (self->account_filter)));
  tree = gtk_tree_list_model_new (G_LIST_MODEL (filtered), FALSE, FALSE, get_child, g_object_ref (self), g_object_unref);

  gtk_sort_list_model_set_model (GTK_SORT_LIST_MODEL (self->sort_list_model), G_LIST_MODEL (tree));
  gtk_custom_sorter_set_sort_func (self->sorter, folders_sorter, NULL, NULL);

  session = stamp_session_get_default ();
  g_signal_connect_object (session, "account-added", G_CALLBACK (on_stamp_folder_list_account_added), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (session, "account-removed", G_CALLBACK (on_stamp_folder_list_account_removed), self, G_CONNECT_DEFAULT);

  g_signal_connect_object (profiles, "changed", G_CALLBACK (on_profile_changed), self, G_CONNECT_DEFAULT);

  self->expand_queue = g_ptr_array_new_with_free_func (g_object_unref);

  /* Nothing has been selected yet, so the folder to look for is the one
   * the active profile was reading when the app last ran. */
  load_restore_target (self);

  g_type_ensure (STAMP_TYPE_ITEM);
}

GtkWidget *
stamp_folder_list_new (void)
{
  return g_object_new (STAMP_TYPE_FOLDER_LIST, NULL);
}

void
stamp_folder_list_unselect (StampFolderList *self)
{
  gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (self->selection));
}
