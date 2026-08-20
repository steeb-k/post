/*
 * Copyright 2024-2026 Jan-Michael Brummer
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

#include "stamp-account.h"

#include "stamp-folder-index.h"
#include "stamp-photo-cache.h"
#include "stamp-session.h"

struct _StampMailService {
  CamelService *service;

  CamelSession *session;
  CamelTransport *transport;
  CamelFolder *trash_folder;
  CamelFolder *sent_folder;
  CamelFolder *drafts_folder;
  CamelFolder *junk_folder;
  ESource *source;
  ESource *transport_source;
  ESource *identity_source;
  CamelInternetAddress *address;
  gboolean enabled;
  GHashTable *aliases;
  ESourceRegistry *registry;
};

struct _StampContactsService {
  EBookClient *client;
  ESource *source;
  gboolean enabled;
};

struct _StampAccount {
  GObject parent_instance;

  gchar *uid;
  gchar *display_name;
  ESourceRegistry *registry;

  ESource *collection;
  StampMailService *mail;
  GPtrArray *address_books;

  GCancellable *cancellable;

  StampPhotoCache *photo_cache;
  GList *categories;
  StampFolderIndex *folder_index;

  /* Guards for the special-folder recheck; see
   * on_store_connection_status_changed. */
  gboolean recheck_pending;
  guint recheck_retries;
  guint recheck_retry_handler;
};

G_DEFINE_FINAL_TYPE (StampAccount, stamp_account, G_TYPE_OBJECT);

typedef struct {
  StampAccount *account;
  GTask *parent_task;
  gint pending;
  GError *error;
  GMutex lock;
} InitContext;

enum {
  MAIL_ADDED,
  MAIL_REMOVED,
  BOOK_ADDED,
  BOOK_REMOVED,
  LAST_SIGNAL,
};

static gint signals[LAST_SIGNAL] = { 0 };

static void
init_context_free (InitContext *ctx)
{
  g_mutex_clear (&ctx->lock);
  g_clear_error (&ctx->error);
  g_clear_object (&ctx->parent_task);
  g_clear_pointer (&ctx, g_free);
}

static void
init_context_finish_one (InitContext *ctx)
{
  gint remaining;

  g_mutex_lock (&ctx->lock);
  remaining = --ctx->pending;
  g_mutex_unlock (&ctx->lock);

  if (remaining > 0)
    return;

  if (ctx->error)
    g_task_return_error (ctx->parent_task, g_steal_pointer (&ctx->error));
  else
    g_task_return_boolean (ctx->parent_task, TRUE);

  g_clear_pointer (&ctx, init_context_free);
}

static void
init_context_set_error (InitContext *ctx,
                        GError      *error)
{
  g_mutex_lock (&ctx->lock);
  if (!ctx->error)
    g_propagate_error (&ctx->error, error);
  else
    g_clear_error (&error);

  g_mutex_unlock (&ctx->lock);
}

static void
stamp_account_init (StampAccount *self)
{
  self->cancellable = g_cancellable_new ();

  self->address_books = g_ptr_array_new_with_free_func (g_free);
}

static void
stamp_account_mail_service_free (gpointer data)
{
  StampMailService *service = data;

  g_clear_object (&service->address);
  g_clear_object (&service->transport);
  g_clear_object (&service->session);
  g_clear_object (&service->source);
  g_clear_object (&service->trash_folder);
  g_clear_object (&service->sent_folder);
  g_clear_object (&service->drafts_folder);
  g_clear_object (&service->junk_folder);
  g_clear_pointer (&service, g_free);
}

static void
stamp_account_dispose (GObject *object)
{
  StampAccount *self = STAMP_ACCOUNT (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_handle_id (&self->recheck_retry_handler, g_source_remove);

  g_clear_pointer (&self->uid, g_free);
  g_clear_pointer (&self->display_name, g_free);
  g_clear_object (&self->collection);
  g_clear_object (&self->registry);

  /* Before the service goes: the index holds folders that belong to
   * that store, and dropping it first lets them go with it. */
  g_clear_object (&self->folder_index);

  g_clear_pointer (&self->mail, stamp_account_mail_service_free);
  g_clear_pointer (&self->address_books, g_ptr_array_unref);

  g_clear_pointer (&self->photo_cache, stamp_photo_cache_free);

  G_OBJECT_CLASS (stamp_account_parent_class)->dispose (object);
}

static void
stamp_account_class_init (StampAccountClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_account_dispose;

  signals[MAIL_ADDED] = g_signal_new ("mail-added", G_OBJECT_CLASS_TYPE (klass),
                                      G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                      0, NULL, NULL, NULL,
                                      G_TYPE_NONE,
                                      2, STAMP_TYPE_ACCOUNT,
                                      G_TYPE_POINTER);
  signals[MAIL_REMOVED] = g_signal_new ("mail-removed", G_OBJECT_CLASS_TYPE (klass),
                                        G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL, NULL,
                                        G_TYPE_NONE,
                                        2, STAMP_TYPE_ACCOUNT,
                                        G_TYPE_POINTER);
  signals[BOOK_ADDED] = g_signal_new ("book-added", G_OBJECT_CLASS_TYPE (klass),
                                      G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                      0, NULL, NULL, NULL,
                                      G_TYPE_NONE,
                                      2, STAMP_TYPE_ACCOUNT,
                                      G_TYPE_POINTER);
  signals[BOOK_REMOVED] = g_signal_new ("book-removed", G_OBJECT_CLASS_TYPE (klass),
                                        G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL, NULL,
                                        G_TYPE_NONE,
                                        2, STAMP_TYPE_ACCOUNT,
                                        G_TYPE_POINTER);
}

StampAccount *
stamp_account_new (ESource         *collection,
                   ESourceRegistry *registry)
{
  StampAccount *self = g_object_new (STAMP_TYPE_ACCOUNT, NULL);

  self->uid = g_strdup (e_source_get_uid (collection));
  self->display_name = g_strdup (e_source_get_display_name (collection));
  self->registry = g_object_ref (registry);
  self->collection = g_object_ref (collection);

  self->photo_cache = stamp_photo_cache_new (self);

  return self;
}

const gchar *
stamp_account_get_name (StampAccount *self)
{
  return self->display_name;
}

const gchar *
stamp_account_get_uid (StampAccount *self)
{
  return self->uid;
}

static EBookClient *
stamp_account_get_book_client (StampAccount *self)
{
  if (self->address_books->len != 0) {
    StampContactsService *service;

#if 0
    /* FIXME: Hack for our company, remove once setting of default book is enabled */
    for (gint idx = 0; idx < self->address_books->len; idx++) {
      const gchar *name;

      service = g_ptr_array_index (self->address_books, idx);
      name = e_source_get_display_name (service->source);

      if (g_strcmp0 (name, "Organisationsbenutzer") == 0)
        return service->client;
    }
#endif

    service = g_ptr_array_index (self->address_books, 0);
    return service->client;
  }

  return NULL;
}

/*
 * A book only takes writes once its client has connected and the backend
 * says it is not read-only. Google and EWS both hand out books that answer
 * queries perfectly well and refuse everything else, so asking the client
 * is the only honest way to know where a new contact can go.
 */
GPtrArray *
stamp_account_get_writable_books (StampAccount *self)
{
  GPtrArray *books = g_ptr_array_new ();

  for (guint idx = 0; idx < self->address_books->len; idx++) {
    StampContactsService *service = g_ptr_array_index (self->address_books, idx);

    if (!service->enabled || !service->client)
      continue;

    if (e_client_is_readonly (E_CLIENT (service->client)))
      continue;

    g_ptr_array_add (books, service);
  }

  return books;
}

void
stamp_account_invalidate_photo (StampAccount *self,
                                const gchar  *key)
{
  stamp_photo_cache_invalidate (self->photo_cache, key);
}

void
stamp_account_get_photo (StampAccount *self,
                         const gchar  *sender,
                         GCancellable *cancellable,
                         GFunc         callback,
                         gpointer      user_data)
{
  stamp_photo_cache_lookup_async (self->photo_cache, sender, NULL, cancellable, callback, user_data);
}

gpointer
stamp_account_get_photo_finish (StampAccount  *session,
                                GAsyncResult  *result,
                                GError       **error)
{
  gpointer ptr;

  g_assert (g_task_is_valid (result, session));
  g_assert (error && !*error);

  ptr = g_task_propagate_pointer (G_TASK (result), error);

  return ptr;
}

void
stamp_account_search_contacts (StampAccount        *self,
                               EBookClient         *client,
                               const gchar         *search_text,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *sexp = NULL;
  EBookQuery *query[2];
  EBookQuery *exists_query;
  EBookQuery *or_query;
  EBookQuery *final_query;
  EBookClient *book_client;

  if (client)
    book_client = client;
  else
    book_client = stamp_account_get_book_client (self);

  if (!book_client)
    return;

  g_debug ("%s: Query for %s in %s", G_STRFUNC, search_text, e_source_get_display_name (e_client_get_source (E_CLIENT (book_client))));

  query[0] = e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_CONTAINS, search_text);
  query[1] = e_book_query_field_test (E_CONTACT_FULL_NAME, E_BOOK_QUERY_CONTAINS, search_text);

  exists_query = e_book_query_field_exists (E_CONTACT_EMAIL);
  or_query = e_book_query_or (2, query, TRUE);
  final_query = e_book_query_andv (exists_query, or_query, NULL);
  sexp = e_book_query_to_string (final_query);
  e_book_client_get_contacts (book_client, sexp, cancellable, callback, user_data);
}

