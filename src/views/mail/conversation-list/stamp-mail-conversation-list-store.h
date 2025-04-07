#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_CONVERSATION_LIST_STORE (stamp_mail_conversation_list_store_get_type())

G_DECLARE_FINAL_TYPE (StampMailConversationListStore, stamp_mail_conversation_list_store, STAMP, MAIL_CONVERSATION_LIST_STORE, GListStore)

G_END_DECLS
