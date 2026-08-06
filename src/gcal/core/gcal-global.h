/*
 * gcal-global.h
 *
 * Copyright 2023 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
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

#pragma once

#include <glib.h>

#include "gcal-types.h"

G_BEGIN_DECLS

GThread *            gcal_get_main_thread                        (void);

/*
 * Post: GNOME Calendar reaches its GcalContext through the GcalApplication
 * singleton. Post has its own application class, so the context is published
 * here instead and gcal-application.h routes callers to it.
 */
GcalContext *        gcal_get_default_context                    (void);

void                 gcal_set_default_context                    (GcalContext *context);

G_END_DECLS
