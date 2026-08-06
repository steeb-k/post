#include <adwaita.h>
#include <gtk/gtk.h>

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_CONTACT_VIEW (stamp_contact_view_get_type ())

G_DECLARE_FINAL_TYPE (StampContactView, stamp_contact_view, STAMP, CONTACT_VIEW, AdwBreakpointBin);

GtkWidget *
stamp_contact_view_new (void);

GtkWidget *
stamp_contact_view_get_sidebar (StampContactView *self);

void
stamp_contact_view_show_contact (StampContactView *self,
                                 const gchar       *mail);

G_END_DECLS

