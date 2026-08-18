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

#include "stamp-account-item.h"

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>

#include "stamp-account.h"
#include "stamp-folder-item.h"
#include "stamp-item.h"
#include "stamp-session.h"

struct _StampAccountItem {
  StampItem parent_instance;

  GCancellable *cancellable;
  CamelOfflineStore *offline_store;

  /* Camel work is handed this one, never the item's own: a connection
   * that has gone stale has to be abandoned mid-flight without taking
   * the item down with it. Replaced after every such reset. */
  GCancellable *op_cancellable;

  GSettings *mail_settings;

  GQueue *refresh_queue;
  gboolean refresh_queue_running;
  gboolean first_refresh;
  guint refresh_folder_handler;
  guint refresh_timer_handler;
  guint refresh_watchdog_handler;
  guint connect_retry_handler;
  gint connect_retries;

  /* The folder being refreshed right now, held for as long as the call
   * is out so the watchdog has something to clear if it never returns. */
  StampFolderItem *refreshing_item;

  /* Raised by camel whenever an operation reports progress, and cleared
   * by the watchdog reading it. Atomic because camel is free to report
   * from whichever thread the work is running on. */
  gint refresh_activity;

  /* Last state seen from the network monitor, so a reconnect can tell a
   * network that has just come back from one that never went away. */
  gboolean network_available;

  gboolean disposed;

  /* Set when the account started offline, so the tree on screen came
   * from the cache and still has to be reconciled with the server once
   * the connection is up. Cleared by that reconcile, which is also what
   * keeps it from running again on every reconnect. */
  gboolean reconcile_pending;
};

G_DEFINE_FINAL_TYPE (StampAccountItem, stamp_account_item, STAMP_TYPE_ITEM);

static void
stamp_account_item_connect_to_account (StampAccountItem *self);
static void
stamp_account_item_reset_connection (StampAccountItem *self);
static void
start_refresh (StampAccountItem *self);
static void
schedule_connect_retry (StampAccountItem *self);
static void
on_refresh (GObject      *source,
            GAsyncResult *result,
            gpointer      user_data);

enum {
  ACCOUNT_ITEM_CHANGED,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

/* Refreshing a big folder over a slow link is allowed to take a while;
 * going quiet for this long with nothing to show is not. TCP will sit on
 * a connection whose route has gone away for many minutes before it
 * gives up, and until it does, every folder in the account is stuck
 * behind it. */
#define REFRESH_TIMEOUT_SECONDS 120

static void
on_operation_status (CamelOperation *operation,
                     const gchar    *what,
                     gint            percent,
                     gpointer        user_data)
{
  StampAccountItem *self = user_data;

  g_atomic_int_set (&self->refresh_activity, TRUE);
}

/* A CamelOperation rather than a plain GCancellable: it is a cancellable
 * that also reports what the server is doing, which is what lets the
 * watchdog below tell a slow connection from a dead one. */
static GCancellable *
op_cancellable_for (StampAccountItem *self)
{
  if (!self->op_cancellable) {
    self->op_cancellable = camel_operation_new ();
    g_signal_connect_object (self->op_cancellable, "status",
                             G_CALLBACK (on_operation_status), self, G_CONNECT_DEFAULT);
  }

  return self->op_cancellable;
}

static void
clear_refresh_watchdog (StampAccountItem *self)
{
  g_clear_handle_id (&self->refresh_watchdog_handler, g_source_remove);
}

static StampFolderItem *
stamp_account_item_find_item (GListStore  *store,
                              const gchar *full_name)
{
  guint list_len = g_list_model_get_n_items (G_LIST_MODEL (store));

  for (guint idx = 0; idx < list_len; idx++) {
    StampFolderItem *item = g_list_model_get_item (G_LIST_MODEL (store), idx);
    GListStore *child_store;

    if (g_strcmp0 (stamp_folder_item_get_full_name (item), full_name) == 0) {
      return item;
    }

    child_store = stamp_item_get_list_store (STAMP_ITEM (item));
    if (child_store) {
      item = stamp_account_item_find_item (child_store, full_name);
      if (item)
        return item;
    }
  }
  return NULL;
}

static gboolean
refresh_folder_main (gpointer user_data);

static void
on_folder_item_changed (GtkWidget *folder,
                        gpointer   user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);

  g_signal_emit (self, signals[ACCOUNT_ITEM_CHANGED], 0);