GSList *
stamp_account_search_contacts_finish (StampAccount  *self,
                                      EBookClient   *client,
                                      GAsyncResult  *res,
                                      GError       **error)
{
  GSList *contacts = NULL;
  g_autoptr (GError) local_error = NULL;
  EBookClient *book_client;

  if (client)
    book_client = client;
  else
    book_client = stamp_account_get_book_client (self);

  e_book_client_get_contacts_finish (book_client, res, &contacts, &local_error);
  if (local_error) {
    g_warning ("Could not get contacts: %s", local_error->message);
    return NULL;
  }

  return contacts;
}

static void
free_contact_slist (gpointer data)
{
  g_slist_free_full ((GSList *)data, (GDestroyNotify)g_object_unref);
}

static void
scan_folder_for_contacts (CamelFolder  *folder,
                          const gchar  *lower_search,
                          GHashTable   *unique,
                          GCancellable *cancellable)
{
  g_autoptr (GPtrArray) uids = camel_folder_dup_uids (folder);
  CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);

  if (!uids || !summary)
    return;

  for (guint i = 0; i < uids->len; i++) {
    const gchar *uid;
    const gchar *from;
    CamelMessageInfo *info;

    if (g_cancellable_is_cancelled (cancellable))
      break;

    uid = g_ptr_array_index (uids, i);
    info = camel_folder_summary_get (summary, uid);
    if (!info)
      continue;

    from = camel_message_info_get_from (info);
    if (from) {
      g_autoptr (CamelInternetAddress) addr = camel_internet_address_new ();

      if (camel_address_decode (CAMEL_ADDRESS (addr), from) > 0) {
        const gchar *name;
        const gchar *email;

        if (camel_internet_address_get (addr, 0, &name, &email) && email) {
          g_autofree char *lower_email = g_ascii_strdown (email, -1);
          gboolean matches = g_strstr_len (lower_email, -1, lower_search) != NULL;

          if (!matches && name) {
            g_autofree char *lower_name = g_ascii_strdown (name, -1);

            matches = g_strstr_len (lower_name, -1, lower_search) != NULL;
          }

          if (matches && !g_hash_table_contains (unique, lower_email)) {
            EContact *contact = e_contact_new ();
            GSList email_list;

            email_list.data = (gpointer)lower_email;
            email_list.next = NULL;

            e_contact_set (contact, E_CONTACT_FULL_NAME, name ? name : lower_email);
            e_contact_set (contact, E_CONTACT_EMAIL, &email_list);

            g_hash_table_insert (unique, g_strdup (lower_email), contact);
          }
        }
      }
    }
  }
}

static void
search_conversations_thread (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable)
{
  StampAccount *self = STAMP_ACCOUNT (source_object);
  const gchar *search_text = task_data;
  GHashTable *unique;
  g_autoslist (EContact) results = NULL;
  GHashTableIter iter;
  gpointer value;

  unique = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  if (self->mail) {
    if (self->mail->sent_folder)
      scan_folder_for_contacts (self->mail->sent_folder, search_text, unique, cancellable);

    if (self->mail->service && !g_cancellable_is_cancelled (cancellable)) {
      g_autoptr (GError) error = NULL;
      CamelFolder *inbox;

      inbox = camel_store_get_folder_sync (CAMEL_STORE (self->mail->service),
                                           "INBOX", CAMEL_STORE_FOLDER_NONE,
                                           cancellable, &error);
      if (inbox) {
        scan_folder_for_contacts (inbox, search_text, unique, NULL);
        g_object_unref (inbox);
      }
    }
  }

  g_hash_table_iter_init (&iter, unique);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    EContact *contact = value;

    results = g_slist_prepend (results, g_object_ref (contact));
  }

  g_hash_table_remove_all (unique);

  if (g_cancellable_is_cancelled (cancellable)) {
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Search cancelled");
  } else {
    g_task_return_pointer (task, g_steal_pointer (&results), free_contact_slist);
  }
}

