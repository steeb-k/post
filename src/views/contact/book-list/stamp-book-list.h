#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define STAMP_TYPE_BOOK_LIST (stamp_book_list_get_type())

G_DECLARE_FINAL_TYPE (StampBookList, stamp_book_list, STAMP, BOOK_LIST, AdwBin);

void
stamp_book_list_unselect (StampBookList *self);

G_END_DECLS
