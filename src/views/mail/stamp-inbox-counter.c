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
 * Counts the unread mail waiting in the inboxes, so the Mail button in
 * the view switcher can carry a badge. The counterpart to
 * StampTodayCounter, which does the same for the Calendar button.
 *
 * Only the accounts the active profile shows are counted: a badge that
 * kept counting a hidden account would send you looking for mail the
 * window is not going to show you. The inbox is whichever folder the
 * store calls its own -- asking camel rather than matching on the name
 * is what makes this work for a server that spells it "Posteingang".
 */

#define G_LOG_DOMAIN "stamp-inbox-counter"

#include "stamp-inbox-counter.h"

#include <camel/camel.h>

#include "stamp-account.h"
#include "stamp-profile-manager.h"
#include "stamp-session.h"

/*
 * One account's inbox. The folder arrives later than the account does,
 * so an entry spends its first moments with nothing to count.
 */
typedef struct {
  gchar *uid;
  CamelFolder *folder;
  gulong changed_id;
} Inbox;

struct _StampInboxCounter {
  GObject parent_instance;

  /* Account uid -> Inbox. The key is the entry's own uid. */
  GHashTable *inboxes;
  GCancellable *cancellable;
  guint count;
};

enum {
  PROP_0,
  PROP_COUNT,
  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_FINAL_TYPE (StampInboxCounter, stamp_inbox_counter, G_TYPE_OBJECT);

static void
inbox_free (gpointer data)
{
  Inbox *inbox = data;

  if (inbox->folder) {
    g_clear_signal_handler (&inbox->changed_id, inbox->folder);
    g_clear_object (&inbox->folder);
  }

  g_free (inbox->uid);
  g_free (inbox);
}

static void
update_count (StampInboxCounter *self)
{
  GHashTableIter iter;
  gpointer value;
  guint count = 0;

  g_hash_table_iter_init (&iter, self->inboxes);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    Inbox *inbox = value;
    CamelFolderSummary *summary;

    if (!inbox->folder || !stamp_profile_shows_account (inbox->uid))
      continue;

    summary = camel_folder_get_folder_summary (inbox->folder);
    if (!summary)
      continue;

    count += camel_folder_summary_get_unread_count (summary);
  }

  if (self->count == count)
    return;

  self->count = count;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COUNT]);
}

static void
on_folder_changed (CamelFolder           *folder,
                   CamelFolderChangeInfo *changes,
                   gpointer               user_data)
{
  update_count (STAMP_INBOX_COUNTER (user_data));
}

static void
on_profile_changed (StampProfileManager *manager,
                    gpointer             user_data)
{
  update_count (STAMP_INBOX_COUNTER (user_data));
}

typedef struct {
  StampInboxCounter *self;
  gchar *uid;
} InboxRequest;

static void
inbox_request_free (InboxRequest *request)
{
  g_clear_object (&request->self);
  g_free (request->uid);
  g_free (request);
}

/*
 * Stops waiting on @uid, so that an account whose inbox could not be
 * opened is asked again the next time it turns up. A start with nothing
 * cached yet has no folder to hand out, and an entry left behind here
 * would keep track_account() from ever asking a second time.
 */
static void
forget_account (StampInboxCounter *self,
                const gchar       *uid)
{
  if (self->inboxes)
    g_hash_table_remove (self->inboxes, uid);
}

/*
 * The inbox, opened under the name the store keeps it by -- the same
 * folder the folder list is watching, rather than a copy of it.
 */
