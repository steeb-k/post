#pragma once

#include <gdk/gdk.h>
#include <glib-object.h>

#include <camel/camel.h>

G_BEGIN_DECLS

#define STAMP_TYPE_ACCOUNT (stamp_account_get_type ())

G_DECLARE_FINAL_TYPE (StampAccount, stamp_account, STAMP, ACCOUNT, GObject);

StampAccount *
stamp_account_new (CamelService *service);

CamelService *
stamp_account_get_service (StampAccount *self);

const char *
stamp_account_get_name (StampAccount *self);

G_END_DECLS

