#include "stamp-mail-conversation-item-model.h"

struct _StampMailConversationItemModel
{
  GObject parent_instance;

  CamelFolderThreadNode *thread_node;
  guint timestamp;
  char *senders;
};

G_DEFINE_FINAL_TYPE (StampMailConversationItemModel, stamp_mail_conversation_item_model, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_MESSAGE_INFO,
  LAST_PROP
};

static void
stamp_mail_conversation_item_model_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  switch (property_id) {
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_mail_conversation_item_model_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  StampMailConversationItemModel *self = STAMP_MAIL_CONVERSATION_ITEM_MODEL (object);

  switch (property_id) {
    case PROP_MESSAGE_INFO:
      /* self->message_info = g_value_get_object (value); */
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


void
stamp_mail_conversation_item_model_class_init (StampMailConversationItemModelClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = stamp_mail_conversation_item_model_get_property;
  gobject_class->set_property = stamp_mail_conversation_item_model_set_property;

  g_object_class_install_property (gobject_class, PROP_MESSAGE_INFO,
                                   g_param_spec_object ("thread-node",
                                                        NULL,
                                                        NULL,
                                                        CAMEL_TYPE_DB,
                                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
stamp_mail_conversation_item_model_init (StampMailConversationItemModel *self)
{
}

static gint64
get_newest_timestamp (CamelFolderThreadNode *node,
                      gint64                 highest)
{
  const CamelMessageInfo *message;
  gint64 time = highest;

  if (!node)
    return time;

  message = node->message;
  if (message) {
    time = MAX (time, camel_message_info_get_date_received (message));
    time = MAX (time, camel_message_info_get_date_sent (message));
  }

  for (CamelFolderThreadNode *child = node->child; child; child = child->next) {
    time = get_newest_timestamp (child, time);
  }

  return time;
}

StampMailConversationItemModel *
stamp_mail_conversation_item_model_new (CamelFolderThreadNode *thread_node)
{
  StampMailConversationItemModel *item = g_object_new (STAMP_TYPE_MAIL_CONVERSATION_ITEM_MODEL,
                                                       /* "thread-node", thread_node, */
                                                       NULL);

  item->thread_node = thread_node;
  item->timestamp = get_newest_timestamp (item->thread_node, -1);

  return item;
}

const char *
stamp_mail_conversation_item_model_get_subject (StampMailConversationItemModel *self)
{
  const CamelMessageInfo *info = self->thread_node->message;

  if (!info)
    return ("Unknown");

  return camel_message_info_get_subject (info);
}

CamelFolderThreadNode *
stamp_mail_conversation_item_model_get_node (StampMailConversationItemModel *self)
{
  return self->thread_node;
}

char *
stamp_mail_conversation_item_model_get_from (StampMailConversationItemModel *self)
{
  CamelFolderThreadNode *current_node = self->thread_node;
  g_autoptr (GHashTable) senders = NULL;

  /* if (self->senders) */
  /*   return self->senders; */

  senders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  while (current_node) {
    const CamelMessageInfo *message = current_node->message;

    if (message) {
      CamelInternetAddress * address = camel_internet_address_new ();

      if (camel_address_decode (CAMEL_ADDRESS (address), camel_message_info_get_from (message)) > 0) {
        const char *ia_name;
        const char *ia_address;
        const char *sender;

        camel_internet_address_get (address, 0, &ia_name, &ia_address);
        if (g_strcmp0 (ia_name, "") != 0) {
          sender = ia_name;
        } else {
          sender = ia_address;
        }

        return g_strdup (sender);
        g_hash_table_add (senders, g_strdup (sender));
        /* if (!g_ptr_array_find (senders, sender, NULL)) { */
          /* g_ptr_array_add (senders, sender); */

          /* break; */
        /* } */
      }
    }

    current_node = current_node->next;
  }

  if (g_hash_table_size (senders) > 0) {
    GString *out = g_string_new (NULL);;
    for (GList *iter = g_hash_table_get_keys (senders); iter; iter = iter->next) {
      /* self->senders = g_strjoinv (",  ", (char **) g_hash_table_get_keys_as_array (senders, NULL)); */
      g_string_append (out, iter->data);
    }
    self->senders = g_string_free (out, FALSE);
  } else {
    self->senders = ("Unknown");
  }

  return self->senders;

}

guint
stamp_mail_conversation_item_model_get_date (StampMailConversationItemModel *self)
{
  return self->timestamp;
}

static guint
count_thread_messages (CamelFolderThreadNode *node)
{
  guint num = 1;

  for (CamelFolderThreadNode *child = node->child; child; child = child->next) {
    num += count_thread_messages (child);
  }

  return num;
}

guint
stamp_mail_conversation_item_model_get_num_messages (StampMailConversationItemModel *self)
{
  return count_thread_messages (self->thread_node);
}

static gboolean
has_thread_flag (CamelFolderThreadNode *node,
                 CamelMessageFlags      flag)
{
  gboolean has_flag;

  if (!node)
    return FALSE;

  has_flag = !(camel_message_info_get_flags (node->message) & flag);

  if (!has_flag) {
    for (CamelFolderThreadNode *child = node->child; child; child = child->next) {
      has_flag = has_thread_flag (child, flag);
      if (has_flag)
        break;
    }
  }

  return has_flag;
}

gboolean
stamp_mail_conversation_item_model_get_unread (StampMailConversationItemModel *self)
{
  return has_thread_flag (self->thread_node, CAMEL_MESSAGE_SEEN);
}

gboolean
stamp_mail_conversation_item_model_has_attachment (StampMailConversationItemModel *self)
{
  return !has_thread_flag (self->thread_node, CAMEL_MESSAGE_ATTACHMENTS);
}

gboolean
stamp_mail_conversation_item_model_get_flagged (StampMailConversationItemModel *self)
{
  return !has_thread_flag (self->thread_node, CAMEL_MESSAGE_FLAGGED);
}

const char *
stamp_mail_conversation_item_model_get_preview (StampMailConversationItemModel *self)
{
  const CamelMessageInfo *info = self->thread_node->message;

  return camel_message_info_get_preview (info);
}