void
stamp_account_search_conversations (StampAccount        *self,
                                    const gchar         *search_text,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  gchar *lower_search;

  if (!self->mail)
    return;

  lower_search = g_ascii_strdown (search_text, -1);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, stamp_account_search_conversations);
  g_task_set_task_data (task, lower_search, g_free);
  g_task_run_in_thread (task, search_conversations_thread);
}

GSList *
stamp_account_search_conversations_finish (StampAccount  *self,
                                           GAsyncResult  *res,
                                           GError       **error)
{
  return g_task_propagate_pointer (G_TASK (res), error);
}

GPtrArray *
stamp_account_get_books (StampAccount *self)
{
  return self->address_books;
}

void
stamp_account_add_mail (StampAccount *self,
                        ESource      *source)
{
  if (!self->mail)
    self->mail = g_new0 (StampMailService, 1);
  self->mail->source = g_object_ref (source);
  self->mail->registry = self->registry;
}

void
stamp_account_add_address_book (StampAccount *self,
                                ESource      *source)
{
  StampContactsService *contacts = g_new0 (StampContactsService, 1);

  contacts->source = g_object_ref (source);
  contacts->enabled = FALSE;
  g_ptr_array_add (self->address_books, contacts);
}

static gboolean
is_goa_disabled (StampAccount *self,
                 ESource      *source)
{
  g_autoptr (ESource) goa_source = NULL;
  gboolean goa_mail_disabled = FALSE;

  goa_source = e_source_registry_find_extension (self->registry, source, E_SOURCE_EXTENSION_GOA);
  if (goa_source)
    goa_mail_disabled = !e_source_get_enabled (source);

  return goa_mail_disabled;
}

static StampContactsService *
find_book_by_source (StampAccount *self,
                     ESource      *source)
{
  if (!self->address_books)
    return NULL;

  for (guint idx = 0; idx < self->address_books->len; idx++) {
    StampContactsService *service = g_ptr_array_index (self->address_books, idx);

    if (service->source == source || g_strcmp0 (e_source_get_uid (service->source), e_source_get_uid (source)) == 0)
      return service;
  }

  return NULL;
}

static void
on_book_client_ready (GObject      *src,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  InitContext *ctx = user_data;
  g_autoptr (GError) error = NULL;
  EClient *client = e_book_client_connect_finish (res, &error);

  if (error) {
    g_warning ("%s: EBookClient failed: %s", G_STRFUNC, error->message);
    init_context_set_error (ctx, g_steal_pointer (&error));
  } else {
    ESource *source = e_client_get_source (client);
    StampContactsService *service = find_book_by_source (ctx->account, source);

    if (service) {
      service->client = E_BOOK_CLIENT (client);
      service->enabled = TRUE;
      g_debug ("%s: Book '%s' connected", G_STRFUNC, e_source_get_display_name (service->source));

      /* TODO: Currently we are deleting everything once a book is connected due to the fact
       * that books are loaded async. Therefore it might have happen that an existing avatar
       * is marked as negative. Check for a better alternative.
       */
      stamp_disk_cache_purge_negative (ctx->account->photo_cache);

      g_signal_emit (ctx->account, signals[BOOK_ADDED], 0, ctx->account, service);
    } else {
      g_clear_object (&client);
    }
  }

  init_context_finish_one (ctx);
}

static gboolean
is_drafts_folder (CamelFolderInfo *fi)
{
  return (fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_DRAFTS;
}

/* Not every server tells us what its special folders are. Proton Bridge
 * for example sends the folder flags but does not announce SPECIAL-USE,
 * so Camel never sets the folder type and we have to go by name. */
static const gchar *
find_folder_by_name (CamelFolderInfo     *root,
                     const gchar * const *names)
{
  CamelFolderInfo *fi = root;

  while (fi) {
    for (guint i = 0; names[i]; i++) {
      /* The display name as well as the full one: Gmail files its own
       * folders under a [Gmail] parent, so the Trash everything here is
       * looking for has the full name "[Gmail]/Trash" and would match
       * nothing. Matching the leaf finds it, and finds the same folder
       * on a server that keeps it at the top level. */
      if (g_ascii_strcasecmp (fi->full_name, names[i]) == 0 ||
          (fi->display_name && g_ascii_strcasecmp (fi->display_name, names[i]) == 0))
        return fi->full_name;
    }

    if (fi->child) {
      fi = fi->child;
      continue;
    }
    while (fi && !fi->next)
      fi = fi->parent;
    if (fi)
      fi = fi->next;
  }

  return NULL;
}

typedef struct {
  StampAccount *account;
  InitContext *ctx;
  gint pending;
} MailEnableData;

static void
mail_enable_one_done (MailEnableData *data)
{
  if (--data->pending > 0)
    return;

  if (data->account->mail && data->account->mail->service)
    g_signal_emit (data->account, signals[MAIL_ADDED], 0, data->account, data->account->mail->service);

  if (data->ctx)
    init_context_finish_one (data->ctx);

  g_free (data);
}

static void
on_junk_folder_ready (GObject      *src,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  MailEnableData *data = user_data;
  g_autoptr (GError) error = NULL;
  CamelFolder *folder = camel_store_get_junk_folder_finish (CAMEL_STORE (src), res, &error);

  if (folder)
    data->account->mail->junk_folder = folder;
  else
    g_warning ("%s: %s", G_STRFUNC, error ? error->message : "");

  mail_enable_one_done (data);
}

static void
on_trash_folder_from_info_ready (GObject      *src,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  MailEnableData *data = user_data;
  g_autoptr (GError) error = NULL;
  CamelFolder *folder = camel_store_get_folder_finish (CAMEL_STORE (src), res, &error);

  if (folder)
    data->account->mail->trash_folder = folder;
  else
    g_warning ("%s: %s", G_STRFUNC, error ? error->message : "");

  mail_enable_one_done (data);
}

static void
on_sent_folder_ready (GObject      *src,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  MailEnableData *data = user_data;
  g_autoptr (GError) error = NULL;
  CamelFolder *folder = camel_store_get_folder_finish (CAMEL_STORE (src), res, &error);

  if (folder)
    data->account->mail->sent_folder = folder;
  else
    g_warning ("%s: %s", G_STRFUNC, error ? error->message : "");

  mail_enable_one_done (data);
}

static void
on_drafts_folder_ready (GObject      *src,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  MailEnableData *data = user_data;
  g_autoptr (GError) error = NULL;
  CamelFolder *folder = camel_store_get_folder_finish (CAMEL_STORE (src), res, &error);

  if (folder)
    data->account->mail->drafts_folder = folder;
  else
    g_warning ("%s: %s", G_STRFUNC, error ? error->message : "");

  mail_enable_one_done (data);
}

/*
 * Which folders of @root are the account's own Sent, Drafts and Trash.
 *
 * Asked twice: once against the summary on disk, which is all an
 * account that starts offline has, and again once the store is online
 * and the server has said what its folders are for. The first answer is
 * usually complete, and where it is not the second one settles it.
 */
static void
find_special_paths (CamelFolderInfo  *root,
                    const gchar     **sent_path,
                    const gchar     **drafts_path,
                    const gchar     **trash_path)
{
  CamelFolderInfo *fi = root;

  *sent_path = NULL;
  *drafts_path = NULL;
  *trash_path = NULL;

  while (fi) {
    if (!*sent_path && ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_SENT))
      *sent_path = fi->full_name;

    if (!*drafts_path && is_drafts_folder (fi))
      *drafts_path = fi->full_name;

    if (!*trash_path && ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_TRASH))
      *trash_path = fi->full_name;

    if (fi->child) {
      fi = fi->child;
      continue;
    }
    while (fi && !fi->next)
      fi = fi->parent;
    if (fi)
      fi = fi->next;
  }

  if (!*sent_path) {
    static const gchar * const names[] = { "Sent", "Sent Mail", "Sent Items", "Sent Messages", NULL };

    *sent_path = find_folder_by_name (root, names);
  }

  if (!*drafts_path) {
    static const gchar * const names[] = { "Drafts", "Draft", NULL };

    *drafts_path = find_folder_by_name (root, names);
  }

  if (!*trash_path) {
    static const gchar * const names[] = { "Trash", "Bin", "Deleted Items", "Deleted Messages", NULL };

    *trash_path = find_folder_by_name (root, names);
  }

  g_debug ("%s: sent '%s', drafts '%s', trash '%s'", G_STRFUNC,
           *sent_path ? *sent_path : "(none)",
           *drafts_path ? *drafts_path : "(none)",
           *trash_path ? *trash_path : "(none)");
}

