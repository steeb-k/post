/* stamp-range.c
 *
 * Copyright 2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stamp-range.h"

/* #include "stamp-date-time-utils.h" */

struct _StampRange {
  gatomicrefcount ref_count;

  GDateTime *range_start;
  GDateTime *range_end;
  StampRangeType range_type;
};

G_DEFINE_BOXED_TYPE (StampRange, stamp_range, stamp_range_ref, stamp_range_unref);

typedef int (*CompareDateTimeFunc) (GDateTime *a,
                                    GDateTime *b);

gint
stamp_date_time_compare_date (GDateTime *dt1,
                              GDateTime *dt2)
{
  GDate d1, d2;

  if (!dt1 && !dt2)
    return 0;
  else if (!dt1)
    return -1;
  else if (!dt2)
    return 1;

  g_date_set_dmy (&d1,
                  g_date_time_get_day_of_month (dt1),
                  g_date_time_get_month (dt1),
                  g_date_time_get_year (dt1));

  g_date_set_dmy (&d2,
                  g_date_time_get_day_of_month (dt2),
                  g_date_time_get_month (dt2),
                  g_date_time_get_year (dt2));

  return g_date_days_between (&d2, &d1);
}


static CompareDateTimeFunc
get_compare_func (StampRange *a,
                  StampRange *b)
{
  if (a->range_type == STAMP_RANGE_DATE_ONLY || b->range_type == STAMP_RANGE_DATE_ONLY)
    return stamp_date_time_compare_date;
  else
    return (CompareDateTimeFunc)g_date_time_compare;
}

/**
 * stamp_range_copy:
 * @self: a #StampRange
 *
 * Makes a deep copy of a #StampRange.
 *
 * Returns: (transfer full): A newly created #StampRange with the same
 *   contents as @self
 */
StampRange *
stamp_range_copy (StampRange *self)
{
  StampRange *copy;

  g_return_val_if_fail (self, NULL);
  g_return_val_if_fail (!g_atomic_ref_count_compare (&self->ref_count, 0), NULL);

  copy = stamp_range_new (self->range_start, self->range_end, self->range_type);

  return copy;
}

static void
stamp_range_free (StampRange *self)
{
  g_assert (self);
  g_assert (g_atomic_ref_count_compare (&self->ref_count, 0));

  /* stamp_clear_date_time (&self->range_start); */
  /* stamp_clear_date_time (&self->range_end); */
  g_free (self);
}

/**
 * stamp_range_ref:
 * @self: A #StampRange
 *
 * Increments the reference count of @self by one.
 *
 * Returns: (transfer full): @self
 */
StampRange *
stamp_range_ref (StampRange *self)
{
  g_return_val_if_fail (self, NULL);
  g_return_val_if_fail (!g_atomic_ref_count_compare (&self->ref_count, 0), NULL);

  g_atomic_ref_count_inc (&self->ref_count);

  return self;
}

/**
 * stamp_range_unref:
 * @self: A #StampRange
 *
 * Decrements the reference count of @self by one, freeing the structure when
 * the reference count reaches zero.
 */
void
stamp_range_unref (StampRange *self)
{
  g_return_if_fail (self);
  g_return_if_fail (!g_atomic_ref_count_compare (&self->ref_count, 0));

  if (g_atomic_ref_count_dec (&self->ref_count))
    stamp_range_free (self);
}

/**
 * stamp_range_new:
 *
 * Creates a new #StampRange.
 *
 * Returns: (transfer full): A newly created #StampRange
 */
StampRange *
stamp_range_new (GDateTime      *range_start,
                 GDateTime      *range_end,
                 StampRangeType  range_type)
{
  g_return_val_if_fail (range_start, NULL);
  g_return_val_if_fail (range_end, NULL);
  g_return_val_if_fail (g_date_time_compare (range_start, range_end) <= 0, NULL);

  return stamp_range_new_take (g_date_time_ref (range_start),
                               g_date_time_ref (range_end),
                               range_type);
}


