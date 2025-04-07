#include <adwaita.h>
#include <gtk/gtk.h>

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_CONTACT_VIEW (stamp_contact_view_get_type ())

G_DECLARE_FINAL_TYPE (StampContactView, stamp_contact_view, STAMP, CONTACT_VIEW, AdwBreakpointBin);

GtkWidget *
stamp_contact_view_new (void);

void
stamp_contact_view_show_contact (StampContactView *self,
                                 const char       *mail);

G_END_DECLS