static void
on_inbox_folder_opened (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  InboxRequest *request = user_data;
  StampInboxCounter *self = request->self;
  g_autoptr (CamelFolder) folder = NULL;
  g_autoptr (GError) error = NULL;
  Inbox *inbox;

  folder = camel_store_get_folder_finish (CAMEL_STORE (source), result, &error);

  if (!folder) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_debug ("%s: Could not open the inbox for account %s: %s", G_STRFUNC, request->uid, error->message);
      forget_account (self, request->uid);
    }
    goto out;
  }

  inbox = self->inboxes ? g_hash_table_lookup (self->inboxes, request->uid) : NULL;
  if (!inbox || inbox->folder)
    goto out;

  inbox->folder = g_steal_pointer (&folder);
  inbox->changed_id = g_signal_connect (inbox->folder, "changed", G_CALLBACK (on_folder_changed), self);

  update_count (self);

out:
  inbox_request_free (request);
}

static void
on_inbox_folder (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  InboxRequest *request = user_data;
  StampInboxCounter *self = request->self;
  g_autoptr (CamelFolder) folder = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *full_name = NULL;
  Inbox *inbox;

  folder = camel_store_get_inbox_folder_finish (CAMEL_STORE (source), result, &error);

  if (!folder) {
    /* Plenty of stores have no inbox to hand out, and the account is
     * still perfectly usable without one; it simply adds nothing. It
     * may also be too early to have one, so let the account be asked
     * again rather than writing it off for the rest of the session. */
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_debug ("%s: No inbox for account %s: %s", G_STRFUNC, request->uid, error->message);
      forget_account (self, request->uid);
    }
    goto out;
  }

  inbox = self->inboxes ? g_hash_table_lookup (self->inboxes, request->uid) : NULL;
  if (!inbox || inbox->folder)
    goto out;

  /* What came back is not the folder the rest of the window is holding,
   * and keeping it would count a second copy of the same mailbox that
   * nothing ever refreshes -- a badge frozen at whatever was on disk
   * when the account loaded. Camel asks its store for a folder called
   * "inbox" and caches folders under the name it was asked for, while
   * IMAP only spells it "INBOX" further in, once that cache key has
   * been settled.
   *
   * So take the name and open it again, which lands on the folder the
   * folder list already has. Asking for the inbox is still what finds
   * it: the name it answers with is the store's own, whether that is
   * "INBOX" or a maildir's "Inbox". */
  full_name = g_strdup (camel_folder_get_full_name (folder));
  if (!full_name) {
    forget_account (self, request->uid);
    goto out;
  }

  camel_store_get_folder (CAMEL_STORE (source),
                          full_name,
                          CAMEL_STORE_FOLDER_NONE,
                          G_PRIORITY_DEFAULT,
                          self->cancellable,
                          on_inbox_folder_opened,
                          g_steal_pointer (&request));
  return;

out:
  inbox_request_free (request);
}

/*
 * Starts counting @account's inbox, if it has mail at all. Called again
 * whenever mail is switched on for an account that had none, so an
 * account already being counted is left alone.
 */
static void
track_account (StampInboxCounter *self,
               StampAccount      *account)
{
  StampMailService *mail_service = stamp_account_get_mail_service (account);
  const gchar *uid = stamp_account_get_uid (account);
  CamelService *service;
  InboxRequest *request;
  Inbox *inbox;

  if (!uid || !mail_service || !stamp_mail_service_get_enabled (mail_service))
    return;

  if (g_hash_table_contains (self->inboxes, uid))
    return;

  service = stamp_mail_service_get_service (mail_service);
  if (!CAMEL_IS_STORE (service))
    return;

  inbox = g_new0 (Inbox, 1);
  inbox->uid = g_strdup (uid);
  g_hash_table_insert (self->inboxes, inbox->uid, inbox);

  request = g_new0 (InboxRequest, 1);
  request->self = g_object_ref (self);
  request->uid = g_strdup (uid);

  camel_store_get_inbox_folder (CAMEL_STORE (service),
                                G_PRIORITY_DEFAULT,
                                self->cancellable,
                                on_inbox_folder,
                                request);
}

static void
on_mail_added (StampAccount *emitter,
               StampAccount *account,
               gpointer      service,
               gpointer      user_data)
{
  track_account (STAMP_INBOX_COUNTER (user_data), account);
}

