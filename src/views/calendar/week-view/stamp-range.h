#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define STAMP_TYPE_RANGE (stamp_range_get_type ())
typedef struct _StampRange StampRange;

/**
 * StampRangeOverlap:
 *
 * @STAMP_RANGE_NO_OVERLAP: the ranges don't overlap
 * @STAMP_RANGE_INTERSECTS: the ranges intersect, but have non-intersected areas
 * @STAMP_RANGE_SUBSET: the first range is a subset of the second range
 * @STAMP_RANGE_EQUAL: the ranges are exactly equal
 * @STAMP_RANGE_SUPERSET: the first range is a superset of the second range
 *
 * The possible results of comparing two ranges.
 */
typedef enum
{
  STAMP_RANGE_NO_OVERLAP,
  STAMP_RANGE_INTERSECTS,
  STAMP_RANGE_SUBSET,
  STAMP_RANGE_EQUAL,
  STAMP_RANGE_SUPERSET,
} StampRangeOverlap;

/**
 * StampRangePosition:
 *
 * @STAMP_RANGE_BEFORE: range @a is before @b
 * @STAMP_RANGE_AFTER: range @a is after @b
 *
 * The position of @a relative to @b. When the ranges are exactly equal, it
 * is undefined.
 */
typedef enum
{
  STAMP_RANGE_BEFORE = -1,
  STAMP_RANGE_MATCH  = 0,
  STAMP_RANGE_AFTER  = 1,
} StampRangePosition;

/**
 * StampRangeType:
 *
 * @STAMP_RANGE_DEFAULT: the default (date and time) range type
 * @STAMP_RANGE_DATE_ONLY: date-only range
 *
 * The type of the range. When comparing ranges, if either one of
 * them is date-only, times are not considered by the comparison.
 */
typedef enum
{
  STAMP_RANGE_DEFAULT,
  STAMP_RANGE_DATE_ONLY,
} StampRangeType;

GType                stamp_range_get_type                         (void) G_GNUC_CONST;

StampRange*           stamp_range_new                              (GDateTime          *range_start,
                                                                  GDateTime          *range_end,
                                                                  StampRangeType       range_type);
StampRange*           stamp_range_new_take                         (GDateTime          *range_start,
                                                                  GDateTime          *range_end,
                                                                  StampRangeType       range_type);
StampRange*           stamp_range_copy                             (StampRange          *self);
StampRange*           stamp_range_ref                              (StampRange          *self);
void                 stamp_range_unref                            (StampRange          *self);

GDateTime*           stamp_range_get_start                        (StampRange          *self);
GDateTime*           stamp_range_get_end                          (StampRange          *self);
StampRangeType        stamp_range_get_range_type                   (StampRange          *self);

StampRangeOverlap     stamp_range_calculate_overlap                (StampRange          *a,
                                                                  StampRange          *b,
                                                                  StampRangePosition  *out_position);

gint                 stamp_range_compare                          (StampRange          *a,
                                                                  StampRange          *b);

StampRange*           stamp_range_union                            (StampRange          *a,
                                                                  StampRange          *b);

gchar*               stamp_range_to_string                        (StampRange          *self);

gboolean             stamp_range_contains_datetime                (StampRange          *self,
                                                                  GDateTime          *datetime);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (StampRange, stamp_range_unref);

G_END_DECLS
