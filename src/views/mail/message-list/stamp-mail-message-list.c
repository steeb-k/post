#include "stamp-mail-message-list.h"

#include "stamp-mail-message-list-item.h"

struct _StampMailMessageList
{
  GtkBox parent_instance;

  GtkWidget *list_box;
  GtkWidget *message_list_bin;
  GtkWidget *message_title;
};

G_DEFINE_FINAL_TYPE (StampMailMessageList, stamp_mail_message_list, GTK_TYPE_BOX)

static void
stamp_mail_message_list_class_init (StampMailMessageListClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_print ("************************************\n");
  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/message-list/stamp-mail-message-list.ui");

  gtk_widget_class_bind_template_child (widget_class, StampMailMessageList, list_box);
  gtk_widget_class_bind_template_child (widget_class, StampMailMessageList, message_title);
}

static void
stamp_mail_message_list_init (StampMailMessageList *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));
  /* self->list_box = gtk_list_box_new (); */
  /* gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->list_box), GTK_SELECTION_NONE); */
  /* gtk_widget_add_css_class (self->list_box, "background"); */
  /* adw_bin_set_child (ADW_BIN (self->message_list_bin), self->list_box); */
}

GtkWidget *
stamp_mail_message_list_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_MESSAGE_LIST, NULL);
}

static void
go_down (StampMailMessageList  *self,
         CamelFolderThreadNode *node)
{
  CamelFolderThreadNode *current_node = node;

  while (current_node) {
    GtkWidget *item = stamp_mail_message_list_item_new (current_node->message);

    gtk_list_box_append (GTK_LIST_BOX (self->list_box), item);

    if (current_node->next)
      go_down (self, current_node->next);

    current_node = current_node->child;
  }
}

void
stamp_mail_message_list_set_conversation (StampMailMessageList  *self,
                                          CamelFolderThreadNode *node)
{
  /* GtkListBoxRow *current_child = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->list_box), 0); */
  GtkWidget *item;
  GtkWidget *child;

  g_print ("%s: CLEARING..\n", G_STRFUNC);
  gtk_list_box_remove_all (GTK_LIST_BOX (self->list_box));
  /* for (int idx = 0; current_child; idx++) { */
  /*      (GTK_LIST_BOX (self->list_box), gtk_list_box_row_get_child (current_child)); */

  /*   current_child = gtk_list_box_get_row_at_index (GTK_LIST_BOX (self->list_box), idx); */
  /* } */

  g_print ("%s: ADD NEW ONES\n", G_STRFUNC);

  gtk_label_set_text (GTK_LABEL (self->message_title), camel_message_info_get_subject (node->message));


  item = stamp_mail_message_list_item_new (node->message);
  gtk_list_box_append (GTK_LIST_BOX (self->list_box), GTK_WIDGET (item));

  if (node->child)
    go_down (self, node->child);

  child = gtk_widget_get_last_child (self->list_box);
    if (child && STAMP_MAIL_MESSAGE_LIST_ITEM (child)) {
      StampMailMessageListItem *list_item = STAMP_MAIL_MESSAGE_LIST_ITEM (child);

      stamp_mail_message_list_item_set_expanded (list_item, TRUE);
    }
}