  /* A folder item reaches the tree before it has its CamelFolder, and
   * the refresh queue is built from folders -- so the pass scheduled
   * when the tree was drawn found nothing to do. Ask for another, on
   * the same idle so a burst of folders landing together costs one. */
  if (!self->refresh_folder_handler)
    self->refresh_folder_handler = g_idle_add (refresh_folder_main, self);
}

static void
show_info (StampAccountItem *self,
           CamelFolderInfo  *folder_info)
{
  GListStore *list_store = stamp_item_get_list_store (STAMP_ITEM (self));
  g_autoptr (GPtrArray) array = NULL;
  guint old;

  old = g_list_model_get_n_items (G_LIST_MODEL (list_store));

  for (guint idx = 0; idx < old; idx++) {
    StampFolderItem *item = g_list_model_get_item (G_LIST_MODEL (list_store), idx);

    stamp_folder_item_disconnect (item);
  }

  array = g_ptr_array_new ();
  while (folder_info) {
    if (folder_info->full_name && (g_ascii_strcasecmp (folder_info->full_name, "[Google Mail]") == 0 || g_ascii_strcasecmp (folder_info->full_name, "[Gmail]") == 0)) {
      CamelFolderInfo *child = folder_info->child;

      while (child) {
        g_autoptr (StampFolderItem) folder_item = stamp_folder_item_new (stamp_item_get_account (STAMP_ITEM (self)), child);

        g_signal_connect_object (folder_item, "folder-item-added", G_CALLBACK (on_folder_item_changed), self, G_CONNECT_DEFAULT);

        g_ptr_array_add (array, g_steal_pointer (&folder_item));
        child = child->next;
      }
    } else {
      g_autoptr (StampFolderItem) folder_item = stamp_folder_item_new (stamp_item_get_account (STAMP_ITEM (self)), folder_info);

      g_signal_connect_object (folder_item, "folder-item-added", G_CALLBACK (on_folder_item_changed), self, G_CONNECT_DEFAULT);

      g_ptr_array_add (array, g_steal_pointer (&folder_item));
    }

    folder_info = folder_info->next;
  }

  g_list_store_splice (list_store, 0, old, array->pdata, array->len);
}

void
on_offline_store_folder_created (CamelOfflineStore *store,
                                 CamelFolderInfo   *object,
                                 gpointer           user_data);

static void
on_get_folder_info (GObject      *source,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);
  CamelStore *store = CAMEL_STORE (source);
  g_autoptr (CamelFolderInfo) folder_info = NULL;
  g_autoptr (GError) error = NULL;

  stamp_item_set_loading (STAMP_ITEM (self), FALSE);

  folder_info = camel_store_get_folder_info_finish (CAMEL_STORE (store), res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning ("Error getting folder: %s", error->message);
      stamp_item_set_error (STAMP_ITEM (self), error);
    } else
      stamp_item_set_error (STAMP_ITEM (self), NULL);

    return;
  }

  if (folder_info) {
    show_info (self, folder_info);
    stamp_account_item_connect_to_account (self);

    /* Defer folder refresh to give folder items time to load their folders */
    g_clear_handle_id (&self->refresh_folder_handler, g_source_remove);
    self->refresh_folder_handler = g_idle_add (refresh_folder_main, self);
  } else {
    /* An offline store with nothing cached for this account -- a new one,
     * or one whose summary has been cleared. There is nothing to draw and
     * nothing to wait for, so go online and let the reconcile ask the
     * server, which is what the account would have done all along. */
    stamp_account_item_connect_to_account (self);
  }
}

void
stamp_account_item_load (StampAccountItem *self)
{
  if (self->disposed || g_cancellable_is_cancelled (self->cancellable))
    return;

  stamp_item_set_loading (STAMP_ITEM (self), TRUE);

  /* Whatever comes back is only as good as the store it came from: an
   * offline store answers from the summary on disk, so the tree has to
   * be asked for again once the connection is up. Deciding it here, per
   * load, rather than once at construction, keeps it right no matter
   * which order the store and this item were set up in -- and makes the
   * reconcile's own load clear the flag, so it cannot loop. */
  self->reconcile_pending = !camel_offline_store_get_online (self->offline_store);

  camel_store_get_folder_info (CAMEL_STORE (self->offline_store),
                               NULL,
                               CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_FAST,
                               G_PRIORITY_DEFAULT,
                               op_cancellable_for (self),
                               on_get_folder_info,
                               self);
}

