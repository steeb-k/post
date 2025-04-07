/* stamp-mail-row.c
 *
 * Copyright 2024 Jan-Michael Brummer
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

#include "stamp-mail-row.h"

#include "stamp-mail-conversation-item-model.h"
#include "stamp-time-helpers.h"

struct _StampMailRow {
  GtkBox parent_instance;

  GtkWidget *avatar;
  GtkWidget *participants;
  GtkWidget *topic;
  GtkWidget *date;
  GtkWidget *body;
  GtkWidget *counter;
  GtkWidget *attachment_icon;
  GtkWidget *flagged_icon;
  gint64 received_date;

  GBinding *binding;
};

G_DEFINE_FINAL_TYPE (StampMailRow, stamp_mail_row, GTK_TYPE_BOX)

void
stamp_mail_row_class_init (StampMailRowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/conversation-list/stamp-mail-row.ui");
  gtk_widget_class_bind_template_child (widget_class, StampMailRow, avatar);
  gtk_widget_class_bind_template_child (widget_class, StampMailRow, participants);
  gtk_widget_class_bind_template_child (widget_class, StampMailRow, date);
  gtk_widget_class_bind_template_child (widget_class, StampMailRow, topic);
  gtk_widget_class_bind_template_child (widget_class, StampMailRow, body);
  gtk_widget_class_bind_template_child (widget_class, StampMailRow, counter);
  gtk_widget_class_bind_template_child (widget_class, StampMailRow, attachment_icon);
  gtk_widget_class_bind_template_child (widget_class, StampMailRow, flagged_icon);
}

void
stamp_mail_row_init (StampMailRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
stamp_mail_row_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_ROW, NULL);
}

void
stamp_mail_row_set_mail (StampMailRow                   *self,
                         StampMailConversationItemModel *model)
{
  const char *source_label_text;
  const char *preview;
  char *tmp;

  gtk_inscription_set_text (GTK_INSCRIPTION (self->topic), stamp_mail_conversation_item_model_get_subject (model));

  source_label_text = stamp_mail_conversation_item_model_get_from (model);
  gtk_inscription_set_text (GTK_INSCRIPTION (self->participants), source_label_text);

  adw_avatar_set_text (ADW_AVATAR (self->avatar), source_label_text);
  adw_avatar_set_show_initials (ADW_AVATAR (self->avatar), TRUE);

  /* return; */

  /* stamp_avatar_set_user (STAMP_AVATAR (self->avatar), sender); */

  if (!stamp_mail_conversation_item_model_get_unread (model)) {
    gtk_widget_add_css_class (self->body, "dim-label");
    gtk_widget_remove_css_class (self->topic, "caption-heading");
    gtk_widget_add_css_class (self->topic, "caption");
    gtk_widget_remove_css_class (self->participants, "heading");
    gtk_widget_remove_css_class (self->topic, "accent");
  } else {
    gtk_widget_remove_css_class (self->topic, "caption");
    gtk_widget_remove_css_class (self->body, "dim-label");
    gtk_widget_add_css_class (self->topic, "caption-heading");
    gtk_widget_add_css_class (self->participants, "heading");
    gtk_widget_add_css_class (self->topic, "accent");
  }

  preview = stamp_mail_conversation_item_model_get_preview (model);
  if (preview) {
    tmp = g_markup_escape_text (preview, -1);
    gtk_inscription_set_markup (GTK_INSCRIPTION (self->body), tmp);
    g_clear_pointer (&tmp, g_free);
  } else {
    gtk_inscription_set_markup (GTK_INSCRIPTION (self->body), "");
  }

  /* flagged_icon */
  if (stamp_mail_conversation_item_model_get_flagged (model)) {
    gtk_image_set_from_icon_name (GTK_IMAGE (self->flagged_icon), "starred-symbolic");
  } else {
    gtk_image_set_from_icon_name (GTK_IMAGE (self->flagged_icon), "non-starred-symbolic");
  }

  self->received_date = stamp_mail_conversation_item_model_get_date (model);
  tmp = stamp_time_helpers_utf_friendly_time (self->received_date);
  gtk_label_set_text (GTK_LABEL (self->date), tmp);
  g_clear_pointer (&tmp, g_free);

  tmp = g_strdup_printf (" %d ", stamp_mail_conversation_item_model_get_num_messages (model));
  gtk_label_set_text (GTK_LABEL (self->counter), tmp);
  gtk_widget_set_visible (self->counter, stamp_mail_conversation_item_model_get_num_messages (model) > 1);
  g_clear_pointer (&tmp, g_free);

  gtk_widget_set_visible (self->attachment_icon, stamp_mail_conversation_item_model_has_attachment (model));
}