typedef enum {
  SPECIAL_SENT,
  SPECIAL_DRAFTS,
  SPECIAL_TRASH,
  N_SPECIALS
} SpecialFolder;

/* By index rather than by address: the mail service can be torn down
 * between asking for a folder and getting one, and a pointer into a
 * struct that is gone would be worse than a lookup. */
static CamelFolder **
special_folder_slot (StampAccount  *self,
                     SpecialFolder  which)
{
  if (!self->mail)
    return NULL;

  switch (which) {
    case SPECIAL_SENT:   return &self->mail->sent_folder;
    case SPECIAL_DRAFTS: return &self->mail->drafts_folder;
    case SPECIAL_TRASH:  return &self->mail->trash_folder;
    case N_SPECIALS:
    default:             return NULL;
  }
}

typedef struct {
  StampAccount *account;
  SpecialFolder which;
} RecheckData;

static void
on_recheck_folder_ready (GObject      *src,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  RecheckData *data = user_data;
  g_autoptr (StampAccount) self = data->account;
  g_autoptr (GError) error = NULL;
  CamelFolder *folder = camel_store_get_folder_finish (CAMEL_STORE (src), res, &error);
  CamelFolder **slot = special_folder_slot (self, data->which);

  g_free (data);

  if (!folder) {
    g_warning ("%s: %s", G_STRFUNC, error ? error->message : "");
    return;
  }

  /* The first pass may have won the race; it asked first and its answer
   * is no worse than this one. */
  if (slot && !*slot)
    *slot = folder;
  else
    g_object_unref (folder);
}

/* A recheck that fails is worth another try or two -- the connection can
 * be up before the server is ready to answer -- but only a few, and only
 * one in flight at a time. An account that simply cannot answer must
 * cost nothing to keep. */
#define RECHECK_RETRY_SECONDS 5
#define RECHECK_RETRY_LIMIT 3

static void maybe_recheck_special_folders (StampAccount *self);

static gboolean
on_recheck_retry (gpointer user_data)
{
  StampAccount *self = user_data;

  self->recheck_retry_handler = 0;
  maybe_recheck_special_folders (self);

  return G_SOURCE_REMOVE;
}

static void
schedule_recheck_retry (StampAccount *self)
{
  if (self->recheck_retry_handler ||
      g_cancellable_is_cancelled (self->cancellable) ||
      self->recheck_retries >= RECHECK_RETRY_LIMIT)
    return;

  self->recheck_retries++;
  self->recheck_retry_handler = g_timeout_add_seconds (RECHECK_RETRY_SECONDS, on_recheck_retry, self);
}

static void
on_folder_info_recheck (GObject      *src,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  g_autoptr (StampAccount) self = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelFolderInfo) root = camel_store_get_folder_info_finish (CAMEL_STORE (src), res, &error);
  const gchar *paths[N_SPECIALS];

  self->recheck_pending = FALSE;

  if (!root) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning ("%s: get_folder_info: %s", G_STRFUNC, error ? error->message : "");
      schedule_recheck_retry (self);
    }

    return;
  }

  find_special_paths (root, &paths[SPECIAL_SENT], &paths[SPECIAL_DRAFTS], &paths[SPECIAL_TRASH]);

  for (guint i = 0; i < N_SPECIALS; i++) {
    CamelFolder **slot = special_folder_slot (self, i);
    RecheckData *data;

    if (!paths[i] || !slot || *slot)
      continue;

    data = g_new0 (RecheckData, 1);
    data->account = g_object_ref (self);
    data->which = i;

    camel_store_get_folder (CAMEL_STORE (src), paths[i], 0,
                            G_PRIORITY_DEFAULT, NULL,
                            on_recheck_folder_ready, data);
  }

  /* Folders the offline summary had never heard of are worth indexing
   * too, and the ones just named are worth leaving out of it. */
  if (self->folder_index) {
    for (guint i = 0; i < N_SPECIALS; i++)
      stamp_folder_index_exclude (self->folder_index, paths[i]);

    stamp_folder_index_build (self->folder_index, root);
  }
}

/*
 * An account starts offline so its folders can be drawn from the
 * summary on disk, and that summary carries folder names but not what
 * the server says each folder is for. Where the names alone did not
 * settle which folder is Trash, the question is worth asking once more
 * as soon as the store is connected and the server can answer it --
 * until it is settled, a delete has nowhere to put the mail.
 */
static void
maybe_recheck_special_folders (StampAccount *self)
{
  if (!self->mail || !self->mail->service)
    return;

  if (self->recheck_pending)
    return;

  if (self->mail->sent_folder && self->mail->drafts_folder && self->mail->trash_folder)
    return;

  self->recheck_pending = TRUE;

  camel_store_get_folder_info (CAMEL_STORE (self->mail->service), NULL,
                               CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL,
                               G_PRIORITY_LOW, self->cancellable,
                               on_folder_info_recheck, g_object_ref (self));
}