/**
 * stamp_range_new_take:
 * @range_start: (transfer full): a #GDateTime
 * @range_end: (transfer full): a #GDateTime
 *
 * Creates a new #StampRange and takes ownership of
 * @range_start and @range_end.
 *
 * Returns: (transfer full): A newly created #StampRange
 */
StampRange *
stamp_range_new_take (GDateTime      *range_start,
                      GDateTime      *range_end,
                      StampRangeType  range_type)
{
  StampRange *self;

  g_return_val_if_fail (range_start, NULL);
  g_return_val_if_fail (range_end, NULL);
  g_return_val_if_fail (g_date_time_compare (range_start, range_end) <= 0, NULL);

  self = g_new0 (StampRange, 1);
  g_atomic_ref_count_init (&self->ref_count);

  self->range_start = range_start;
  self->range_end = range_end;
  self->range_type = range_type;

  return self;
}

/**
 * stamp_range_get_start:
 * @self: a #StampRange
 *
 * Retrieves a copy of @self's range start.
 *
 * Returns: (transfer full): a #GDateTime
 */
GDateTime *
stamp_range_get_start (StampRange *self)
{
  g_return_val_if_fail (self, NULL);
  g_return_val_if_fail (!g_atomic_ref_count_compare (&self->ref_count, 0), NULL);

  return g_date_time_ref (self->range_start);
}

/**
 * stamp_range_get_end:
 * @self: a #StampRange
 *
 * Retrieves a copy of @self's range end.
 *
 * Returns: (transfer full): a #GDateTime
 */
GDateTime *
stamp_range_get_end (StampRange *self)
{
  g_return_val_if_fail (self, NULL);
  g_return_val_if_fail (!g_atomic_ref_count_compare (&self->ref_count, 0), NULL);

  return g_date_time_ref (self->range_end);
}

/**
 * stamp_range_get_range_type:
 * @self: a #StampRange
 *
 * Retrieves the range type of @self.
 *
 * Returns: the range type of @self
 */
StampRangeType
stamp_range_get_range_type (StampRange *self)
{
  g_return_val_if_fail (self, STAMP_RANGE_DEFAULT);
  g_return_val_if_fail (!g_atomic_ref_count_compare (&self->ref_count, 0), STAMP_RANGE_DEFAULT);

  return self->range_type;
}

/**
 * stamp_range_calculate_overlap:
 * @a: a #StampRange
 * @b: a #StampRange
 * @out_position: (direction out)(nullable): return location for a #StampRangePosition
 *
 * Calculates how @a and @b overlap.
 *
 * The position returned at @out_position is always relative to @a. For example,
 * %STAMP_RANGE_AFTER means @a is after @b. The heuristic for the position is:
 *
 *  1. If @a begins before @b, @a comes before @b.
 *  2. If @a and @b begin at precisely the same moment, but @a ends before @b,
 *     @a comes before @b.
 *  3. Otherwise, @b comes before @a.
 *
 * Returns: the overlap result between @a and @b
 */
