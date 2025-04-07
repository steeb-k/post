#pragma once

#include <adwaita.h>

#include <camel/camel.h>

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_CONVERSATION_ITEM_MODEL (stamp_mail_conversation_item_model_get_type())

G_DECLARE_FINAL_TYPE (StampMailConversationItemModel, stamp_mail_conversation_item_model, STAMP, MAIL_CONVERSATION_ITEM_MODEL, GObject)

StampMailConversationItemModel *
stamp_mail_conversation_item_model_new (CamelFolderThreadNode *thread_node);

const char *
stamp_mail_conversation_item_model_get_subject (StampMailConversationItemModel *self);

CamelFolderThreadNode *
stamp_mail_conversation_item_model_get_node (StampMailConversationItemModel *self);

char *
stamp_mail_conversation_item_model_get_from (StampMailConversationItemModel *self);

guint
stamp_mail_conversation_item_model_get_date (StampMailConversationItemModel *self);

guint
stamp_mail_conversation_item_model_get_num_messages (StampMailConversationItemModel *self);

gboolean
stamp_mail_conversation_item_model_get_unread (StampMailConversationItemModel *self);

gboolean
stamp_mail_conversation_item_model_has_attachment (StampMailConversationItemModel *self);

gboolean
stamp_mail_conversation_item_model_get_flagged (StampMailConversationItemModel *self);

const char *
stamp_mail_conversation_item_model_get_preview (StampMailConversationItemModel *self);

G_END_DECLS
