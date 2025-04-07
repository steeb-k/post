#pragma once

#include <gtk/gtk.h>
#include <adwaita.h>

#include <camel/camel.h>

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_MESSAGE_HEADER (stamp_mail_message_header_get_type ())
G_DECLARE_FINAL_TYPE (StampMailMessageHeader, stamp_mail_message_header, STAMP, MAIL_MESSAGE_HEADER, AdwActionRow);

struct _StampMailMessageHeader {
  AdwActionRow parent_instance;
};

GtkWidget *stamp_mail_message_header_new (void);

void stamp_mail_message_header_set_mail (StampMailMessageHeader *self,
                                         const CamelMessageInfo       *message_info);

G_END_DECLS

