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

#include "stamp-composer-from.h"

#include "stamp-account.h"

struct _StampComposerFrom {
  GObject parent_instance;

  StampAccount *account;
  char *name;
  char *mail;
};

G_DEFINE_FINAL_TYPE (StampComposerFrom, stamp_composer_from, G_TYPE_OBJECT);

static void
stamp_composer_from_init (StampComposerFrom *self)
{
}

static void
stamp_composer_from_dispose (GObject *object)
{
  StampComposerFrom *self = STAMP_COMPOSER_FROM (object);

  g_clear_object (&self->account);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->mail, g_free);

  G_OBJECT_CLASS (stamp_composer_from_parent_class)->dispose (object);
}

static void
stamp_composer_from_class_init (StampComposerFromClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_composer_from_dispose;
}

StampComposerFrom *
stamp_composer_from_new (StampAccount *account,
                         const char   *name,
                         const char   *mail)
{
  StampComposerFrom *self = g_object_new (STAMP_TYPE_COMPOSER_FROM, NULL);

  g_set_object (&self->account, account);
  g_set_str (&self->name, name);
  g_set_str (&self->mail, mail);

  return self;
}

StampAccount *
stamp_composer_from_get_account (StampComposerFrom *self)
{
  return self->account;
}

const char *
stamp_composer_from_get_name (StampComposerFrom *self)
{
  return self->name;
}

const char *
stamp_composer_from_get_mail (StampComposerFrom *self)
{
  return self->mail;
}
