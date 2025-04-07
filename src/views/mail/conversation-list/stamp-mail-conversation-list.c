#include "stamp-mail-conversation-list.h"

#include "stamp-mail-row.h"
#include "stamp-account.h"

#include "stamp-mail-conversation-list-store.h"
#include "stamp-mail-conversation-item-model.h"

struct _StampMailConversationList
{
  GtkBox parent_instance;
  GtkWidget *mail_list;

  GListStore *mails;
  GCancellable *cancellable;
  GPtrArray *array;
};

G_DEFINE_FINAL_TYPE (StampMailConversationList, stamp_mail_conversation_list, GTK_TYPE_BOX)

enum {
  CONVERSATION_SELECTED,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

static void
stamp_mail_conversation_list_class_init (StampMailConversationListClass *klass)
{
  signals[CONVERSATION_SELECTED] = g_signal_new ("conversation-selected", G_OBJECT_CLASS_TYPE (klass),
                                 G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                 0, NULL, NULL, NULL,
                                 G_TYPE_NONE,
                                 1, G_TYPE_POINTER);
}

static gint
mail_time_compare (gconstpointer a,
                   gconstpointer b,
                   gpointer      user_data)
{
  StampMailConversationItemModel *mail_a = (StampMailConversationItemModel *)a;
  StampMailConversationItemModel *mail_b = (StampMailConversationItemModel *)b;

  if (stamp_mail_conversation_item_model_get_date (mail_a) < stamp_mail_conversation_item_model_get_date (mail_b))
    return 1;

  if (stamp_mail_conversation_item_model_get_date (mail_a) > stamp_mail_conversation_item_model_get_date (mail_b))
    return -1;

  return 0;
}

static void
add_conversation_item (StampMailConversationList *self,
                       CamelFolderThreadNode     *thread_node)
{
  StampMailConversationItemModel *item = stamp_mail_conversation_item_model_new (thread_node);

  /* stamp_mail_row_set_mail (STAMP_MAIL_ROW (row), message_info); */

  /* gtk_list_box_append (GTK_LIST_BOX (self->mail_list), row); */
  g_list_store_insert_sorted (self->mails, item, mail_time_compare, self);
  /* g_ptr_array_add (self->array, item); */
}

static void
on_folder_loaded (GObject      *source,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  StampMailConversationList *self = STAMP_MAIL_CONVERSATION_LIST (user_data);
  g_autoptr (GPtrArray) uids = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelFolder) folder = camel_store_get_folder_finish (CAMEL_STORE (source), res, &error);

  g_print ("%s: ENTER\n", G_STRFUNC);
  if (error) {
    g_warning ("Could not get folder: %s", error->message);
    return;
  }

  /* camel_folder_refresh_info_sync (folder, NULL, NULL); */
  self->array = g_ptr_array_new ();
  uids = camel_folder_get_uids (folder);
  if (uids) {
    CamelFolderThread *thread = camel_folder_thread_messages_new (folder, uids, FALSE);
    CamelFolderThreadNode *child = thread->tree;

    g_print ("%s: child %p\n", G_STRFUNC, child);
    while (child) {
      const CamelMessageInfo *message = child->message;

      add_conversation_item (self, child);
      child = child->next;
    }
  }

  /* g_print ("%s: PRE EXIT\n", G_STRFUNC); */
  /* g_list_store_splice (self->mails, 0, 0, self->array->pdata, self->array->len); */

  g_print ("%s: EXIT\n", G_STRFUNC);
}

static void
load_folder (gpointer key,
             gpointer value,
             gpointer user_data)
{
  StampMailConversationList *self = STAMP_MAIL_CONVERSATION_LIST (user_data);
  StampAccount *current_account = STAMP_ACCOUNT (key);
  CamelService *service = stamp_account_get_service (current_account);
  CamelStore *store = CAMEL_STORE (service);
  char *current_full_name = value;

  g_print ("get folder: %s\n", current_full_name);
  camel_store_get_folder (store, current_full_name, CAMEL_STORE_FOLDER_NONE, G_PRIORITY_DEFAULT, NULL, on_folder_loaded, self);
}

static void
setup_listitem_cb (GtkListItemFactory *factory,
                   GtkListItem        *list_item)
{
  GtkWidget *row = stamp_mail_row_new ();

  gtk_list_item_set_child (list_item, row);
}