/* Driven by connection-status rather than by "online", which cannot be
 * used for this. Camel re-emits notify::online whenever host-reachable
 * changes or the connection status falls to DISCONNECTED, with the value
 * unchanged -- so a recheck fired from it feeds itself: the recheck asks
 * the store for folders, the store's failed connection attempt lands on
 * DISCONNECTED, and that re-emits the notification that started it. On a
 * store that cannot authenticate this has no ceiling, and it cost an
 * account roughly ten thousand folder-info calls and twenty thousand
 * IMAP logins per burst until the server rate-limited it.
 *
 * A failed connection reaches DISCONNECTED, never CONNECTED, so there is
 * no path from a failure back to here. It is also the more accurate
 * moment: camel notifies "online" before it attempts the connection, so
 * a recheck driven by that was asking the question before there was
 * anyone to answer it. */
static void
on_store_connection_status_changed (GObject    *object,
                                    GParamSpec *pspec,
                                    gpointer    user_data)
{
  StampAccount *self = user_data;

  self->recheck_retries = 0;
  g_clear_handle_id (&self->recheck_retry_handler, g_source_remove);

  if (camel_service_get_connection_status (CAMEL_SERVICE (object)) != CAMEL_SERVICE_CONNECTED)
    return;

  maybe_recheck_special_folders (self);
}

static void
on_folder_info_for_sent_drafts (GObject      *src,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  MailEnableData *data = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelFolderInfo) root = camel_store_get_folder_info_finish (CAMEL_STORE (src), res, &error);
  const gchar *sent_path = NULL;
  const gchar *drafts_path = NULL;
  const gchar *trash_path = NULL;
  gint new_pending = 0;

  if (error) {
    g_warning ("%s: get_folder_info: %s", G_STRFUNC, error->message);
    mail_enable_one_done (data);
    return;
  }

  find_special_paths (root, &sent_path, &drafts_path, &trash_path);

  new_pending = (sent_path ? 1 : 0) + (drafts_path ? 1 : 0) + (trash_path ? 1 : 0);
  data->pending += new_pending;

  if (sent_path)
    camel_store_get_folder (CAMEL_STORE (src), sent_path, 0,
                            G_PRIORITY_DEFAULT, NULL,
                            on_sent_folder_ready, data);
  if (drafts_path)
    camel_store_get_folder (CAMEL_STORE (src), drafts_path, 0,
                            G_PRIORITY_DEFAULT, NULL,
                            on_drafts_folder_ready, data);
  if (trash_path)
    camel_store_get_folder (CAMEL_STORE (src), trash_path, 0,
                            G_PRIORITY_DEFAULT, NULL,
                            on_trash_folder_from_info_ready, data);

  /* The same tree the scan above walked, put to a second use: which
   * ordinary folders exist is exactly what the index needs, and asking
   * the store for it again would be a second round trip for an answer
   * already in hand. The three paths resolved above go with it -- on a
   * server that advertises no folder types they are the only way to
   * know which folders those are. */
  if (data->account->folder_index) {
    stamp_folder_index_exclude (data->account->folder_index, sent_path);
    stamp_folder_index_exclude (data->account->folder_index, drafts_path);
    stamp_folder_index_exclude (data->account->folder_index, trash_path);
    stamp_folder_index_build (data->account->folder_index, root);
  }

  mail_enable_one_done (data);
}

static void
on_categories_ready (GObject      *source,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  g_autoptr (StampAccount) self = STAMP_ACCOUNT (user_data);
  g_autoptr (GError) error = NULL;
  GList *categories = stamp_m365_get_categories_finish (E_SOURCE (source), res, &error);

  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("%s: Categories: %s", G_STRFUNC, error->message);

    return;
  }

  self->categories = categories;
}

/* Camel keeps each store's folder summary in its own cache directory;
 * the folders database being there is what makes an offline start worth
 * anything. */
static gboolean
store_has_cache (CamelService *service)
{
  const gchar *cache_dir = camel_service_get_user_cache_dir (service);
  g_autofree char *folders_db = NULL;

  if (!cache_dir)
    return FALSE;

  folders_db = g_build_filename (cache_dir, "folders.db", NULL);

  return g_file_test (folders_db, G_FILE_TEST_EXISTS);
}

static void
stamp_account_enable_mail_async (StampAccount *self,
                                 InitContext  *ctx)
{
  MailEnableData *data;
  g_autoptr (GError) error = NULL;
  ESourceMailAccount *extension;
  ESourceMailTransport *transport_extension;

  self->mail->enabled = TRUE;
  extension = e_source_get_extension (self->mail->source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);

  self->mail->service = camel_session_add_service (CAMEL_SESSION (stamp_session_get_default ()),
                                                   e_source_get_uid (self->mail->source),
                                                   e_source_backend_get_backend_name (E_SOURCE_BACKEND (extension)),
                                                   CAMEL_PROVIDER_STORE,
                                                   NULL);
  if (!self->mail->service) {
    g_warning ("%s: Failed to add mail service", G_STRFUNC);
    if (ctx)
      init_context_finish_one (ctx);
    return;
  }

  /* Everything below and everything StampAccountItem does next asks the
   * store for folders, and an online store answers that by connecting
   * and authenticating first -- so nothing can be drawn until the
   * network says so. Start offline instead and the same questions are
   * answered from the summary already on disk; StampAccountItem brings
   * the store online once the tree is up, and reconciles then.
   *
   * Only worth doing when there is a summary to answer from: an account
   * that has never synced would just show an empty tree, and its sent,
   * drafts and trash folders are resolved once, right here, from what
   * the store says it has. */
  if (CAMEL_IS_OFFLINE_STORE (self->mail->service) && store_has_cache (self->mail->service)) {
    g_autoptr (GError) offline_error = NULL;

    if (!camel_offline_store_set_online_sync (CAMEL_OFFLINE_STORE (self->mail->service),
                                              FALSE, NULL, &offline_error))
      g_warning ("%s: Could not start offline: %s", G_STRFUNC, offline_error->message);
  }

  transport_extension = e_source_get_extension (self->mail->transport_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);
  self->mail->transport = CAMEL_TRANSPORT (camel_session_add_service (CAMEL_SESSION (stamp_session_get_default ()),
                                                                      e_source_get_uid (self->mail->transport_source),
                                                                      e_source_backend_get_backend_name (E_SOURCE_BACKEND (transport_extension)),
                                                                      CAMEL_PROVIDER_TRANSPORT, &error));
  if (!self->mail->transport)
    g_warning ("%s: Failed to add transport: %s", G_STRFUNC, error ? error->message : "");

  data = g_new0 (MailEnableData, 1);
  data->account = self;
  data->ctx = ctx;
  data->pending = 2;

  self->folder_index = stamp_folder_index_new (CAMEL_STORE (self->mail->service));

  if (CAMEL_IS_OFFLINE_STORE (self->mail->service))
    g_signal_connect_object (self->mail->service, "notify::connection-status",
                             G_CALLBACK (on_store_connection_status_changed), self, G_CONNECT_DEFAULT);

  camel_store_get_folder_info (CAMEL_STORE (self->mail->service), NULL,
                               CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL,
                               G_PRIORITY_DEFAULT, NULL,
                               on_folder_info_for_sent_drafts, data);

  camel_store_get_junk_folder (CAMEL_STORE (self->mail->service),
                               G_PRIORITY_DEFAULT, NULL,
                               on_junk_folder_ready, data);

  if (g_strcmp0 (e_source_backend_get_backend_name (E_SOURCE_BACKEND (extension)), "microsoft365") == 0) {
    /* Microsoft 365 categories fetch via Graph API. Asked for
     * asynchronously: this is a round trip to Graph, and done
     * synchronously it holds the main loop -- and so the whole window --
     * for as long as it takes. Nothing needs the categories to draw;
     * they colour rows and fill the category filter once they arrive. */
    stamp_m365_get_categories_async (self->mail->source, self->cancellable, on_categories_ready, g_object_ref (self));
  }
}