static void
on_synchronize (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);
  g_autoptr (GError) error = NULL;

  camel_store_synchronize_finish (CAMEL_STORE (self->offline_store), res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("%s: Error synchronizing store: %s", G_STRFUNC, error->message);

    g_object_unref (self);
    return;
  }

  /* The store is up now, so ask it the same question again -- this time
   * the answer comes from the server, and folders added or removed
   * elsewhere since the last run appear. Loading it also starts the
   * refresh queue, which is what fills in the message counts. */
  if (self->reconcile_pending) {
    self->reconcile_pending = FALSE;
    stamp_account_item_load (self);
  }

  g_object_unref (self);
}

static void
on_service_connect (GObject      *source,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);
  g_autoptr (GError) error = NULL;

  camel_service_connect_finish (CAMEL_SERVICE (self->offline_store), res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Error connecting to service: %s", error->message);

    g_object_unref (self);
    return;
  }

  self->connect_retries = 0;

  camel_store_synchronize (CAMEL_STORE (self->offline_store), FALSE, G_PRIORITY_DEFAULT, op_cancellable_for (self), on_synchronize, self);
}

static void
on_offline_store_set_online (GObject      *source,
                             GAsyncResult *res,
                             gpointer      user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);
  g_autoptr (GError) error = NULL;

  camel_offline_store_set_online_finish (self->offline_store, res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning ("Error setting offline store to online: %s", error->message);
      schedule_connect_retry (self);
    }

    g_object_unref (self);
    return;
  }

  camel_service_connect (CAMEL_SERVICE (self->offline_store), G_PRIORITY_DEFAULT, op_cancellable_for (self), on_service_connect, self);
}

/* Going online is refused while the session itself is still marked
 * offline, and on the way back from a suspend the session is told by the
 * same monitor signal that brought us here -- so the first attempt can
 * arrive a moment too early. Left alone it would come right on the next
 * refresh tick a minute later, which is a minute of error badges on
 * every folder. A couple of short retries close that gap; the network
 * check in connect_to_account keeps them from running while genuinely
 * offline, and the count keeps them from running forever. */
#define CONNECT_RETRY_SECONDS 5
#define CONNECT_RETRY_LIMIT 3

static gboolean
on_connect_retry (gpointer user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);

  self->connect_retry_handler = 0;
  stamp_account_item_connect_to_account (self);

  return G_SOURCE_REMOVE;
}

static void
schedule_connect_retry (StampAccountItem *self)
{
  if (self->disposed || self->connect_retry_handler)
    return;

  if (self->connect_retries >= CONNECT_RETRY_LIMIT) {
    g_debug ("%s: Giving up reconnecting until the network changes again", G_STRFUNC);
    return;
  }

  self->connect_retries++;
  self->connect_retry_handler = g_timeout_add_seconds (CONNECT_RETRY_SECONDS, on_connect_retry, self);
}

