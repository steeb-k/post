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

#define LIBICAL_GLIB_UNSTABLE_API 1

#include <adwaita.h>
#include <libical-glib/libical-glib.h>

G_BEGIN_DECLS

#define STAMP_TYPE_INVITATION_DIALOG (stamp_invitation_dialog_get_type ())
G_DECLARE_FINAL_TYPE (StampInvitationDialog, stamp_invitation_dialog, STAMP, INVITATION_DIALOG, AdwDialog)

/*
 * Shows a calendar invitation the way a mail client is expected to: what
 * and when, a Join button for any meeting link it carries, who organised
 * it and who else is invited with their answers, and the notes. An
 * invitation that asks for an answer (METHOD:REQUEST) gets Decline,
 * Tentative and Accept buttons and emits "respond" with the chosen
 * ICalParameterPartstat. Anything else gets an Add to Calendar button
 * and emits "add-to-calendar". The dialog closes itself either way.
 *
 * @vcalendar is the whole VCALENDAR from the message. @method is its
 * METHOD, or NULL when it had none. @own_email is the address the
 * message was received at, used to find and report the reader's own
 * current answer.
 */
StampInvitationDialog *
stamp_invitation_dialog_new (ICalComponent *vcalendar,
                             const gchar   *method,
                             const gchar   *own_email);

G_END_DECLS
