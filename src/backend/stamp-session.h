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

#pragma once

#include <camel/camel.h>
#include <gtk/gtk.h>

#include "stamp-signature.h"

G_BEGIN_DECLS

#define STAMP_TYPE_SESSION (stamp_session_get_type ())
G_DECLARE_FINAL_TYPE (StampSession, stamp_session, STAMP, SESSION, CamelSession);

StampSession *
stamp_session_get_default (void);

GList *
stamp_session_get_accounts (StampSession *self);

gboolean
stamp_session_get_accounts_loaded (StampSession *self);

GList *
stamp_session_get_signatures (StampSession *self);

ESourceRegistry *
stamp_session_get_registry (StampSession *self);

void
stamp_session_remove_signature (StampSession   *self,
                                StampSignature *signature);

StampSignature *
stamp_session_create_signature (StampSession *self,
                                const gchar  *name,
                                const gchar  *content,
                                const gchar  *mime_type);

typedef struct _StampAccount StampAccount;

typedef struct _StampMailAccountParams {
  const gchar *display_name;
  const gchar *identity_name;
  const gchar *address;
  const gchar *reply_to;
  const gchar *imap_host;
  guint16 imap_port;
  const gchar *imap_security;
  const gchar *imap_user;
  const gchar *smtp_host;
  guint16 smtp_port;
  const gchar *smtp_security;
  const gchar *smtp_user;
} StampMailAccountParams;

gboolean
stamp_session_create_mail_account (StampSession                  *self,
                                   const StampMailAccountParams  *params,
                                   GError                       **error);

gboolean
stamp_session_remove_account (StampSession  *self,
                              StampAccount  *account,
                              GError       **error);

G_END_DECLS