static void
on_service_disconnect (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  g_autoptr (StampAccountItem) self = user_data;
  g_autoptr (GError) error = NULL;

  if (!camel_service_disconnect_finish (CAMEL_SERVICE (source), res, &error) &&
      !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_debug ("%s: Could not drop the stale connection: %s", G_STRFUNC, error->message);

  /* Failed or not, what it was holding is of no further use. */
  stamp_account_item_connect_to_account (self);
}

/* Throw away the connection and everything queued on it, then build it
 * again from scratch.
 *
 * The order is the point. Camel serialises connect and disconnect on the
 * service, so a disconnect asked for while a read is wedged waits on the
 * very socket it was meant to abandon -- which is how one dead
 * connection takes the whole account offline and keeps it there.
 * Cancelling first releases that lock; clean = FALSE then skips the
 * parting LOGOUT, because a connection that cannot carry a fetch cannot
 * carry that either. */
static void
stamp_account_item_reset_connection (StampAccountItem *self)
{
  if (self->disposed || g_cancellable_is_cancelled (self->cancellable))
    return;

  g_cancellable_cancel (self->op_cancellable);
  g_clear_object (&self->op_cancellable);

  g_clear_handle_id (&self->connect_retry_handler, g_source_remove);
  self->connect_retries = 0;

  clear_refresh_watchdog (self);

  /* Anything still out reports to a cancellable nobody reads any more,
   * so the queue belongs to the reconnect now. */
  if (self->refreshing_item)
    stamp_item_set_loading (STAMP_ITEM (self->refreshing_item), FALSE);
  g_clear_object (&self->refreshing_item);
  self->refresh_queue_running = FALSE;

  camel_service_disconnect (CAMEL_SERVICE (self->offline_store),
                            FALSE,
                            G_PRIORITY_DEFAULT,
                            op_cancellable_for (self),
                            on_service_disconnect,
                            g_object_ref (self));
}

static gboolean
on_refresh_timeout (gpointer user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);

  /* Something arrived during the last interval, so this is a slow
   * connection and not a dead one -- which is the whole difference on a
   * phone holding on to one bar. Give it another interval. */
  if (g_atomic_int_compare_and_exchange (&self->refresh_activity, TRUE, FALSE))
    return G_SOURCE_CONTINUE;

  self->refresh_watchdog_handler = 0;

  g_warning ("%s: Refresh of %s went silent for %d seconds, dropping the connection",
             G_STRFUNC,
             self->refreshing_item ? stamp_folder_item_get_full_name (self->refreshing_item) : "(unknown)",
             REFRESH_TIMEOUT_SECONDS);

  stamp_account_item_reset_connection (self);

  return G_SOURCE_REMOVE;
}

/* One timer per refresh, armed when the call goes out and dropped when
 * it comes back, so an account that is simply idle arms nothing. */
static void
arm_refresh_watchdog (StampAccountItem *self)
{
  clear_refresh_watchdog (self);
  g_atomic_int_set (&self->refresh_activity, FALSE);
  self->refresh_watchdog_handler = g_timeout_add_seconds (REFRESH_TIMEOUT_SECONDS, on_refresh_timeout, self);
}

static void
stamp_account_item_connect_to_account (StampAccountItem *self)
{
  GNetworkMonitor *monitor = e_network_monitor_get_default ();

  if (self->disposed || g_cancellable_is_cancelled (self->cancellable))
    return;

  if (!g_network_monitor_get_network_available (monitor))
    return;

  camel_offline_store_set_online (self->offline_store,
                                  TRUE,
                                  G_PRIORITY_DEFAULT,
                                  op_cancellable_for (self),
                                  on_offline_store_set_online,
                                  g_object_ref (self));
}

static void
on_network_changed (GNetworkMonitor *monitor,
                    gboolean         network_available,
                    gpointer         user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);
  gboolean was_available = self->network_available;

  self->network_available = network_available;

  if (!network_available)
    return;

  /* Only the edge is worth acting on. The monitor also fires for changes
   * that leave the network up -- a new route, a connectivity recheck --
   * and dropping healthy connections for those would cost more than it
   * saves.
   *
   * Coming back up is different. After a suspend, or a move to another
   * network, the old connections still read as established while being
   * bound to an address the machine no longer has; they are worth less
   * than nothing, since every later request queues behind them. */
  if (was_available)
    stamp_account_item_connect_to_account (self);
  else
    stamp_account_item_reset_connection (self);
}

static void
on_offline_store_folder_changed (CamelOfflineStore *store,
                                 gpointer           user_data)
{
  /* StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data); */

  /* stamp_account_item_load (self); */
}

static gboolean
stamp_account_item_find_and_delete_item (StampAccountItem *self,
                                         GListStore       *store,
                                         const gchar      *full_name)
{
  guint list_len = g_list_model_get_n_items (G_LIST_MODEL (store));

  for (guint idx = 0; idx < list_len; idx++) {
    StampFolderItem *item = g_list_model_get_item (G_LIST_MODEL (store), idx);
    GListStore *child_store;

    if (g_strcmp0 (stamp_folder_item_get_full_name (item), full_name) == 0) {
      g_list_store_remove (store, idx);
      g_list_model_items_changed (G_LIST_MODEL (store), idx, 1, 0);
      return TRUE;
    }

    child_store = stamp_item_get_list_store (STAMP_ITEM (item));
    if (child_store) {
      if (stamp_account_item_find_and_delete_item (self, child_store, full_name))
        return TRUE;
    }
  }

  return FALSE;
}

void
on_offline_store_folder_created (CamelOfflineStore *store,
                                 CamelFolderInfo   *object,
                                 gpointer           user_data)
{
  /* on_offline_store_folder_changed (store, user_data); */
}

