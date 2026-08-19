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

#pragma once

#include <camel/camel.h>
#include <glib.h>

G_BEGIN_DECLS

/*
 * The subjects the clustered view hands to Camel in place of the real
 * ones.
 *
 * Camel can thread by subject on its own, but only by the whole subject
 * and with no sense of time, so a subject an automated sender reuses
 * every night would collect years of mail into a single row. The way
 * around it is to thread on a subject of our own making: the normalized
 * subject with a number after it that goes up whenever two mails
 * sharing that subject are further apart than @window_days. Mails on
 * either side of such a gap end up with different subjects as far as
 * Camel is concerned, and so land in different clusters.
 *
 * Mails whose subject normalizes to nothing get a string of their own
 * that nothing else can match, so they never cluster.
 *
 * @items holds #CamelMessageInfo. The returned table borrows them as
 * keys, so it must not outlive @items.
 *
 * Returns: (transfer full): #CamelMessageInfo to the subject to thread
 *   it under.
 */
GHashTable *
stamp_cluster_build_subjects (GPtrArray *items,
                              guint      window_days);

G_END_DECLS