void
stamp_account_init_async (StampAccount        *self,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
  g_autoptr (GTask) task = g_task_new (self, cancellable, callback, user_data);
  gboolean mail_enabled = self->mail && e_source_get_enabled (self->mail->source);
  InitContext *ctx;
  guint pending = 0;

  g_task_set_source_tag (task, stamp_account_init_async);
  g_task_set_task_data (task, self, NULL);

  if (mail_enabled)
    pending++;

  pending += self->address_books->len;

  if (pending == 0) {
    g_debug ("%s: No active services (%s)", G_STRFUNC, self->display_name);
    g_task_return_boolean (task, TRUE);
    return;
  }
  g_debug ("%s: %d pending", G_STRFUNC, pending);

  ctx = g_new0 (InitContext, 1);
  ctx->account = self;
  ctx->parent_task = g_object_ref (task);
  ctx->pending = pending + 1;
  g_mutex_init (&ctx->lock);

  if (mail_enabled)
    stamp_account_enable_mail_async (self, ctx);

  for (guint i = 0; i < self->address_books->len; i++) {
    StampContactsService *service = g_ptr_array_index (self->address_books, i);
    gboolean enabled;

    enabled = !is_goa_disabled (self, service->source);
    if (!enabled) {
      g_debug ("%s: Address book '%s' in account '%s' disabled", G_STRFUNC, e_source_get_display_name (service->source), self->display_name);
      service->enabled = FALSE;
      g_mutex_lock (&ctx->lock);
      ctx->pending--;
      g_mutex_unlock (&ctx->lock);
      continue;
    }
    g_debug ("%s: Address book '%s' in account '%s' enabled", G_STRFUNC, e_source_get_display_name (service->source), self->display_name);

    e_book_client_connect (service->source, 10, cancellable, on_book_client_ready, ctx);
  }

  init_context_finish_one (ctx);
}

gboolean
stamp_account_init_finish (GAsyncResult  *res,
                           GError       **error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}

static
StampContactsService *
find_contacts_by_source (StampAccount *self,
                         ESource      *source)
{
  for (guint i = 0; i < self->address_books->len; i++) {
    StampContactsService *book = g_ptr_array_index (self->address_books, i);

    if (book->source == source)
      return book;
  }

  return NULL;
}

static void
on_book_client_connect (GObject      *src,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  StampAccount *self = STAMP_ACCOUNT (user_data);
  StampContactsService *service;
  ESource *source;
  g_autoptr (GError) error = NULL;
  g_autoptr (EClient) client = e_book_client_connect_finish (res, &error);

  if (error) {
    g_warning ("%s: EBookClient failed: %s", G_STRFUNC, error->message);
    return;
  }

  source = e_client_get_source (client);
  service = find_book_by_source (self, source);
  if (service) {
    service->client = E_BOOK_CLIENT (g_steal_pointer (&client));
    service->enabled = TRUE;
    g_debug ("%s: Book '%s' connected", G_STRFUNC, e_source_get_display_name (service->source));

    stamp_disk_cache_purge_negative (self->photo_cache);
    g_signal_emit (self, signals[BOOK_ADDED], 0, self, service);
  }
}

void
stamp_account_contacts_changed (StampAccount *self,
                                ESource      *source)
{
  StampContactsService *cal = find_contacts_by_source (self, source);
  gboolean enabled = e_source_get_enabled (source);

  if (!cal)
    return;

  if (!enabled && cal->client) {
    g_debug ("%s: Address book '%s' from '%s' disabled", G_STRFUNC, e_source_get_display_name (source), self->display_name);
    g_signal_emit (self, signals[BOOK_REMOVED], 0, self, cal);
    g_ptr_array_remove (self->address_books, cal);
    e_client_cancel_all (E_CLIENT (cal->client));
    g_clear_object (&cal->client);
  } else if (enabled && !cal->client) {
    g_debug ("%s: Address book '%s' from '%s' enabled", G_STRFUNC, e_source_get_display_name (source), self->display_name);

    e_book_client_connect (source, 10, NULL, on_book_client_connect, self);
  }
}

void
stamp_account_mail_changed (StampAccount *self,
                            ESource      *source)
{
  gboolean enabled = e_source_get_enabled (source);

  if (!enabled) {
    g_debug ("%s: Mail '%s' from '%s' disabled", G_STRFUNC, e_source_get_display_name (source), self->display_name);
    g_signal_emit (self, signals[MAIL_REMOVED], 0, self, self->mail);
    camel_service_disconnect_sync (self->mail->service, TRUE, NULL, NULL);
    g_clear_object (&self->mail->service);
    g_clear_object (&self->mail->transport);
  } else {
    g_debug ("%s: Mail '%s' from '%s' enabled", G_STRFUNC, e_source_get_display_name (source), self->display_name);

    stamp_account_enable_mail_async (self, NULL);
  }
}

void
stamp_account_set_name (StampAccount *self,
                        const gchar  *name)
{
  if (!name || self->display_name == name)
    return;

  g_set_str (&self->display_name, name);
}

void
stamp_account_add_mail_transport (StampAccount *self,
                                  ESource      *source)
{
  if (!self->mail)
    self->mail = g_new0 (StampMailService, 1);
  self->mail->transport_source = g_object_ref (source);
}

void
stamp_account_add_mail_identity (StampAccount *self,
                                 ESource      *source)
{
  ESourceMailIdentity *identity = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_IDENTITY);
  const gchar *name = e_source_mail_identity_get_name (identity);
  const gchar *address = e_source_mail_identity_get_address (identity);
  GHashTable *aliases = e_source_mail_identity_get_aliases_as_hash_table (identity);

  g_debug ("%s: Setting own address to name %s, address %s, aliases %p", G_STRFUNC, name, address, aliases);
  if (!self->mail)
    self->mail = g_new0 (StampMailService, 1);

  self->mail->address = camel_internet_address_new ();
  camel_internet_address_add (self->mail->address, name, address);

  self->mail->aliases = aliases;
  self->mail->identity_source = g_object_ref (source);
}