static void
on_offline_store_folder_deleted (CamelOfflineStore *store,
                                 CamelFolderInfo   *object,
                                 gpointer           user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);
  const gchar *full_name = object->full_name;
  GListStore *list_store = stamp_item_get_list_store (STAMP_ITEM (self));

  stamp_account_item_find_and_delete_item (self, list_store, full_name);
}

static void
on_offline_store_folder_info_stale (CamelOfflineStore *store,
                                    gpointer           user_data)
{
  on_offline_store_folder_changed (store, user_data);
}

static void
on_offline_store_folder_renamed (CamelOfflineStore *store,
                                 gchar             *object,
                                 CamelFolderInfo   *folder_info,
                                 gpointer           user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);
  StampFolderItem *dest_item = NULL;
  GListStore *list_store = stamp_item_get_list_store (STAMP_ITEM (self));

  dest_item = stamp_account_item_find_item (list_store, object);
  if (!dest_item)
    return;

  stamp_folder_item_set_folder_info (dest_item, folder_info);
}

static void
on_account_changed (StampSession *session,
                    StampAccount *account,
                    gpointer      user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);

  if (stamp_item_get_account (STAMP_ITEM (self)) != account)
    return;

  stamp_item_set_name (STAMP_ITEM (self), stamp_account_get_name (account));
}

static void
refresh_next_in_queue (StampAccountItem *self)
{
  while (!g_queue_is_empty (self->refresh_queue)) {
    g_autoptr (StampFolderItem) folder_item = g_queue_pop_head (self->refresh_queue);
    CamelFolder *folder = stamp_folder_item_get_folder (folder_item);

    if (!CAMEL_IS_FOLDER (folder))
      continue;

    g_debug ("%s: Refreshing %s", G_STRFUNC, camel_folder_get_display_name (folder));

    self->refresh_queue_running = TRUE;
    self->refreshing_item = g_steal_pointer (&folder_item);
    stamp_item_set_loading (STAMP_ITEM (self->refreshing_item), TRUE);
    arm_refresh_watchdog (self);

    camel_folder_refresh_info (folder, G_PRIORITY_DEFAULT, op_cancellable_for (self), on_refresh, g_object_ref (self));
    return;
  }
}

static void
on_refresh (GObject      *source,
            GAsyncResult *result,
            gpointer      user_data)
{
  g_autoptr (StampAccountItem) self = user_data;
  CamelFolder *folder = CAMEL_FOLDER (source);
  g_autoptr (StampFolderItem) folder_item = NULL;
  g_autoptr (GError) error = NULL;
  gboolean refreshed;

  refreshed = camel_folder_refresh_info_finish (folder, result, &error);

  if (self->disposed)
    return;

  /* A refresh cancelled by a connection reset can land after the next
   * one has gone out. It has nothing left to say about a folder it no
   * longer owns. */
  if (!self->refreshing_item || stamp_folder_item_get_folder (self->refreshing_item) != folder)
    return;

  folder_item = g_steal_pointer (&self->refreshing_item);
  clear_refresh_watchdog (self);
  self->refresh_queue_running = FALSE;

  stamp_item_set_loading (STAMP_ITEM (folder_item), FALSE);

  if (refreshed) {
    stamp_item_set_error (STAMP_ITEM (folder_item), NULL);
  } else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    /* We dropped the connection ourselves and a reconnect is already on
     * its way, so the badge would be reporting our own doing. */
    stamp_item_set_error (STAMP_ITEM (folder_item), NULL);
    return;
  } else {
    if (!stamp_item_get_error (STAMP_ITEM (folder_item)))
      g_warning ("Could not refresh folder %s: %s", camel_folder_get_display_name (folder), error->message);

    stamp_item_set_error (STAMP_ITEM (folder_item), error);
    /* Continue */
  }

  refresh_next_in_queue (self);
}

