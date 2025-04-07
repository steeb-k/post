#include "stamp-account.h"

struct _StampAccount {
  GObject parent_instance;

  CamelService *service;
};

G_DEFINE_FINAL_TYPE (StampAccount, stamp_account, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_SERVICE,
  LAST_PROP
};

static GParamSpec *obj_properties[LAST_PROP];

static void
stamp_account_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  StampAccount *self = STAMP_ACCOUNT (object);

  switch (property_id) {
    case PROP_SERVICE:
      self->service = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
stamp_account_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void
stamp_account_init (StampAccount *self)
{
}

static void
stamp_account_class_init (StampAccountClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = stamp_account_set_property;
  object_class->get_property = stamp_account_get_property;

  obj_properties[PROP_SERVICE] =
    g_param_spec_object ("service",
                      NULL, NULL,
                      CAMEL_TYPE_SERVICE,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, obj_properties);
}

StampAccount *
stamp_account_new (CamelService *service)
{
  return g_object_new (STAMP_TYPE_ACCOUNT,
                       "service", service,
                       NULL);
}

CamelService *
stamp_account_get_service (StampAccount *self)
{
  return self->service;
}

const char *
stamp_account_get_name (StampAccount *self)
{
  return camel_service_get_uid (self->service);
}