static gchar *
alias_to_string (GHashTable *aliases)
{
  GString *str = g_string_new ("");
  GList *keys = g_hash_table_get_keys (aliases);
  GList *iter;

  for (iter = keys; iter; iter = g_list_next (iter)) {
    const gchar *mail = iter->data;
    const gchar *name = g_hash_table_lookup (aliases, mail);
    g_autofree char *encoded = NULL;

    encoded = camel_internet_address_encode_address (NULL, name, mail);
    if (encoded && *encoded) {
      if (str->len > 0)
        g_string_append (str, ",");

      g_string_append (str, encoded);
    }
  }

  g_list_free (keys);

  return g_string_free_and_steal (str);
}

void
stamp_mail_service_set_aliases (StampMailService *self,
                                GHashTable       *aliases)
{
  ESource *source;
  ESourceMailIdentity *identity;
  g_autofree char *alias_str = NULL;
  g_autoptr (GError) error = NULL;

  source = self->identity_source;
  if (!source)
    return;

  identity = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_IDENTITY);
  alias_str = alias_to_string (aliases);
  e_source_mail_identity_set_aliases (identity, alias_str);

  e_source_registry_commit_source_sync (self->registry, source, NULL, &error);
  if (error)
    g_warning ("Could not commit alias: %s", error->message);

  self->aliases = e_source_mail_identity_get_aliases_as_hash_table (identity);
}

CamelInternetAddress *
stamp_account_get_address (StampAccount *self)
{
  if (!self->mail)
    return NULL;

  return self->mail->address;
}

static void
stamp_account_append_to_sent_folder (StampAccount      *self,
                                     gboolean           server_saved,
                                     CamelMimeMessage  *message,
                                     GCancellable      *cancellable,
                                     GError           **error)
{
  CamelFolder *sent_folder;
  g_autoptr (CamelMessageInfo) info = NULL;
  gboolean ok;

  if (server_saved)
    return;

  sent_folder = stamp_account_get_mail_sent_folder (self);
  if (!sent_folder) {
    g_warning ("%s: No sent folder, abort", G_STRFUNC);
    return;
  }

  info = camel_message_info_new (NULL);
  camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

  ok = camel_folder_append_message_sync (sent_folder, message, info, NULL, cancellable, error);
  if (ok)
    camel_folder_synchronize_sync (sent_folder, FALSE, cancellable, NULL);
}

gpointer
stamp_account_send_mail_finish (StampAccount  *account,
                                GAsyncResult  *result,
                                GError       **error)
{
  gpointer ptr;

  g_assert (g_task_is_valid (result, account));
  g_assert (error && !*error);

  ptr = g_task_propagate_pointer (G_TASK (result), error);

  return ptr;
}

typedef struct {
  CamelMimeMessage *message;
  CamelInternetAddress *sender;
  CamelInternetAddress *recipient;
  gboolean pgp_sign;
  gboolean pgp_encrypt;
} SendMailData;

static void
send_mail_data_free (gpointer user_data)
{
  SendMailData *data = user_data;

  g_clear_object (&data->message);
  g_clear_object (&data->sender);
  g_clear_object (&data->recipient);
  g_clear_pointer (&data, g_free);
}

