#include <adwaita.h>
#include <gtk/gtk.h>

#include "stamp-window.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_VIEW (stamp_mail_view_get_type ())

G_DECLARE_FINAL_TYPE (StampMailView, stamp_mail_view, STAMP, MAIL_VIEW, AdwBreakpointBin);

GtkWidget *
stamp_mail_view_new (void);

void
stamp_mail_view_setup (StampMailView    *self,
                       AdwViewStack     *stack);

G_END_DECLS