StampRangeOverlap
stamp_range_calculate_overlap (StampRange         *a,
                               StampRange         *b,
                               StampRangePosition *out_position)
{
  CompareDateTimeFunc compare_func;
  StampRangePosition position;
  StampRangeOverlap overlap;
  gint a_start_b_start_diff;
  gint a_end_b_end_diff;

  g_return_val_if_fail (a && b, STAMP_RANGE_NO_OVERLAP);

  /*
   * There are 11 cases that we need to take care of:
   *
   * 1. Equal
   *
   *   A |------------------------|
   *   B |------------------------|
   *
   * 2. Superset
   *
   * i.
   *   A |------------------------|
   *   B |-------------------|
   *
   * ii.
   *   A |------------------------|
   *   B   |-------------------|
   *
   * iii.
   *   A |------------------------|
   *   B      |-------------------|
   *
   * 3. Subset
   *
   * i.
   *   A |-------------------|
   *   B |------------------------|
   *
   * ii.
   *   A   |-------------------|
   *   B |------------------------|
   *
   * iii.
   *   A      |-------------------|
   *   B |------------------------|
   *
   * 4. Intersection
   *
   * i.
   *   A |--------------------|
   *   B     |--------------------|
   *
   * ii.
   *   A     |--------------------|
   *   B |--------------------|
   *
   * 5) No Overlap
   *
   * i.
   *   A             |------------|
   *   B |-----------|
   *
   * ii.
   *   A |------------|
   *   B              |-----------|
   *
   */

  compare_func = get_compare_func (a, b);
  a_start_b_start_diff = compare_func (a->range_start, b->range_start);
  a_end_b_end_diff = compare_func (a->range_end, b->range_end);

  if (a_start_b_start_diff == 0 && a_end_b_end_diff == 0){
    /* Case 1, the easiest */
    overlap = STAMP_RANGE_EQUAL;
    position = STAMP_RANGE_MATCH;
  } else {
    if (a_start_b_start_diff == 0){
      if (a_end_b_end_diff > 0){
        /* Case 2.i */
        overlap = STAMP_RANGE_SUPERSET;
        position = STAMP_RANGE_AFTER;
      } else { /* a_end_b_end_diff < 0 */
        /* Case 3.i */
        overlap = STAMP_RANGE_SUBSET;
        position = STAMP_RANGE_BEFORE;
      }
    } else if (a_end_b_end_diff == 0) {
      gint a_start_b_end_diff;
      gint a_end_b_start_diff;

      a_start_b_end_diff = compare_func (a->range_start, b->range_end);
      a_end_b_start_diff = compare_func (a->range_end, b->range_start);

      if (a_start_b_end_diff >= 0){
        /* Case 5.i for zero length A */
        overlap = STAMP_RANGE_NO_OVERLAP;
        position = STAMP_RANGE_AFTER;
      } else if (a_end_b_start_diff <= 0) {
        /* Case 5.ii for zero length B */
        overlap = STAMP_RANGE_NO_OVERLAP;
        position = STAMP_RANGE_BEFORE;
      } else if (a_start_b_start_diff < 0) {
        /* Case 2.iii */
        overlap = STAMP_RANGE_SUPERSET;
        position = STAMP_RANGE_BEFORE;
      } else { /* a_start_b_start_diff > 0 */
        /* Case 3.iii */
        overlap = STAMP_RANGE_SUBSET;
        position = STAMP_RANGE_AFTER;
      }
    } else { /* a_start_b_start_diff != 0 && a_end_b_end_diff != 0 */
      if (a_start_b_start_diff < 0 && a_end_b_end_diff > 0){
        /* Case 2.ii */
        overlap = STAMP_RANGE_SUPERSET;
        position = STAMP_RANGE_BEFORE;
      } else if (a_start_b_start_diff > 0 && a_end_b_end_diff < 0) {
        /* Case 3.ii */
        overlap = STAMP_RANGE_SUBSET;
        position = STAMP_RANGE_AFTER;
      } else {
        gint a_start_b_end_diff;
        gint a_end_b_start_diff;

        a_start_b_end_diff = compare_func (a->range_start, b->range_end);
        a_end_b_start_diff = compare_func (a->range_end, b->range_start);

        /* No overlap cases */
        if (a_start_b_end_diff >= 0){
          /* Case 5.i */
          overlap = STAMP_RANGE_NO_OVERLAP;
          position = STAMP_RANGE_AFTER;
        } else if (a_end_b_start_diff <= 0) {
          /* Case 5.ii */
          overlap = STAMP_RANGE_NO_OVERLAP;
          position = STAMP_RANGE_BEFORE;
        } else {
          /* Intersection cases */
          if (a_start_b_start_diff < 0 && a_end_b_start_diff > 0 && a_end_b_end_diff < 0){
            /* Case 4.i */
            overlap = STAMP_RANGE_INTERSECTS;
            position = STAMP_RANGE_BEFORE;
          } else if (a_start_b_start_diff > 0 && a_start_b_end_diff < 0 && a_end_b_end_diff > 0) {
            /* Case 4.ii */
            overlap = STAMP_RANGE_INTERSECTS;
            position = STAMP_RANGE_AFTER;
          } else {
            g_assert_not_reached ();
          }
        }
      }
    }
  }

  if (out_position)
    *out_position = position;

  return overlap;
}

