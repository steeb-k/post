#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_FOLDER_LIST (stamp_mail_folder_list_get_type())

G_DECLARE_FINAL_TYPE (StampMailFolderList, stamp_mail_folder_list, STAMP, MAIL_FOLDER_LIST, AdwBin)

GtkWidget *
stamp_mail_folder_list_new (void);

G_END_DECLS
