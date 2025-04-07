#include "stamp-mail-conversation-list-store.h"
#include "stamp-mail-conversation-item-model.h"

struct _StampMailConversationListStore
{
  GObject parent_instance;

  GList *data;
  guint n_items;
};

static void stamp_mail_conversation_list_store_list_model_init (GListModelInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (StampMailConversationListStore, stamp_mail_conversation_list_store, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, stamp_mail_conversation_list_store_list_model_init))

void
stamp_mail_conversation_list_store_class_init (StampMailConversationListStoreClass *klass)
{
}

void
stamp_mail_conversation_list_store_init (StampMailConversationListStore *self)
{
  /* self->data = g_list_new (); */
}

GType
stamp_mail_conversation_list_store_get_item_type (GListModel *model)
{
  return STAMP_TYPE_MAIL_CONVERSATION_ITEM_MODEL;
}

static void
count_data (gpointer data,
            gpointer user_data)
{
  StampMailConversationListStore *self = data;
  self->n_items++;
}

guint
stamp_mail_conversation_list_store_get_n_items (GListModel *model)
{
  StampMailConversationListStore *self = STAMP_MAIL_CONVERSATION_LIST_STORE (model);

  g_print ("COUNTING\n");
  g_list_foreach (self->data, count_data, self);

  return self->n_items;
}

static void
stamp_mail_conversation_list_store_list_model_init (GListModelInterface *iface)
{
  iface->get_item_type = stamp_mail_conversation_list_store_get_item_type;
  iface->get_n_items = stamp_mail_conversation_list_store_get_n_items;
}