static void
create_queue (StampAccountItem *self,
              GListStore       *store)
{
  guint len = g_list_model_get_n_items (G_LIST_MODEL (store));
  g_autoptr (GPtrArray) priority_folders = g_ptr_array_new ();
  g_autoptr (GPtrArray) normal_folders = g_ptr_array_new ();

  for (guint idx = 0; idx < len; idx++) {
    g_autoptr (StampFolderItem) folder_item = STAMP_FOLDER_ITEM (g_list_model_get_item (G_LIST_MODEL (store), idx));
    GListStore *child_store = stamp_item_get_list_store (STAMP_ITEM (folder_item));
    CamelFolder *folder = stamp_folder_item_get_folder (folder_item);
    const gchar *full_name;

    /* First process child folders if present */
    if (child_store) {
      create_queue (self, child_store);
    }

    if (!CAMEL_IS_FOLDER (folder)) {
      continue;
    }

    full_name = stamp_folder_item_get_full_name (folder_item);

    /* Prioritize INBOX and its subfolders (including German names) */
    if (g_str_has_prefix (full_name, "INBOX") ||
        g_strcmp0 (full_name, "Inbox") == 0 ||
        g_strcmp0 (full_name, "INBOX") == 0 ||
        g_strcmp0 (full_name, "Posteingang") == 0 ||
        g_str_has_prefix (full_name, "Posteingang/")) {
      g_ptr_array_add (priority_folders, g_steal_pointer (&folder_item));
    } else {
      g_ptr_array_add (normal_folders, g_steal_pointer (&folder_item));
    }
  }

  /* Add priority folders first (INBOX) */
  for (guint idx = 0; idx < priority_folders->len; idx++) {
    StampFolderItem *folder_item = priority_folders->pdata[idx];
    g_queue_push_tail (self->refresh_queue, g_object_ref (folder_item));
  }

  /* Then normal folders */
  for (guint idx = 0; idx < normal_folders->len; idx++) {
    StampFolderItem *folder_item = normal_folders->pdata[idx];
    g_queue_push_tail (self->refresh_queue, g_object_ref (folder_item));
  }
}

static StampFolderItem *
find_priority_folder_item (StampAccountItem *self)
{
  GList *iter;

  for (iter = self->refresh_queue->head; iter; iter = g_list_next (iter)) {
    StampFolderItem *folder_item = STAMP_FOLDER_ITEM (iter->data);
    const gchar *full_name = stamp_folder_item_get_full_name (folder_item);

    if (g_str_has_prefix (full_name, "INBOX") ||
        g_strcmp0 (full_name, "Inbox") == 0 ||
        g_strcmp0 (full_name, "INBOX") == 0 ||
        g_strcmp0 (full_name, "Posteingang") == 0 ||
        g_str_has_prefix (full_name, "Posteingang/")) {
      return folder_item;
    }
  }

  return NULL;
}

static void
start_refresh (StampAccountItem *self)
{
  if (self->disposed || g_cancellable_is_cancelled (self->cancellable))
    return;

  /* Refreshes are run one at a time per store, which is what the queue
   * is for -- camel does not care for several at once on one service. */
  if (self->refresh_queue_running) {
    g_debug ("%s: Refresh already running, queue updated for next iteration", G_STRFUNC);
    return;
  }

  g_queue_clear_full (self->refresh_queue, g_object_unref);
  create_queue (self, stamp_item_get_list_store (STAMP_ITEM (self)));

  /* On first refresh, prioritize INBOX/Posteingang */
  if (self->first_refresh) {
    StampFolderItem *priority = find_priority_folder_item (self);

    self->first_refresh = FALSE;
    g_debug ("%s: First refresh - looking for INBOX, found: %p", G_STRFUNC, priority);

    /* The queue is drained from the head, so moving it there is all the
     * priority amounts to. The queue's own reference moves with it. */
    if (priority) {
      g_queue_remove (self->refresh_queue, priority);
      g_queue_push_head (self->refresh_queue, priority);
    }
  }

  refresh_next_in_queue (self);
}

static gboolean
refresh_folder_main (gpointer user_data)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (user_data);

  self->refresh_folder_handler = 0;
  start_refresh (self);

  return G_SOURCE_REMOVE;
}

static gboolean
on_refresh_timer (gpointer user_data)
{
  start_refresh (STAMP_ACCOUNT_ITEM (user_data));

  return G_SOURCE_CONTINUE;
}

/* A timeout on the main context rather than a thread watching a
 * deadline: g_timeout_add_seconds rounds to whole seconds, so the wakeup
 * coalesces with whatever else glib has pending instead of costing ten
 * wakeups a second of its own -- which is the difference that shows up
 * on a phone battery. */
