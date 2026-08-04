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

#include "stamp-book-account-item.h"

#include <camel/camel.h>

#include "stamp-account.h"
#include "stamp-book-item.h"
#include "stamp-item.h"

struct _StampBookAccountItem {
  StampItem parent_instance;
};

G_DEFINE_FINAL_TYPE (StampBookAccountItem, stamp_book_account_item, STAMP_TYPE_ITEM);

enum {
  ACCOUNT_ITEM_CHANGED,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

static void
on_book_added (GObject              *source,
               StampAccount         *account,
               StampContactsService *service,
               gpointer              user_data)
{
  StampBookAccountItem *self = STAMP_BOOK_ACCOUNT_ITEM (user_data);
  EBookClient *client = stamp_contacts_service_get_client (service);
  ESource *src;
  GListStore *list_store;

  if (!client)
    return;

  src = e_client_get_source (E_CLIENT (client));
  g_debug ("%s: New book added %s", G_STRFUNC, e_source_get_display_name (src));

  list_store = stamp_item_get_list_store (STAMP_ITEM (self));
  g_list_store_append (list_store, stamp_book_item_new (account, E_CLIENT (client)));
}

static gboolean
equal (gconstpointer a,
       gconstpointer b)
{
  StampBookItem *item_a = (StampBookItem *)a;
  StampBookItem *item_b = (StampBookItem *)b;

  if (stamp_item_get_account (STAMP_ITEM (item_a)) != stamp_item_get_account (STAMP_ITEM (item_b)))
    return FALSE;

  if (stamp_book_item_get_client (STAMP_BOOK_ITEM (item_a)) != stamp_book_item_get_client (STAMP_BOOK_ITEM (item_b)))
    return FALSE;

  return TRUE;
}

static void
on_book_removed (GObject              *source,
                 StampAccount         *account,
                 StampContactsService *service,
                 gpointer              user_data)
{
  StampBookAccountItem *self = STAMP_BOOK_ACCOUNT_ITEM (user_data);
  EBookClient *client = stamp_contacts_service_get_client (service);
  ESource *src = e_client_get_source (E_CLIENT (client));
  StampBookItem *item = stamp_book_item_new (account, E_CLIENT (client));
  GListStore *list_store;
  guint position = 0;

  g_debug ("%s: Book removed %s", G_STRFUNC, e_source_get_display_name (src));

  list_store = stamp_item_get_list_store (STAMP_ITEM (self));

  if (g_list_store_find_with_equal_func (list_store, item, equal, &position))
    g_list_store_remove (list_store, position);
}

static void
stamp_book_account_item_constructed (GObject *object)
{
  StampBookAccountItem *self = STAMP_BOOK_ACCOUNT_ITEM (object);
  StampAccount *account;
  GPtrArray *books;

  G_OBJECT_CLASS (stamp_book_account_item_parent_class)->constructed (object);

  account = stamp_item_get_account (STAMP_ITEM (self));
  books = stamp_account_get_books (account);

  stamp_item_set_name (STAMP_ITEM (self), stamp_account_get_name (account));
  stamp_item_set_list_store_type (STAMP_ITEM (self), STAMP_TYPE_BOOK_ITEM);

  /* Fill existing books */
  for (guint idx = 0; idx < books->len; idx++) {
    StampContactsService *service = books->pdata[idx];
    GListStore *list_store;

    if (!stamp_contacts_service_get_enabled (service))
      continue;

    list_store = stamp_item_get_list_store (STAMP_ITEM (self));
    g_list_store_append (list_store, stamp_book_item_new (account, E_CLIENT (stamp_contacts_service_get_client (service))));
  }

  g_signal_connect_object (account, "book-added", G_CALLBACK (on_book_added), self, G_CONNECT_DEFAULT);
  g_signal_connect_object (account, "book-removed", G_CALLBACK (on_book_removed), self, G_CONNECT_DEFAULT);
}

static void
stamp_book_account_item_dispose (GObject *object)
{
  /* StampBookAccountItem *self = STAMP_BOOK_ACCOUNT_ITEM (object); */

  G_OBJECT_CLASS (stamp_book_account_item_parent_class)->dispose (object);
}

void
stamp_book_account_item_class_init (StampBookAccountItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = stamp_book_account_item_constructed;
  object_class->dispose = stamp_book_account_item_dispose;

  signals[ACCOUNT_ITEM_CHANGED] = g_signal_new ("account-item-changed", G_OBJECT_CLASS_TYPE (klass),
                                                G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                                0, NULL, NULL, NULL,
                                                G_TYPE_NONE,
                                                0);
}

void
stamp_book_account_item_init (StampBookAccountItem *self)
{
}

StampBookAccountItem *
stamp_book_account_item_new (StampAccount *account)
{
  return g_object_new (STAMP_TYPE_BOOK_ACCOUNT_ITEM, "account", account, NULL);
}