static void
bind_listitem_cb (GtkListItemFactory *factory,
                  GtkListItem        *list_item)
{
  GtkWidget *row;
  StampMailConversationItemModel *model;

  row = gtk_list_item_get_child (list_item);
  model = STAMP_MAIL_CONVERSATION_ITEM_MODEL (gtk_list_item_get_item (list_item));

  stamp_mail_row_set_mail (STAMP_MAIL_ROW (row), model);
}

static void
on_selection_changed (GtkSelectionModel *selection,
                      guint               position,
                      guint               n_items,
                      gpointer            user_data)
{
  StampMailConversationItemModel *item = gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (selection));
  StampMailConversationList *self = STAMP_MAIL_CONVERSATION_LIST (user_data);

  g_signal_emit (self, signals[CONVERSATION_SELECTED], 0, stamp_mail_conversation_item_model_get_node (item));
}

static GtkWidget *
create_widget_func (gpointer  item,
                    gpointer  user_data)
{
  GtkWidget *row = stamp_mail_row_new ();
  StampMailConversationItemModel *model = STAMP_MAIL_CONVERSATION_ITEM_MODEL (item);

  stamp_mail_row_set_mail (STAMP_MAIL_ROW (row), model);

  return row;
}


static void
stamp_mail_conversation_list_init (StampMailConversationList *self)
{
/* #if 1 */
  GtkListItemFactory *factory;
  GtkSingleSelection *selection;

  self->mails = g_list_store_new (STAMP_TYPE_MAIL_CONVERSATION_ITEM_MODEL);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (setup_listitem_cb), NULL);
  g_signal_connect (factory, "bind", G_CALLBACK (bind_listitem_cb), NULL);
  /* g_signal_connect (factory, "unbind", G_CALLBACK (unbind_listitem_cb), NULL); */
  /* gtk_list_view_set_factory (GTK_LIST_VIEW (self->mail_list), factory); */

  selection = gtk_single_selection_new (G_LIST_MODEL (self->mails));
  /* gtk_list_view_set_model (GTK_LIST_VIEW (self->mail_list), GTK_SELECTION_MODEL (selection)); */

  self->mail_list = gtk_list_view_new (GTK_SELECTION_MODEL (selection), factory);
  gtk_widget_add_css_class (self->mail_list , "navigation-sidebar");
  gtk_box_append (GTK_BOX (self), self->mail_list);

  g_signal_connect (selection, "selection-changed", G_CALLBACK (on_selection_changed), self);
/* #else */
/*   self->mails = g_list_store_new (STAMP_TYPE_MAIL_CONVERSATION_ITEM_MODEL); */

/*   self->mail_list = gtk_list_box_new (); */
/*   gtk_box_append (GTK_BOX (self), self->mail_list); */
/*   gtk_list_box_bind_model (GTK_LIST_BOX (self->mail_list), G_LIST_MODEL (self->mails), create_widget_func, NULL, NULL); */
/* #endif */

}

GtkWidget *
stamp_mail_conversation_list_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_CONVERSATION_LIST, NULL);
}

static void
load_folder_int (GTask        *task,
             gpointer      object,
             gpointer      user_data,
             GCancellable *cancellable)
{
  StampMailConversationList *self = STAMP_MAIL_CONVERSATION_LIST (object);
  GHashTable *table = g_task_get_task_data (task);

  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  /* self->conver */
  g_list_store_remove_all (self->mails);

  self->cancellable = g_cancellable_new ();

  g_hash_table_foreach (table, load_folder, self);
}

void
stamp_mail_conversation_list_load_folder (StampMailConversationList *self,
                                          GHashTable                *table)
{
#if 0
  if (self->cancellable) {
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
  }

  /* self->conver */
  g_list_store_remove_all (self->mails);

  self->cancellable = g_cancellable_new ();

  g_hash_table_foreach (table, load_folder, self);
#else
  g_autoptr (GTask) task = NULL;
  g_return_if_fail (self);

  g_print ("%s: ENTER\n", G_STRFUNC);
  task = g_task_new (G_OBJECT (self), NULL, NULL, NULL);
  g_task_set_task_data (task, table, NULL);
  g_task_run_in_thread (task, load_folder_int);
  g_print ("%s: EXIT\n", G_STRFUNC);
#endif
}

void
stamp_mail_conversation_list_search_changed (StampMailConversationList *self)
{
  /* gtk_filter_changed (GTK_FILTER (self->filter), GTK_FILTER_CHANGE_DIFFERENT); */
}