/**
 * stamp_range_compare:
 * @a: a #StampRange
 * @b: a #StampRange
 *
 * Compares @a and @b. See stamp_range_calculate_overlap() for the
 * rules of when a range comes before or after.
 *
 * Returns: -1 is @a comes before @b, 0 if they're equal, or 1 if
 * @a comes after @b.
 */
gint
stamp_range_compare (StampRange *a,
                     StampRange *b)
{
  CompareDateTimeFunc compare_func;
  gint result;

  g_return_val_if_fail (a && b, 0);

  compare_func = get_compare_func (a, b);
  result = compare_func (a->range_start, b->range_start);

  if (result == 0)
    result = compare_func (a->range_end, b->range_end);

  return result;
}

/**
 * stamp_range_union:
 * @a: a #StampRange
 * @b: a #StampRange
 *
 * Creates a new #StampRange with the union of @a and @b.
 *
 * Returns: (transfer full): a #StampRange.
 */
StampRange *
stamp_range_union (StampRange *a,
                   StampRange *b)
{
  CompareDateTimeFunc compare_func;
  StampRangeType range_type;
  GDateTime *start;
  GDateTime *end;

  g_return_val_if_fail (a != NULL, NULL);
  g_return_val_if_fail (b != NULL, NULL);

  compare_func = get_compare_func (a, b);

  if (compare_func (a->range_start, b->range_start) < 0)
    start = a->range_start;
  else
    start = b->range_start;

  if (compare_func (a->range_end, b->range_end) > 0)
    end = a->range_end;
  else
    end = b->range_end;

  if (a->range_type == STAMP_RANGE_DATE_ONLY || b->range_type == STAMP_RANGE_DATE_ONLY)
    range_type = STAMP_RANGE_DATE_ONLY;
  else
    range_type = STAMP_RANGE_DEFAULT;

  return stamp_range_new (start, end, range_type);
}

/**
 * stamp_range_to_string:
 * @self: a #StampRange
 *
 * Formats @self using ISO8601 dates. This is only useful
 * for debugging purposes.
 *
 * Returns: (transfer full): a string representation of @self
 */
gchar *
stamp_range_to_string (StampRange *self)
{
  g_autofree gchar *start_string = NULL;
  g_autofree gchar *end_string = NULL;

  g_return_val_if_fail (self, NULL);

  start_string = g_date_time_format_iso8601 (self->range_start);
  end_string = g_date_time_format_iso8601 (self->range_end);

  return g_strdup_printf ("[%s | %s)", start_string, end_string);
}

/**
 * stamp_range_contains_datetime:
 * @self: a #StampRange
 * @datetime: a #GDateTime
 *
 * Checks if @datetime is contained within @self.
 *
 * Returns: %TRUE is @datetime is within the range described
 * by @self, %FALSE otherwise
 */
gboolean
stamp_range_contains_datetime (StampRange *self,
                               GDateTime  *datetime)
{
  g_return_val_if_fail (self, FALSE);
  g_return_val_if_fail (datetime, FALSE);

  switch (self->range_type){
    case STAMP_RANGE_DEFAULT:
      return g_date_time_compare (datetime, self->range_start) >= 0 &&
             g_date_time_compare (datetime, self->range_end) < 0;

    case STAMP_RANGE_DATE_ONLY:
      return stamp_date_time_compare_date (datetime, self->range_start) >= 0 &&
             stamp_date_time_compare_date (datetime, self->range_end) <= 0;
    default:
      g_assert_not_reached ();
  }
}
