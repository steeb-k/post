#include "stamp-mail-message-header.h"
#include "stamp-time-helpers.h"

typedef struct {
  GtkWidget *avatar;
  GtkWidget *from_full;
  GtkWidget *from_short;
  gboolean collapsed;
  gboolean is_unread;
  gboolean is_read;
  GtkWidget *attachment_icon;
  GtkWidget *body_preview;

  GtkWidget *to;
  GtkWidget *cc;
  GtkWidget *date;
  gboolean compact;
} StampMailMessageHeaderPrivate;

#define GET_PRIVATE(o) stamp_mail_message_header_get_instance_private (o)

G_DEFINE_TYPE_WITH_PRIVATE (StampMailMessageHeader, stamp_mail_message_header, ADW_TYPE_ACTION_ROW)

enum {
  PROP_0,
  PROP_COLLAPSED,
  PROP_COMPACT,
  PROP_IS_UNREAD,
  PROP_IS_READ,
  LAST_PROP
};

static void
update_visibility (StampMailMessageHeader *self);

static void
stamp_mail_message_header_init (StampMailMessageHeader *self)
{
  StampMailMessageHeaderPrivate *priv = GET_PRIVATE (self);

  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
stamp_mail_message_header_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  StampMailMessageHeader *self = STAMP_MAIL_MESSAGE_HEADER (object);
  StampMailMessageHeaderPrivate *priv = GET_PRIVATE (self);

  switch (property_id) {
    case PROP_COLLAPSED:
      g_value_set_boolean (value, priv->collapsed);
      break;
    case PROP_COMPACT:
      g_value_set_boolean (value, priv->compact);
      break;
    case PROP_IS_UNREAD:
      g_value_set_boolean (value, priv->is_unread);
      break;
    case PROP_IS_READ:
      g_value_set_boolean (value, priv->is_read);
      break;
    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_mail_message_header_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  StampMailMessageHeader *self = STAMP_MAIL_MESSAGE_HEADER (object);
  StampMailMessageHeaderPrivate *priv = GET_PRIVATE (self);

  switch (property_id) {
    case PROP_COLLAPSED:
      priv->collapsed = g_value_get_boolean (value);
      update_visibility (self);
      break;
    case PROP_COMPACT:
      priv->compact = g_value_get_boolean (value);
      adw_avatar_set_size (ADW_AVATAR (priv->avatar), priv->compact ? 36 : 48);
      /* stamp_avatar_set_size (STAMP_AVATAR (priv->avatar), priv->compact ? 36 : 48); */
      update_visibility (self);
      break;
    case PROP_IS_UNREAD:
      priv->is_unread = g_value_get_boolean (value);
      break;
    case PROP_IS_READ:
      priv->is_read = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_mail_message_header_class_init (StampMailMessageHeaderClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/message-list/stamp-mail-message-header.ui");

  gobject_class->get_property = stamp_mail_message_header_get_property;
  gobject_class->set_property = stamp_mail_message_header_set_property;

  gtk_widget_class_bind_template_child_private (widget_class, StampMailMessageHeader, avatar);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailMessageHeader, from_full);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailMessageHeader, from_short);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailMessageHeader, to);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailMessageHeader, cc);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailMessageHeader, date);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailMessageHeader, attachment_icon);
  gtk_widget_class_bind_template_child_private (widget_class, StampMailMessageHeader, body_preview);

  g_object_class_install_property (gobject_class, PROP_COLLAPSED,
                                   g_param_spec_boolean ("collapsed",
                                                         NULL,
                                                         NULL,
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COMPACT,
                                   g_param_spec_boolean ("compact",
                                                         NULL,
                                                         NULL,
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IS_UNREAD,
                                   g_param_spec_boolean ("is-unread",
                                                         NULL,
                                                         NULL,
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IS_UNREAD,
                                   g_param_spec_boolean ("is-read",
                                                         NULL,
                                                         NULL,
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

GtkWidget *
stamp_mail_message_header_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_MESSAGE_HEADER, NULL);
}

static void
update_visibility (StampMailMessageHeader *self)
{
  StampMailMessageHeaderPrivate *priv = GET_PRIVATE (self);

  if (priv->collapsed) {
    gtk_widget_set_visible (priv->to, FALSE);
    gtk_widget_set_visible (priv->cc, FALSE);
  } else {
    gtk_widget_set_visible (priv->to, strlen (gtk_label_get_text (GTK_LABEL (priv->to))) != 0);
    gtk_widget_set_visible (priv->cc, strlen (gtk_label_get_text (GTK_LABEL (priv->cc))) != 0);
  }
}

void stamp_mail_message_header_set_mail (StampMailMessageHeader *self,
                                         const CamelMessageInfo       *message_info)
{
  StampMailMessageHeaderPrivate *priv = GET_PRIVATE (self);
  /* StampUser *sender = stamp_mail_get_sender (mail); */
  /* GList *receivers = stamp_mail_get_receiver (mail); */
  g_autofree char *markup = NULL;
  const char *sender = NULL;
  const char *to = NULL;
  CamelInternetAddress *address = camel_internet_address_new ();
  const char *ia_name;
  const char *ia_address;

  if (camel_address_decode (CAMEL_ADDRESS (address), camel_message_info_get_from (message_info)) > 0) {
    camel_internet_address_get (address, 0, &ia_name, &ia_address);
    if (g_strcmp0 (ia_name, "") != 0) {
      sender = ia_name;
    } else {
      sender = ia_address;
    }
  }

  gtk_inscription_set_text (GTK_INSCRIPTION (priv->body_preview), camel_message_info_get_preview (message_info));

  /* Avatar */
  if (sender) {
    g_print ("Sender: %s\n", sender);
  /*   stamp_avatar_set_user (STAMP_AVATAR (priv->avatar), sender); */
    adw_avatar_set_text (ADW_AVATAR (priv->avatar), sender);
    adw_avatar_set_show_initials (ADW_AVATAR (priv->avatar), TRUE);
    /* adw_avatar_set_custom_image (ADW_AVATAR (priv->avatar), GDK_PAINTABLE (stamp_user_get_photo (sender))); */

    gtk_label_set_text (GTK_LABEL (priv->from_short), sender);
    markup = g_strdup_printf ("<b>%s</b> <small>(%s)</small>", ia_name, ia_address);
    gtk_label_set_markup (GTK_LABEL (priv->from_full), markup);
    /* gtk_label_set_markup (GTK_LABEL (priv->from_short), sender); */
  }

  address = camel_internet_address_new ();
  if (camel_address_decode (CAMEL_ADDRESS (address), camel_message_info_get_to (message_info)) > 0) {
    camel_internet_address_get (address, 0, &ia_name, &ia_address);
    if (g_strcmp0 (ia_name, "") != 0) {
      to = ia_name;
    } else {
      to = ia_address;
    }
  }

  if (to) {
    g_autofree char *tmp = g_markup_printf_escaped ("<span alpha=\"55%%\">To:</span> %s", to);
    gtk_label_set_markup (GTK_LABEL (priv->to), tmp);
  }
  /* if (receivers) { */
  /*   g_autoptr (GString) to_label = g_string_new ("<span alpha=\"55%%\">To:</span> "); */

  /*   for (GList *iter = receivers; iter && iter->data; iter = iter->next) { */
  /*     StampUser *receiver = STAMP_USER (iter->data); */

  /*     g_string_append (to_label, stamp_user_get_name (receiver)); */
  /*     if (iter->next) */
  /*       g_string_append (to_label, "; "); */
  /*   } */

  /*   gtk_label_set_markup (GTK_LABEL (priv->to), to_label->str); */
  /* } */

  /* priv->is_unread = stamp_mail_get_unread (mail); */
  /* priv->is_read = !priv->is_unread; */

  gtk_label_set_text (GTK_LABEL (priv->date), stamp_time_helpers_utf_friendly_time (camel_message_info_get_date_received (message_info)));

  /* gtk_widget_set_visible (priv->attachment_icon, stamp_mail_has_attachment (mail)); */

  /* update_visibility (self); */
}
