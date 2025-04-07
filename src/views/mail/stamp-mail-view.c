#include "stamp-mail-view.h"

#include "stamp-mail-folder-list.h"
#include "stamp-mail-conversation-list.h"

#include "stamp-mail-message-list.h"

#include "stamp-account.h"

#include "stamp-session.h"

struct _StampMailView {
  AdwBreakpointBin parent_instance;

  GList *accounts;

  GtkWidget *outer_view;
  GtkWidget *inner_view;

  GtkWidget *spinner;
  /* GtkWidget *stack; */
  GtkWidget *search_entry;
  GtkWidget *search_bar;
  GtkWidget *details;
  GtkWidget *window_title;
  GtkWidget *view_switcher;
  GtkWidget *message_header;
  GtkWidget *avatar;
  /* GtkWidget *message_title; */

  GtkWidget *folders_list;
  GtkWidget *folders_list_bin;
  GtkWidget *mail_list_bin;
  GtkWidget *mail_list;
  GtkWidget *mail_list_stack;
  GtkWidget *message_list;
  /* GtkWidget *message_list_bin; */

  gboolean is_session_started;
  GCancellable *cancellable;
  GSettings *settings;
};

G_DEFINE_FINAL_TYPE (StampMailView, stamp_mail_view, ADW_TYPE_BREAKPOINT_BIN)

static void
two_pane_apply_cb (AdwBreakpoint *breakpoint,
                   StampMailView *self)
{
  if (adw_navigation_split_view_get_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->outer_view))) {
    adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->inner_view), TRUE);
  }
}

static void
two_pane_unapply_cb (AdwBreakpoint *breakpoint,
                     StampMailView *self)
{
}

static void
one_pane_unapply_cb (AdwBreakpoint *breakpoint,
                     StampMailView *self)
{
  /* const char *section = adw_view_stack_get_visible_child_name (self->sections); */

  /* if (g_strcmp0 (section, "mail") == 0) { */
  /*   GtkListBoxRow *row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->folders_list), 0); */
  /*   gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->folders_list), GTK_SELECTION_BROWSE); */
  /*   gtk_list_box_select_row (GTK_LIST_BOX (self->folders_list), row); */
  /* } else if (g_strcmp0 (section, "mail") == 0) { */
  /*   GtkListBoxRow *row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->mails_list), 0); */
  /*   gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->mails_list), GTK_SELECTION_BROWSE); */
  /*   gtk_list_box_select_row (GTK_LIST_BOX (self->mails_list), row); */
  /* } */
}

/* static int */
/* filter_match (void *item, */
/*               void *user_data) */
/* { */
  /* StampMailView *self = STAMP_MAIL_VIEW (user_data); */
  /* StampMail *mail = STAMP_MAIL (item); */
  /* g_autofree char *search_text = g_utf8_strdown (gtk_editable_get_text (GTK_EDITABLE (self->search_entry)), -1); */
  /* g_autofree char *subject = NULL; */
  /* StampUser *sender; */
  /* g_autofree char *sender_name = NULL; */

  /* if (!search_text || strlen (search_text) == 0) */
  /*   return TRUE; */

  /* subject = g_utf8_strdown (stamp_mail_get_subject (mail), -1); */
  /* if (subject && g_strstr_len (subject, strlen (subject), search_text) != NULL) */
  /*   return 1; */

  /* sender = stamp_mail_get_sender (mail); */
  /* if (sender) { */
  /*   sender_name = g_utf8_strdown (stamp_user_get_name (sender), -1); */

  /*   if (sender_name) */
  /*     return g_strstr_len (sender_name, strlen (sender_name), search_text) != NULL; */
  /* } */

/*   return 0; */
/* } */

static void
on_mail_search_entry_changed (GtkWidget *search_entry,
                              gpointer   user_data)
{
  /* StampMailView *self = STAMP_MAIL_VIEW (user_data); */

  /* stamp_mail_conversation_list_search_changed (self->folders_list); */

}

void
stamp_mail_view_class_init (StampMailViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/stamp-mail-view.ui");

  gtk_widget_class_bind_template_child (widget_class, StampMailView, outer_view);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, inner_view);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, spinner);
  /* gtk_widget_class_bind_template_child (widget_class, StampMailView, stack); */
  gtk_widget_class_bind_template_child (widget_class, StampMailView, view_switcher);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, search_entry);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, search_bar);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, folders_list_bin);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, mail_list_bin);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, window_title);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, mail_list_stack);
  /* gtk_widget_class_bind_template_child (widget_class, StampMailView, message_title); */
  gtk_widget_class_bind_template_child (widget_class, StampMailView, message_list);

  gtk_widget_class_bind_template_callback (widget_class, two_pane_apply_cb);
  gtk_widget_class_bind_template_callback (widget_class, two_pane_unapply_cb);
  gtk_widget_class_bind_template_callback (widget_class, one_pane_unapply_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_mail_search_entry_changed);
}

