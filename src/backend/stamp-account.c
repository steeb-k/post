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
#include "stamp-photo-cache.h"
#include "stamp-session.h"

struct _StampMailService {
  CamelService *service;

  CamelSession *session;
  CamelTransport *transport;
  CamelFolder *trash_folder;
  CamelFolder *sent_folder;
  CamelFolder *drafts_folder;
  ESource *source;
  ESource *transport_source;
  CamelInternetAddress *address;
  gboolean enabled;
};

struct _StampContactsService {
  EBookClient *client;
  ESource *source;
  gboolean enabled;
};

struct _StampCalendarService {
  ECalClient *client;
  ESource *source;
  gboolean enabled;
};

struct _StampAccount {
  GObject parent_instance;

  char *uid;
  char *display_name;
  ESourceRegistry *registry;

  ESource *collection;
  StampMailService *mail;
  GPtrArray *calendars;
  GPtrArray *address_books;

  GCancellable *cancellable;

  StampPhotoCache *photo_cache;
};

G_DEFINE_FINAL_TYPE (StampAccount, stamp_account, G_TYPE_OBJECT)

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
  CALENDAR_ADDED,
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

  self->calendars = g_ptr_array_new_with_free_func (g_free);
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
  g_clear_pointer (&service, g_free);
}

static void
stamp_account_dispose (GObject *object)
{
  StampAccount *self = STAMP_ACCOUNT (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_pointer (&self->uid, g_free);
  g_clear_pointer (&self->display_name, g_free);
  g_clear_object (&self->collection);
  g_clear_object (&self->registry);

  g_clear_pointer (&self->mail, stamp_account_mail_service_free);
  g_clear_pointer (&self->calendars, g_ptr_array_unref);
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
  signals[CALENDAR_ADDED] = g_signal_new ("calendar-added", G_OBJECT_CLASS_TYPE (klass),
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

const char *
stamp_account_get_name (StampAccount *self)
{
  return self->display_name;
}

const char *
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
    for (int idx = 0; idx < self->address_books->len; idx++) {
      const char *name;

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

void
stamp_account_get_photo (StampAccount *self,
                         const char   *sender,
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
                               const char          *search_text,
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

  g_print ("%s: Query for %s in %s\n", G_STRFUNC, search_text, e_source_get_display_name (e_client_get_source (E_CLIENT (book_client))));

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
}

void
stamp_account_add_calendar (StampAccount *self,
                            ESource      *source)
{
  StampCalendarService *cal = g_new0 (StampCalendarService, 1);

  cal->source = g_object_ref (source);
  g_ptr_array_add (self->calendars, cal);
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

static StampCalendarService *
find_calendar_by_source (StampAccount *self,
                         ESource      *source)
{
  if (!self->calendars)
    return NULL;

  for (guint idx = 0; idx < self->calendars->len; idx++) {
    StampCalendarService *service = g_ptr_array_index (self->calendars, idx);

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
      g_print ("Book '%s' connected\n", e_source_get_display_name (service->source));
      g_signal_emit (ctx->account, signals[BOOK_ADDED], 0, ctx->account, service);
    } else {
      g_clear_object (&client);
    }
  }

  init_context_finish_one (ctx);
}

static void
on_cal_client_ready (GObject      *src,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  InitContext *ctx = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (EClient) client = e_cal_client_connect_finish (res, &error);

  if (error) {
    g_warning ("%s: ECalClient failed: %s", G_STRFUNC, error->message);
    init_context_set_error (ctx, g_steal_pointer (&error));
  } else {
    ESource *source = e_client_get_source (client);
    StampCalendarService *service = find_calendar_by_source (ctx->account, source);

    if (service) {
      service->client = E_CAL_CLIENT (g_object_ref (client));
      service->enabled = TRUE;
      g_print ("Calendar '%s' connected\n", e_source_get_display_name (service->source));
      g_signal_emit (ctx->account, signals[CALENDAR_ADDED], 0, ctx->account, service);
    } else {
      g_clear_object (&client);
    }
  }

  init_context_finish_one (ctx);
}

typedef enum {
  FIND_BY_FLAGS,
  FIND_BY_NAME,
} FindFolderType;

typedef struct {
  FindFolderType type;
  guint32 flags;
  const char *name;
} FindFolderData;

static gboolean
match_folder_info (CamelFolderInfo *fi,
                   const FindFolderData *data)
{
  if (data->type == FIND_BY_FLAGS)
    return (fi->flags & CAMEL_FOLDER_TYPE_MASK) == data->flags;

  if (data->type == FIND_BY_NAME && data->name)
    return fi->display_name && g_ascii_strcasecmp (fi->display_name, data->name) == 0;

  return FALSE;
}

static CamelFolderInfo *
find_folder_info_recursive (CamelFolderInfo  *fi,
                            const FindFolderData *data)
{
  while (fi) {
    if (match_folder_info (fi, data))
      return fi;

    if (fi->child) {
      CamelFolderInfo *found = find_folder_info_recursive (fi->child, data);
      if (found)
        return found;
    }

    fi = fi->next;
  }

  return NULL;
}

static CamelFolder *
open_folder_from_fi (CamelStore       *store,
                     CamelFolderInfo  *fi,
                     GError          **error)
{
  g_return_val_if_fail (fi != NULL, NULL);

  return camel_store_get_folder_sync (store, fi->full_name,
                                      0, NULL, error);
}

static CamelFolder *
find_sent_folder (CamelStore *store)
{
  g_autoptr (GError) error = NULL;
  CamelFolderInfo *root = NULL;
  g_autoptr (CamelFolderInfo) fi = NULL;
  CamelFolder *folder = NULL;

  root = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL, NULL, &error);
  if (error) {
    g_warning ("%s: get_folder_info: %s", G_STRFUNC, error->message);
    return NULL;
  }

  {
    FindFolderData data = { .type = FIND_BY_FLAGS, .flags = CAMEL_FOLDER_TYPE_SENT };
    fi = find_folder_info_recursive (root, &data);
  }
  if (fi) {
    folder = camel_store_get_folder_sync (store, fi->full_name, 0, NULL, &error);
    if (error) {
      g_warning ("%s: get_folder '%s': %s", G_STRFUNC, fi->full_name, error->message);
      folder = NULL;
    }

    if (folder)
      goto out;
  }

  /* Strategy 3 – well-known display names */
  {
    static const gchar *sent_names[] = {
      "Sent",
      "Sent Items",
      "Sent Messages",
      "Gesendete Elemente",
      "Gesendete Objekte",
      NULL
    };

    for (gint i = 0; sent_names[i]; i++) {
      FindFolderData data = { .type = FIND_BY_NAME, .name = sent_names[i] };
      fi = find_folder_info_recursive (root, &data);
      if (fi) {
        folder = open_folder_from_fi (store, fi, &error);
        if (error) {
          g_clear_error (&error);
          folder = NULL;
        }
        if (folder) goto out;
      }
    }
  }

out:
  return folder;
}

static gboolean
is_drafts_folder (CamelFolderInfo *fi)
{
  const char *name;

  if ((fi->flags & CAMEL_FOLDER_TYPE_MASK) == CAMEL_FOLDER_TYPE_DRAFTS)
    return TRUE;

  name = fi->display_name ? fi->display_name : fi->full_name;
  return g_ascii_strcasecmp (name, "Drafts") == 0;
}

static CamelFolder *
get_drafts_folder (CamelStore    *store,
                   GCancellable  *cancellable,
                   GError       **error)
{
  CamelFolderInfo *root = camel_store_get_folder_info_sync (store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, cancellable, error);
  CamelFolderInfo *fi;
  const gchar *drafts_path;
  CamelFolder *folder;

  if (!root)
    return NULL;

  fi = root;
  drafts_path = NULL;

  while (fi) {
    if (is_drafts_folder (fi)) {
      drafts_path = fi->full_name;
      break;
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

  /* Fallback falls kein \Drafts vom Server gemeldet */
  if (!drafts_path)
    drafts_path = "Drafts";

  folder = camel_store_get_folder_sync (
    store, drafts_path,
    0,
    cancellable, error);

  camel_folder_info_free (root);
  return folder;
}

static void
stamp_account_enable_mail (StampAccount *self)
{
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

  transport_extension = e_source_get_extension (self->mail->transport_source, E_SOURCE_EXTENSION_MAIL_TRANSPORT);
  self->mail->transport = CAMEL_TRANSPORT (camel_session_add_service (CAMEL_SESSION (stamp_session_get_default ()),
                                                                      e_source_get_uid (self->mail->transport_source),
                                                                      e_source_backend_get_backend_name (E_SOURCE_BACKEND (transport_extension)),
                                                                      CAMEL_PROVIDER_TRANSPORT, &error));

  self->mail->trash_folder = camel_store_get_trash_folder_sync (CAMEL_STORE (self->mail->service), NULL, &error);
  self->mail->sent_folder = find_sent_folder (CAMEL_STORE (self->mail->service));
  self->mail->drafts_folder = get_drafts_folder (CAMEL_STORE (self->mail->service), NULL, &error);
}

void
stamp_account_init_async (StampAccount        *self,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  gboolean mail_enabled = self->mail && e_source_get_enabled (self->mail->source);
  InitContext *ctx;
  guint pending = 1;

  g_task_set_task_data (task, self, NULL);
  if (mail_enabled) {
    /* pending += 2; */
  }

  pending += self->address_books->len;
  pending += self->calendars->len;

  if (pending == 0) {
    g_print ("%s: No active services (%s)\n", G_STRFUNC, self->display_name);
    g_task_return_boolean (task, TRUE);
    g_clear_object (&task);
    return;
  }
  g_print ("%s: %d pending\n", G_STRFUNC, pending);

  ctx = g_new0 (InitContext, 1);
  ctx->account = self;
  ctx->parent_task = task;
  ctx->pending = pending;
  g_mutex_init (&ctx->lock);

  if (mail_enabled) {
    stamp_account_enable_mail (self);
    g_signal_emit (self, signals[MAIL_ADDED], 0, self, self->mail->service);
  }

  for (guint i = 0; i < self->calendars->len; i++) {
    StampCalendarService *service = g_ptr_array_index (self->calendars, i);

    if (is_goa_disabled (self, service->source)) {
      g_print ("%s: Calendar '%s' in account '%s' disabled\n", G_STRFUNC, e_source_get_display_name (service->source), self->display_name);
      service->enabled = FALSE;
      g_mutex_lock (&ctx->lock);
      ctx->pending--;
      g_mutex_unlock (&ctx->lock);
      continue;
    }

    g_print ("%s: Connecting to calendar '%s' in account '%s'\n", G_STRFUNC, e_source_get_display_name (service->source), self->display_name);
#ifdef CALENDAR_ASYNC
    e_cal_client_connect (service->source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 10, cancellable, on_cal_client_ready, ctx);
#else
    {
      g_autoptr (GError) error = NULL;
      g_autoptr (EClient) client = e_cal_client_connect_sync (service->source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 10, cancellable, &error);
      if (error) {
        g_warning ("%s: ECalClient failed: %s", G_STRFUNC, error->message);
        init_context_set_error (ctx, g_steal_pointer (&error));
      } else {
        service->client = E_CAL_CLIENT (g_object_ref (client));
        service->enabled = TRUE;
        g_print ("Calendar '%s' connected\n", e_source_get_display_name (service->source));
        g_signal_emit (ctx->account, signals[CALENDAR_ADDED], 0, ctx->account, service);
      }
      init_context_finish_one (ctx);
    }
#endif
  }

  for (guint i = 0; i < self->address_books->len; i++) {
    StampContactsService *service = g_ptr_array_index (self->address_books, i);
    gboolean enabled;

    enabled = !is_goa_disabled (self, service->source);
    if (!enabled) {
      g_print ("%s: Address book '%s' in account '%s' disabled\n", G_STRFUNC, e_source_get_display_name (service->source), self->display_name);
      service->enabled = FALSE;
      g_mutex_lock (&ctx->lock);
      ctx->pending--;
      g_mutex_unlock (&ctx->lock);
      continue;
    }
    g_print ("%s: Address book '%s' in account '%s' enabled\n", G_STRFUNC, e_source_get_display_name (service->source), self->display_name);

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

void
stamp_account_contacts_changed (StampAccount *self,
                                ESource      *source)
{
  StampContactsService *cal = find_contacts_by_source (self, source);
  gboolean enabled = e_source_get_enabled (source);

  if (!cal)
    return;

  if (!enabled && cal->client) {
    g_print ("%s: Address book '%s' from '%s' disabled\n", G_STRFUNC, e_source_get_display_name (source), self->display_name);
    g_ptr_array_remove (self->address_books, cal);
    g_signal_emit (self, signals[BOOK_REMOVED], 0, self, cal);
    e_client_cancel_all (E_CLIENT (cal->client));
    g_clear_object (&cal->client);
  } else if (enabled && !cal->client) {
    g_print ("%s: Address book '%s' from '%s' enabled\n", G_STRFUNC, e_source_get_display_name (source), self->display_name);

    e_book_client_connect (source, 10, NULL, on_cal_client_ready, self);
  }
}

void
stamp_account_mail_changed (StampAccount *self,
                            ESource      *source)
{
  gboolean enabled = e_source_get_enabled (source);

  if (!enabled) {
    g_print ("%s: Mail '%s' from '%s' disabled\n", G_STRFUNC, e_source_get_display_name (source), self->display_name);
    g_signal_emit (self, signals[MAIL_REMOVED], 0, self, self->mail);
    camel_service_disconnect_sync (self->mail->service, TRUE, NULL, NULL);
    g_clear_object (&self->mail->service);
    g_clear_object (&self->mail->transport);
  } else if (enabled) {
    g_print ("%s: Mail '%s' from '%s' enabled\n", G_STRFUNC, e_source_get_display_name (source), self->display_name);
    stamp_account_enable_mail (self);
    g_signal_emit (self, signals[MAIL_ADDED], 0, self, self->mail->service);
  }
}

void
stamp_account_set_name (StampAccount *self,
                        const char   *name)
{
  if (!name || self->display_name == name)
    return;

  g_clear_pointer (&self->display_name, g_free);
  self->display_name = g_strdup (name);
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
  const char *name = e_source_mail_identity_get_name (identity);
  const char *address = e_source_mail_identity_get_address (identity);

  g_print ("%s: Setting own address to %s / %s\n", G_STRFUNC, name, address);
  if (!self->mail)
    self->mail = g_new0 (StampMailService, 1);

  self->mail->address = camel_internet_address_new ();
  camel_internet_address_add (self->mail->address, name, address);
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

  g_print ("%s: Append to sent folder %s\n", G_STRFUNC, camel_folder_get_display_name (sent_folder));
  info = camel_message_info_new (NULL);
  camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);

  ok = camel_folder_append_message_sync (sent_folder, message, info, NULL, cancellable, error);
  if (ok)
    camel_folder_synchronize_sync (sent_folder, FALSE, cancellable, NULL);

  g_print ("%s: EXIT\n", G_STRFUNC);
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
  StampAccount *account;
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

gboolean
stamp_calendar_service_get_enabled (StampCalendarService *self)
{
  return self->enabled;
}

ECalClient *
stamp_calendar_service_get_client (StampCalendarService *self)
{
  return self->client;
}

ESource *
stamp_calendar_service_get_source (StampCalendarService *self)
{
  return self->source;
}

char *
stamp_account_save_draft (StampAccount         *self,
                          const char           *draft_uid,
                          CamelMimeMessage     *message,
                          CamelInternetAddress *sender,
                          CamelInternetAddress *recipient)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelMessageInfo) info = camel_message_info_new (NULL);
  char *uid = NULL;

  if (error) {
    g_warning ("%s: Could not load draft folder: %s", G_STRFUNC, error->message);
    return NULL;
  }

  camel_message_info_set_flags (info, CAMEL_MESSAGE_DRAFT, CAMEL_MESSAGE_DRAFT);
  camel_folder_append_message_sync (self->mail->drafts_folder, message, info, &uid, NULL, NULL);

  if (draft_uid) {
    g_print ("%s: Previous draft, removing old one %s\n", G_STRFUNC, draft_uid);
    camel_folder_delete_message (self->mail->drafts_folder, draft_uid);
    camel_folder_refresh_info_sync (self->mail->drafts_folder, NULL, NULL);
    camel_folder_expunge_sync (self->mail->drafts_folder, NULL, &error);
    if (error)
      g_warning ("%s: Could not expunge folder: %s", G_STRFUNC, error->message);
  }

  return uid;
}

void
stamp_account_remove_draft (StampAccount *self,
                            const char   *uid)
{
  g_autoptr (GError) error = NULL;

  camel_folder_delete_message (self->mail->drafts_folder, uid);
  camel_folder_refresh_info_sync (self->mail->drafts_folder, NULL, NULL);
  camel_folder_expunge_sync (self->mail->drafts_folder, NULL, &error);
  if (error)
    g_warning ("%s: Could not expunge folder: %s", G_STRFUNC, error->message);
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
  char *uid;

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
                                const char           *draft_uid,
                                CamelMimeMessage     *message,
                                CamelInternetAddress *sender,
                                CamelInternetAddress *recipient,
                                GCancellable         *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  SaveDraftData *data = g_new0 (SaveDraftData, 1);

  data->account = g_object_ref (self);
  data->draft_uid = g_strdup (draft_uid);
  data->message = g_object_ref (message);
  data->sender = g_object_ref (sender);
  data->recipient = g_object_ref (recipient);

  g_task_set_task_data (task, data, save_draft_data_free);
  g_task_run_in_thread (task, save_draft_thread);
}

char *
stamp_account_save_draft_async_finish (StampAccount  *self,
                                       GAsyncResult  *result,
                                       GError       **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

typedef struct {
  StampAccount *account;
  char *uid;
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
                                 const char           *uid,
                                 GCancellable         *cancellable,
                                 GAsyncReadyCallback   callback,
                                 gpointer             user_data)
{
  GTask *task = g_task_new (self, cancellable, callback, user_data);
  RemoveDraftData *data = g_new0 (RemoveDraftData, 1);

  data->account = g_object_ref (self);
  data->uid = g_strdup (uid);

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
