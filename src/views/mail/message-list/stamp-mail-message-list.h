#pragma once

#include <adwaita.h>

#include <camel/camel.h>

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_MESSAGE_LIST (stamp_mail_message_list_get_type())

G_DECLARE_FINAL_TYPE (StampMailMessageList, stamp_mail_message_list, STAMP, MAIL_MESSAGE_LIST, GtkBox)

GtkWidget *
stamp_mail_message_list_new (void);

void
stamp_mail_message_list_set_conversation (StampMailMessageList  *self,
                                          CamelFolderThreadNode *node);

G_END_DECLS
