#include "stamp-calendar-view.h"

#include "stamp-account.h"

struct _StampCalendarView {
  GtkWidget parent_instance;

  GtkWidget *view_switcher;
  GtkWidget *outer_view;
  GtkWidget *inner_view;

  GtkWidget *book_list;
  GtkWidget *book_list_bin;
  GtkWidget *contact_list;
  GtkWidget *contact_details;

  StampAccount *account;
};

G_DEFINE_FINAL_TYPE (StampCalendarView, stamp_calendar_view, ADW_TYPE_BREAKPOINT_BIN)

/* static void */
/* on_contacts_hidden (AdwNavigationPage *page, */
/*                     gpointer           user_data) */
/* { */
/*   StampCalendarView *self = STAMP_CONTACT_VIEW (user_data); */
/*   stamp_book_list_unselect (STAMP_BOOK_LIST (self->book_list)); */
/* } */

/* static void */
/* on_details_hidden (AdwNavigationPage *page, */
/*                    gpointer           user_data) */
/* { */
/*   StampCalendarView *self = STAMP_CONTACT_VIEW (user_data); */
/*   stamp_contact_list_unselect (STAMP_CONTACT_LIST (self->contact_list)); */
/* } */

/* static void */
/* two_pane_apply_cb (AdwBreakpoint    *breakpoint, */
/*                    StampContactView *self) */
/* { */
/*   if (adw_navigation_split_view_get_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->outer_view))) { */
/*     adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->inner_view), TRUE); */
/*   } */
/* } */

/* static void */
/* on_book_selected (GtkWidget    *object, */
/*                   StampAccount *account, */
/*                   EClient      *client, */
/*                   gpointer      user_data) */
/* { */
/*   StampCalendarView *self = STAMP_CONTACT_VIEW (user_data); */

/*   adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->inner_view), TRUE); */
/*   stamp_contact_list_load (STAMP_CONTACT_LIST (self->contact_list), client, account); */
/* } */

/* static void */
/* on_contact_selected (GtkWidget        *object, */
/*                      StampAccount     *account, */
/*                      StampContactItem *item, */
/*                      gpointer          user_data) */
/* { */
/*   StampCalendarView *self = STAMP_CONTACT_VIEW (user_data); */

/*   adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->outer_view), TRUE); */
/*   stamp_contact_details_show (STAMP_CONTACT_DETAILS (self->contact_details), account, item); */
/* } */

/* static void */
/* on_outer_view_collapsed (AdwBreakpoint    *breakpoint, */
/*                          StampCalendarView *self) */
/* { */
/*   if (adw_navigation_split_view_get_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->outer_view))) { */
/*     adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->inner_view), TRUE); */
/*   } */
/* } */

void
stamp_calendar_view_class_init (StampCalendarViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/calendar/stamp-calendar-view.ui");

  gtk_widget_class_bind_template_child (widget_class, StampCalendarView, view_switcher);
  /* gtk_widget_class_bind_template_child (widget_class, StampCalendarView, outer_view); */
  /* gtk_widget_class_bind_template_child (widget_class, StampCalendarView, inner_view); */
  /* gtk_widget_class_bind_template_child (widget_class, StampCalendarView, contact_list); */
  /* gtk_widget_class_bind_template_child (widget_class, StampCalendarView, contact_details); */
  /* gtk_widget_class_bind_template_child (widget_class, StampCalendarView, book_list); */

  /* gtk_widget_class_bind_template_callback (widget_class, on_book_selected); */
  /* gtk_widget_class_bind_template_callback (widget_class, on_contact_selected); */
  /* gtk_widget_class_bind_template_callback (widget_class, on_contacts_hidden); */
  /* gtk_widget_class_bind_template_callback (widget_class, on_details_hidden); */
  /* gtk_widget_class_bind_template_callback (widget_class, two_pane_apply_cb); */
  /* gtk_widget_class_bind_template_callback (widget_class, on_outer_view_collapsed); */
}

void
stamp_calendar_view_init (StampCalendarView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
stamp_calendar_view_new (void)
{
  return g_object_new (STAMP_TYPE_CALENDAR_VIEW, NULL);
}

void
stamp_calendar_view_setup (StampCalendarView *self,
                           AdwViewStack      *stack)
{
  adw_view_switcher_bar_set_stack (ADW_VIEW_SWITCHER_BAR (self->view_switcher), stack);
}
