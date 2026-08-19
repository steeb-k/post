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

#include "stamp-cluster.h"

#include "stamp-helper.h"

/*
 * Between the number that separates one run of a subject from the next
 * and the subject itself. It cannot show up in a subject of its own
 * accord, which is what keeps "Report 2" from being read as the third
 * run of "Report".
 */
#define CLUSTER_SEPARATOR "\x1f"

/*
 * And why the number goes first. Camel still runs its own stripping
 * over whatever subject it is handed, looking for leading whitespace, a
 * "[list]" tag and a "Re:". Starting every made-up subject with a "#"
 * stops all of that on the first character, so Camel's rules cannot
 * quietly rejoin two runs this pass decided to separate -- and its
 * isspace()/isdigit() calls, which are undefined on the negative
 * char values a UTF-8 subject is full of, never reach the subject.
 */
#define CLUSTER_PREFIX "#"

/* How many mails one cluster may gather before it starts another. */
#define STAMP_CLUSTER_MAX_MEMBERS 50

/*
 * The date a mail clusters by.
 *
 * Deliberately the same rule Camel's own sort_node_cb() uses -- sent
 * date, falling back to received -- rather than the other way round.
 * Camel sorts the tree we are about to hand it by this, so measuring
 * the gaps by anything else would let a mail sort into one run while
 * being counted against another.
 */
static gint64
effective_date (const CamelMessageInfo *info)
{
  gint64 date = camel_message_info_get_date_sent ((CamelMessageInfo *)info);

  if (date <= 0)
    date = camel_message_info_get_date_received ((CamelMessageInfo *)info);

  return date > 0 ? date : 0;
}

static gint
compare_by_date (gconstpointer a,
                 gconstpointer b)
{
  gint64 date_a = effective_date (*(CamelMessageInfo **)a);
  gint64 date_b = effective_date (*(CamelMessageInfo **)b);

  if (date_a < date_b)
    return -1;

  return date_a > date_b ? 1 : 0;
}

GHashTable *
stamp_cluster_build_subjects (GPtrArray *items,
                              guint      window_days)
{
  g_autoptr (GHashTable) buckets = NULL;
  GHashTable *subjects;
  GHashTableIter iter;
  gpointer key;
  gpointer value;
  gint64 window;

  g_return_val_if_fail (items != NULL, NULL);


  subjects = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
  buckets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
  window = (gint64)window_days * 24 * 60 * 60;

  /* Gather the mails that share a subject. */
  for (guint idx = 0; idx < items->len; idx++) {
    CamelMessageInfo *info = g_ptr_array_index (items, idx);
    g_autofree gchar *normalized = NULL;
    GPtrArray *bucket;

    if (!info)
      continue;

    normalized = stamp_normalize_subject (camel_message_info_get_subject (info));

    /*
     * Nothing to match on. Give it a subject nothing else can share --
     * the separator cannot start a normalized subject, so this can
     * never collide with a real cluster.
     */
    if (!normalized) {
      g_hash_table_insert (subjects, info,
                           g_strconcat (CLUSTER_PREFIX "u" CLUSTER_SEPARATOR,
                                        camel_message_info_get_uid (info), NULL));
      continue;
    }

    bucket = g_hash_table_lookup (buckets, normalized);
    if (!bucket) {
      bucket = g_ptr_array_new ();
      g_hash_table_insert (buckets, g_steal_pointer (&normalized), bucket);
    }

    g_ptr_array_add (bucket, info);
  }

  /* Then cut each subject into runs no more than the window apart. */
  g_hash_table_iter_init (&iter, buckets);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    const gchar *normalized = key;
    GPtrArray *bucket = value;
    gint64 previous = 0;
    guint members = 0;
    guint run = 0;

    g_ptr_array_sort (bucket, compare_by_date);

    for (guint idx = 0; idx < bucket->len; idx++) {
      CamelMessageInfo *info = g_ptr_array_index (bucket, idx);
      gint64 date = effective_date (info);

      /*
       * The gap is measured against the mail before it rather than
       * against the start of the run, so a ticket that gets a reply
       * every week stays one cluster however long it runs.
       *
       * Which is also why a run needs a ceiling: a nightly report
       * never leaves a gap wide enough to break the chain, so without
       * one it would gather every mail the sender ever sent into a
       * single unopenable row.
       */
      if (idx > 0 && (date - previous > window || members >= STAMP_CLUSTER_MAX_MEMBERS)) {
        run++;
        members = 0;
      }

      members++;

      g_hash_table_insert (subjects, info,
                           g_strdup_printf (CLUSTER_PREFIX "%u" CLUSTER_SEPARATOR "%s", run, normalized));
      previous = date;
    }
  }

  return subjects;
}
