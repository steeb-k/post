/* stamp-range-tree.h
 *
 * Copyright (C) 2016-2020 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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
 */

#pragma once

#include "stamp-range.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define STAMP_TYPE_RANGE_TREE (stamp_range_tree_get_type())

typedef struct _StampRangeTree StampRangeTree;

/**
 * StampRangeTraverseFunc:
 * @start: #GDateTime with the start of range of the entry
 * @end: #GDateTime with the end of range of the entry
 * @data: (nullable): the data of the entry
 * @user_data: (closure): user data passed to the function
 *
 * Function type to be called when traversing a #StampRangeTree.
 *
 * Returns: %TRUE to stop traversing, %FALSE to continue traversing
 */
typedef gboolean    (*StampRangeTraverseFunc)                     (StampRange          *range,
                                                                  gpointer            data,
                                                                  gpointer            user_data);

#define STAMP_TRAVERSE_CONTINUE FALSE;
#define STAMP_TRAVERSE_STOP     TRUE;

GType                stamp_range_tree_get_type                    (void) G_GNUC_CONST;

StampRangeTree*       stamp_range_tree_new                         (void);

StampRangeTree*       stamp_range_tree_new_with_free_func          (GDestroyNotify      destroy_func);

StampRangeTree*       stamp_range_tree_copy                        (StampRangeTree      *self);

StampRangeTree*       stamp_range_tree_ref                         (StampRangeTree      *self);

void                 stamp_range_tree_unref                       (StampRangeTree      *self);

void                 stamp_range_tree_add_range                   (StampRangeTree      *self,
                                                                  StampRange          *range,
                                                                  gpointer            data);

void                 stamp_range_tree_remove_range                (StampRangeTree      *self,
                                                                  StampRange          *range,
                                                                  gpointer            data);

void                 stamp_range_tree_remove_data                 (StampRangeTree      *self,
                                                                  gpointer            data);

void                 stamp_range_tree_traverse                    (StampRangeTree      *self,
                                                                  GTraverseType       type,
                                                                  StampRangeTraverseFunc func,
                                                                  gpointer           user_data);

GPtrArray*           stamp_range_tree_get_all_data                (StampRangeTree      *self);

GPtrArray*           stamp_range_tree_get_data_at_range           (StampRangeTree      *self,
                                                                  StampRange          *range);

guint64              stamp_range_tree_count_entries_at_range      (StampRangeTree      *self,
                                                                  StampRange          *range);

void                 stamp_range_tree_print                       (StampRangeTree      *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (StampRangeTree, stamp_range_tree_unref)

G_END_DECLS