static void
send_mail (GTask        *task,
           gpointer      source_object,
           gpointer      task_data,
           GCancellable *cancellable)
{
  StampAccount *self = STAMP_ACCOUNT (source_object);
  SendMailData *data = task_data;
  g_autoptr (GError) error = NULL;
  gboolean out_saved;

  if (!camel_service_connect_sync (CAMEL_SERVICE (self->mail->transport), cancellable, &error)) {
    g_warning ("%s: Could not connect to transport service '%s': %s", G_STRFUNC, e_source_get_display_name (self->mail->transport_source), error->message);
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  if (!camel_transport_send_to_sync (self->mail->transport, data->message, CAMEL_ADDRESS (data->sender), CAMEL_ADDRESS (data->recipient), &out_saved, cancellable, &error)) {
    g_warning ("%s: Could not send to transport service '%s': %s", G_STRFUNC, e_source_get_display_name (self->mail->transport_source), error->message);
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  stamp_account_append_to_sent_folder (self, out_saved, data->message, NULL, NULL);

  if (!camel_service_disconnect_sync (CAMEL_SERVICE (self->mail->transport), TRUE, cancellable, &error)) {
    g_warning ("%s: Could not disconnect from transport service '%s': %s", G_STRFUNC, e_source_get_display_name (self->mail->transport_source), error->message);
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  g_task_return_boolean (task, TRUE);
}

void
stamp_account_send_mail (StampAccount         *self,
                         CamelMimeMessage     *message,
                         CamelInternetAddress *sender,
                         CamelInternetAddress *recipient,
                         gboolean              pgp_sign,
                         gboolean              pgp_encrypt,
                         GCancellable         *cancellable,
                         GAsyncReadyCallback   callback,
                         gpointer              user_data)
{
  g_autoptr (GTask) task = g_task_new (self, cancellable, callback, user_data);
  SendMailData *data = g_new0 (SendMailData, 1);

  data->message = g_object_ref (message);
  data->sender = g_object_ref (sender);
  data->recipient = g_object_ref (recipient);
  data->pgp_sign = pgp_sign;
  data->pgp_encrypt = pgp_encrypt;

  g_task_set_source_tag (task, stamp_account_send_mail);
  g_task_set_task_data (task, data, send_mail_data_free);
  g_task_run_in_thread (task, send_mail);
}

StampMailService *
stamp_account_get_mail_service (StampAccount *self)
{
  return self->mail;
}

CamelService *
stamp_mail_service_get_service (StampMailService *self)
{
  return self->service;
}

CamelTransport *
stamp_mail_service_get_transport (StampMailService *self)
{
  return self->transport;
}

gboolean
stamp_mail_service_get_enabled (StampMailService *self)
{
  return self->enabled;
}

ESource *
stamp_mail_service_get_source (StampMailService *self)
{
  return self->source;
}

ESource *
stamp_mail_service_get_transport_source (StampMailService *self)
{
  return self->transport_source;
}

ESource *
stamp_mail_service_get_identity_source (StampMailService *self)
{
  return self->identity_source;
}

gboolean
stamp_contacts_service_get_enabled (StampContactsService *self)
{
  return self->enabled;
}

EBookClient *
stamp_contacts_service_get_client (StampContactsService *self)
{
  return self->client;
}

ESource *
stamp_contacts_service_get_source (StampContactsService *self)
{
  return self->source;
}

static void
on_folder_synchronized (GObject      *source,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  CamelFolder *folder = CAMEL_FOLDER (source);
  g_autoptr (GError) error = NULL;

  if (!camel_folder_synchronize_finish (folder, res, &error)) {
    g_warning ("%s: Could not synchronize folder %s: %s", G_STRFUNC, camel_folder_get_display_name (folder), error->message);
  }
}

gchar *
stamp_account_save_draft (StampAccount         *self,
                          const gchar          *draft_uid,
                          CamelMimeMessage     *message,
                          CamelInternetAddress *sender,
                          CamelInternetAddress *recipient)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelMessageInfo) info = camel_message_info_new (NULL);
  gchar *uid = NULL;

  if (!self->mail->drafts_folder) {
    g_warning ("%s: No drafts folder", G_STRFUNC);
    return NULL;
  }

  camel_message_info_set_flags (info, CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_SEEN);
  if (!camel_folder_append_message_sync (self->mail->drafts_folder, message, info, &uid, self->cancellable, &error)) {
    if (error)
      g_warning ("%s: Could not append message to drafts folder: %s", G_STRFUNC, error->message);

    return NULL;
  }

  if (draft_uid) {
    camel_folder_delete_message (self->mail->drafts_folder, draft_uid);
    camel_folder_synchronize (self->mail->drafts_folder, TRUE, G_PRIORITY_DEFAULT, self->cancellable, on_folder_synchronized, self);
  }

  return uid;
}

void
stamp_account_remove_draft (StampAccount *self,
                            const gchar  *uid)
{
  g_autoptr (CamelMessageInfo) info = NULL;
  g_autoptr (GError) error = NULL;

  if (!uid) {
    g_warning ("%s: Called with uid = NULL", G_STRFUNC);
    return;
  }

  camel_folder_set_message_flags (self->mail->drafts_folder, uid, CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED);
  camel_folder_synchronize (self->mail->drafts_folder, TRUE, G_PRIORITY_DEFAULT, self->cancellable, on_folder_synchronized, self);
}

typedef struct {
  StampAccount *account;
  gchar *draft_uid;
  CamelMimeMessage *message;
  CamelInternetAddress *sender;
  CamelInternetAddress *recipient;
} SaveDraftData;

static void
save_draft_data_free (gpointer user_data)
{
  SaveDraftData *data = user_data;

  g_clear_object (&data->account);
  g_clear_pointer (&data->draft_uid, g_free);
  g_clear_object (&data->message);
  g_clear_object (&data->sender);
  g_clear_object (&data->recipient);
  g_clear_pointer (&data, g_free);
}

static void
save_draft_thread (GTask        *task,
                   gpointer      source_object,
                   gpointer      task_data,
                   GCancellable *cancellable)
{
  SaveDraftData *data = task_data;
  gchar *uid;

  uid = stamp_account_save_draft (data->account,
                                  data->draft_uid,
                                  data->message,
                                  data->sender,
                                  data->recipient);

  if (uid)
    g_task_return_pointer (task, uid, g_free);
  else
    g_task_return_pointer (task, NULL, NULL);
}

void
stamp_account_save_draft_async (StampAccount         *self,
                                const gchar          *draft_uid,
                                CamelMimeMessage     *message,
                                CamelInternetAddress *sender,
                                CamelInternetAddress *recipient,
                                GCancellable         *cancellable,
                                GAsyncReadyCallback   callback,
                                gpointer              user_data)
{
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  SaveDraftData *data = g_new0 (SaveDraftData, 1);

  data->account = g_object_ref (self);
  data->draft_uid = g_strdup (draft_uid);
  data->message = g_object_ref (message);
  data->sender = g_object_ref (sender);
  data->recipient = g_object_ref (recipient);

  g_task_set_source_tag (task, stamp_account_save_draft_async);
  g_task_set_task_data (task, data, save_draft_data_free);
  g_task_run_in_thread (task, save_draft_thread);
}

gchar *
stamp_account_save_draft_async_finish (StampAccount  *self,
                                       GAsyncResult  *result,
                                       GError       **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

typedef struct {
  StampAccount *account;
  gchar *uid;
} RemoveDraftData;

static void
remove_draft_data_free (gpointer user_data)
{
  RemoveDraftData *data = user_data;

  g_clear_object (&data->account);
  g_clear_pointer (&data->uid, g_free);
  g_clear_pointer (&data, g_free);
}

static void
remove_draft_thread (GTask        *task,
                     gpointer      source_object,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
  RemoveDraftData *data = task_data;

  stamp_account_remove_draft (data->account, data->uid);

  g_task_return_boolean (task, TRUE);
}

void
stamp_account_remove_draft_async (StampAccount        *self,
                                  const gchar         *uid,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  RemoveDraftData *data = g_new0 (RemoveDraftData, 1);

  data->account = g_object_ref (self);
  data->uid = g_strdup (uid);

  g_task_set_source_tag (task, stamp_account_remove_draft_async);
  g_task_set_task_data (task, data, remove_draft_data_free);
  g_task_run_in_thread (task, remove_draft_thread);
}

gboolean
stamp_account_remove_draft_async_finish (StampAccount  *self,
                                         GAsyncResult  *result,
                                         GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

CamelFolder *
stamp_account_get_mail_trash_folder (StampAccount *self)
{
  return self->mail->trash_folder;
}

CamelFolder *
stamp_account_get_mail_sent_folder (StampAccount *self)
{
  return self->mail->sent_folder;
}

CamelFolder *
stamp_account_get_mail_drafts_folder (StampAccount *self)
{
  return self->mail->drafts_folder;
}

void
stamp_account_clear_negative_photo_cache (StampAccount *self)
{
  stamp_disk_cache_purge_negative (self->photo_cache);
}

StampFolderIndex *
stamp_account_get_folder_index (StampAccount *self)
{
  g_return_val_if_fail (STAMP_IS_ACCOUNT (self), NULL);

  return self->folder_index;
}

StampCategory *
stamp_account_find_category (StampAccount *self,
                             const gchar  *name)
{
  g_autoptr (GString) str = NULL;

  if (!self->categories)
    return NULL;

  str = g_string_new (name);
  g_string_replace (str, "_", " ", 0);

  for (GList *iter = self->categories; iter && iter->data; iter = g_list_next (iter)) {
    StampCategory *cat = iter->data;

    if (g_strcmp0 (stamp_category_get_name (cat), str->str) == 0)
      return g_object_ref (cat);
  }

  return NULL;
}

GList *
stamp_account_get_categories (StampAccount *self)
{
  return self->categories;
}

GHashTable *
stamp_mail_service_get_aliases (StampMailService *self)
{
  return self->aliases;
}

CamelFolder *
stamp_account_get_mail_junk_folder (StampAccount *self)
{
  return self->mail->junk_folder;
}