/* static void */
/* on_row_expanded (GtkTreeListRow *row, */
/*                  GParamSpec     *pspec, */
/*                  gpointer        user_data) */
/* { */
  /* GSettings *account_settings; */
  /* g_autofree char *settings_path = NULL; */
  /* StampItemModel *item_model = STAMP_ITEM_MODEL (gtk_tree_list_row_get_item (row)); */
  /* StampAccount *account = stamp_folder_item_model_get_account (STAMP_FOLDER_ITEM_MODEL (item_model)); */
  /* StampMailFolder *folder = stamp_folder_item_model_get_folder (STAMP_FOLDER_ITEM_MODEL (item_model)); */
  /* g_autoptr(GStrvBuilder) builder = g_strv_builder_new (); */
  /* GStrv folders = NULL; */
  /* g_auto (GStrv) new_folders = NULL; */

  /* g_print ("%s: ENTER\n", G_STRFUNC); */

  /* settings_path = g_strconcat ("/org/tabos/stamp/mail/accounts/", stamp_account_get_name (account), "/", NULL); */
  /* account_settings = g_settings_new_with_path ("org.tabos.stamp.mail.accounts", settings_path); */

  /* folders = g_settings_get_strv (account_settings, "expanded-folders"); */

  /* if (gtk_tree_list_row_get_expanded (row)) { */
  /*   new_folders = g_strv_append ((const char * const *)folders, stamp_mail_folder_get_name (folder)); */
  /* } else { */
  /*   new_folders = g_strv_remove ((const char * const *)folders, stamp_mail_folder_get_name (folder)); */
  /* } */

  /* g_settings_set_strv (account_settings, "expanded-folders", (const char * const *)new_folders); */
/* } */

/* static void */
/* mark_read (GSimpleAction *action, */
/*            GVariant      *parameter, */
/*            gpointer       user_data) */
/* { */
  /* StampMailView *self = STAMP_MAIL_VIEW (user_data); */
  /* StampMail *mail = self->current_mail; */
/*   g_autoptr (GError) error = NULL; */

  /* stamp_account_mail_mark_read (STAMP_ACCOUNT_MAIL (self->account), mail, NULL, &error); */
/* } */

/* static GActionEntry mail_entries[] = { */
  /* { "view-source", view_source, NULL, NULL, NULL }, */
/*   { "mark-read", mark_read, NULL, NULL, NULL }, */
/* }; */



static void
on_folder_selected (GtkWidget *object,
                    GHashTable *table,
                    gpointer   user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  char *current_full_name = g_hash_table_get_values (table)->data;

  g_print ("%s: ENTER %p\n", G_STRFUNC, self->mail_list);

  adw_window_title_set_title (ADW_WINDOW_TITLE (self->window_title), current_full_name);
  adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->inner_view), TRUE);

  stamp_mail_conversation_list_load_folder (STAMP_MAIL_CONVERSATION_LIST (self->mail_list), table);
}

static void
on_conversation_selected (GtkWidget *object,
                          gpointer   thread_node,
                          gpointer   user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  CamelFolderThreadNode *node = thread_node;
  const CamelMessageInfo *message = node->message;

  g_print ("%s: ENTER\n", G_STRFUNC);
  adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->outer_view), TRUE);

  stamp_mail_message_list_set_conversation (STAMP_MAIL_MESSAGE_LIST (self->message_list), (CamelFolderThreadNode*)thread_node);
}

static void
on_session_started (GObject      *source,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  /* StampSession *session = STAMP_SESSION (source); */
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  g_print ("%s: ENTER\n", G_STRFUNC);

  self->is_session_started = TRUE;
  /* g_signal_emit (SESSION_STARTED); */
}

void
stamp_mail_view_init (StampMailView *self)
{
  GtkWidget *mail_folder_list;
  GtkWidget *mail_conversation_list;
  /* GtkListItemFactory *factory; */
  /* GtkSingleSelection *selection; */
  StampSession *session;
  GtkWidget *mail_message_list;

  gtk_widget_init_template (GTK_WIDGET (self));
  g_type_ensure (STAMP_TYPE_MAIL_MESSAGE_LIST);

  self->settings = g_settings_new ("org.tabos.stamp.mail");

  mail_folder_list = stamp_mail_folder_list_new ();
  gtk_widget_set_vexpand (mail_folder_list, TRUE);
  adw_bin_set_child (ADW_BIN (self->folders_list_bin), mail_folder_list);
  g_signal_connect (mail_folder_list, "folder-selected", G_CALLBACK (on_folder_selected), self);

  mail_conversation_list = stamp_mail_conversation_list_new ();
  self->mail_list = mail_conversation_list;
  gtk_widget_set_vexpand (mail_conversation_list, TRUE);
  adw_bin_set_child (ADW_BIN (self->mail_list_bin), mail_conversation_list);
  g_signal_connect (mail_conversation_list, "conversation-selected", G_CALLBACK (on_conversation_selected), self);

  /* mail_message_list = stamp_mail_message_list_new (); */
  /* self->message_list = mail_message_list; */
  /* adw_bin_set_child (ADW_BIN (self->message_list_bin), mail_message_list); */

  session = stamp_session_get_default ();
  stamp_session_start (session, NULL, on_session_started, self);
}

GtkWidget *
stamp_mail_view_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_VIEW, NULL);
}

