#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_CONVERSATION_LIST (stamp_mail_conversation_list_get_type())

G_DECLARE_FINAL_TYPE (StampMailConversationList, stamp_mail_conversation_list, STAMP, MAIL_CONVERSATION_LIST, GtkBox)

GtkWidget *
stamp_mail_conversation_list_new (void);

void
stamp_mail_conversation_list_load_folder (StampMailConversationList *self,
                                          GHashTable                *table);

void
stamp_mail_conversation_list_search_changed (StampMailConversationList *self);

G_END_DECLS