static void
watch_account (StampInboxCounter *self,
               StampAccount      *account)
{
  /* The session announces an account as it is added and again once the
   * whole set has loaded, and both are worth acting on -- but only one
   * of them may leave a handler behind. */
  if (g_signal_handler_find (account, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
                             0, 0, NULL, on_mail_added, self) == 0) {
    /* Mail can be switched on after the account exists, and the store
     * is only there to ask once it has been. */
    g_signal_connect_object (account, "mail-added", G_CALLBACK (on_mail_added), self, G_CONNECT_DEFAULT);
  }

  track_account (self, account);
}

static void
on_account_added (GObject      *session,
                  StampAccount *account,
                  gpointer      user_data)
{
  watch_account (STAMP_INBOX_COUNTER (user_data), account);
}

static void
on_account_removed (GObject      *session,
                    StampAccount *account,
                    gpointer      user_data)
{
  StampInboxCounter *self = STAMP_INBOX_COUNTER (user_data);
  const gchar *uid = stamp_account_get_uid (account);

  if (uid && g_hash_table_remove (self->inboxes, uid))
    update_count (self);
}

static void
on_accounts_loaded (StampInboxCounter *self)
{
  StampSession *session = stamp_session_get_default ();

  for (GList *iter = stamp_session_get_accounts (session); iter; iter = iter->next)
    watch_account (self, STAMP_ACCOUNT (iter->data));
}

/*
 * GObject
 */

static void
stamp_inbox_counter_constructed (GObject *object)
{
  StampInboxCounter *self = STAMP_INBOX_COUNTER (object);
  StampSession *session = stamp_session_get_default ();

  G_OBJECT_CLASS (stamp_inbox_counter_parent_class)->constructed (object);

  g_signal_connect_object (session, "account-added", G_CALLBACK (on_account_added), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (session, "account-removed", G_CALLBACK (on_account_removed), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (session, "accounts-loaded", G_CALLBACK (on_accounts_loaded), self, G_CONNECT_SWAPPED);

  g_signal_connect_object (stamp_profile_manager_get_default (), "changed",
                           G_CALLBACK (on_profile_changed), self, G_CONNECT_DEFAULT);

  /* The session may have finished loading before this existed. */
  if (stamp_session_get_accounts_loaded (session))
    on_accounts_loaded (self);
}

static void
stamp_inbox_counter_dispose (GObject *object)
{
  StampInboxCounter *self = STAMP_INBOX_COUNTER (object);

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  g_clear_object (&self->cancellable);
  g_clear_pointer (&self->inboxes, g_hash_table_unref);

  G_OBJECT_CLASS (stamp_inbox_counter_parent_class)->dispose (object);
}

static void
stamp_inbox_counter_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  StampInboxCounter *self = STAMP_INBOX_COUNTER (object);

  switch (property_id) {
    case PROP_COUNT:
      g_value_set_uint (value, self->count);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
stamp_inbox_counter_class_init (StampInboxCounterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = stamp_inbox_counter_constructed;
  object_class->dispose = stamp_inbox_counter_dispose;
  object_class->get_property = stamp_inbox_counter_get_property;

  properties[PROP_COUNT] = g_param_spec_uint ("count",
                                              NULL,
                                              NULL,
                                              0,
                                              G_MAXUINT,
                                              0,
                                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
stamp_inbox_counter_init (StampInboxCounter *self)
{
  self->inboxes = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, inbox_free);
  self->cancellable = g_cancellable_new ();
}

StampInboxCounter *
stamp_inbox_counter_new (void)
{
  return g_object_new (STAMP_TYPE_INBOX_COUNTER, NULL);
}

guint
stamp_inbox_counter_get_count (StampInboxCounter *self)
{
  g_return_val_if_fail (STAMP_IS_INBOX_COUNTER (self), 0);

  return self->count;
}
