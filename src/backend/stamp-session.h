#pragma once

#include <camel/camel.h>

#include <libebackend/libebackend.h>
#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

#define STAMP_TYPE_SESSION (stamp_session_get_type ())
G_DECLARE_FINAL_TYPE (StampSession, stamp_session, STAMP, SESSION, CamelSession);

struct _StampSession {
  CamelSession parent_instance;
};

StampSession *
stamp_session_get_default (void);

void
stamp_session_start (StampSession        *self,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data);

GList *
stamp_session_get_accounts (StampSession *self);

G_END_DECLS