static void
schedule_refresh_timer (StampAccountItem *self)
{
  guint interval;

  g_clear_handle_id (&self->refresh_timer_handler, g_source_remove);

  interval = g_settings_get_uint (self->mail_settings, "refresh-interval");
  if (interval == 0)
    return;

  self->refresh_timer_handler = g_timeout_add_seconds (interval, on_refresh_timer, self);
}

static void
on_refresh_interval_changed (GSettings   *settings,
                             const gchar *key,
                             gpointer     user_data)
{
  schedule_refresh_timer (STAMP_ACCOUNT_ITEM (user_data));
}

static void
stamp_account_item_constructed (GObject *object)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (object);
  GNetworkMonitor *network_monitor = e_network_monitor_get_default ();
  StampMailService *mail_service;

  G_OBJECT_CLASS (stamp_account_item_parent_class)->constructed (object);

  mail_service = stamp_account_get_mail_service (stamp_item_get_account (STAMP_ITEM (self)));
  g_assert (mail_service);

  stamp_item_set_name (STAMP_ITEM (self), stamp_account_get_name (stamp_item_get_account (STAMP_ITEM (self))));

  stamp_item_set_list_store_type (STAMP_ITEM (self), STAMP_TYPE_FOLDER_ITEM);

  self->cancellable = g_cancellable_new ();
  self->mail_settings = g_settings_new ("io.github.steeb_k.Post.mail");
  self->network_available = g_network_monitor_get_network_available (network_monitor);

  /* Register callbacks for folder changes... */
  self->offline_store = CAMEL_OFFLINE_STORE (stamp_mail_service_get_service (mail_service));
  g_object_ref (self->offline_store);

  g_signal_connect_object (self->offline_store, "folder-created", G_CALLBACK (on_offline_store_folder_created), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (self->offline_store, "folder-deleted", G_CALLBACK (on_offline_store_folder_deleted), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (self->offline_store, "folder-info-stale", G_CALLBACK (on_offline_store_folder_info_stale), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (self->offline_store, "folder-renamed", G_CALLBACK (on_offline_store_folder_renamed), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (stamp_session_get_default (), "account-changed", G_CALLBACK (on_account_changed), self, G_CONNECT_DEFAULT);

  /* NetworkMonitor */
  g_signal_connect_object (network_monitor, "network-changed", G_CALLBACK (on_network_changed), self, G_CONNECT_DEFAULT);

  self->refresh_queue = g_queue_new ();

  g_signal_connect_object (self->mail_settings, "changed::refresh-interval",
                           G_CALLBACK (on_refresh_interval_changed), self, G_CONNECT_DEFAULT);
  schedule_refresh_timer (self);
}

static void
refresh_queue_free (GQueue *queue)
{
  g_queue_free_full (queue, g_object_unref);
}

static void
stamp_account_item_dispose (GObject *object)
{
  StampAccountItem *self = STAMP_ACCOUNT_ITEM (object);

  self->disposed = TRUE;

  g_cancellable_cancel (self->cancellable);
  g_cancellable_cancel (self->op_cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->op_cancellable);

  g_clear_handle_id (&self->refresh_folder_handler, g_source_remove);
  g_clear_handle_id (&self->refresh_timer_handler, g_source_remove);
  g_clear_handle_id (&self->refresh_watchdog_handler, g_source_remove);
  g_clear_handle_id (&self->connect_retry_handler, g_source_remove);

  g_clear_object (&self->refreshing_item);
  g_clear_pointer (&self->refresh_queue, refresh_queue_free);
  g_clear_object (&self->mail_settings);
  g_clear_object (&self->offline_store);

  G_OBJECT_CLASS (stamp_account_item_parent_class)->dispose (object);
}

void
stamp_account_item_class_init (StampAccountItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = stamp_account_item_constructed;
  object_class->dispose = stamp_account_item_dispose;

  signals[ACCOUNT_ITEM_CHANGED] = g_signal_new ("account-item-changed", G_OBJECT_CLASS_TYPE (klass),
                                                G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                                0, NULL, NULL, NULL,
                                                G_TYPE_NONE,
                                                0);
}

void
stamp_account_item_init (StampAccountItem *self)
{
  self->first_refresh = TRUE;
}

StampAccountItem *
stamp_account_item_new (StampAccount *account)
{
  return g_object_new (STAMP_TYPE_ACCOUNT_ITEM, "account", account, NULL);
}
