#include "stamp-session.h"

#include "stamp-account.h"

#include <libedataserverui4/libedataserverui4.h>

static StampSession *_session = NULL;

typedef struct {
  ESourceRegistry *registry;
  GList *accounts;
} StampSessionPrivate;

G_DEFINE_FINAL_TYPE_WITH_PRIVATE (StampSession, stamp_session, CAMEL_TYPE_SESSION)

enum {
  ACCOUNT_ADDED,
  LAST_SIGNAL,
};

static gint signals[LAST_SIGNAL] = { 0 };

static void
on_user_alert (CamelSession *session,
               CamelService *service,
               gint          type,
               char         *message)
{
  g_warning ("%s: %s", G_STRFUNC, message);
}

static void
on_network_changed (GNetworkMonitor *monitor,
                    gboolean         state,
                    gpointer         user_data)
{
  StampSession *self = STAMP_SESSION (user_data);

  g_print ("%s: State changed to %d\n", G_STRFUNC, state);
  camel_session_set_online (CAMEL_SESSION (self), state);
}

static void
stamp_session_init (StampSession *self)
{
  StampSessionPrivate *priv = stamp_session_get_instance_private (self);
  GNetworkMonitor *network_monitor = e_network_monitor_get_default ();

  camel_init (e_get_user_data_dir (), FALSE);
  g_signal_connect (G_OBJECT (self), "user-alert", G_CALLBACK (on_user_alert), NULL);

  camel_session_set_network_monitor (CAMEL_SESSION (self), network_monitor);
  g_signal_connect (network_monitor, "network-changed", G_CALLBACK (on_network_changed), self);
  camel_session_set_online (CAMEL_SESSION (self), TRUE);
}

typedef struct _TryCredentialsData {
  CamelService *service;
  const gchar *mechanism;
} TryCredentialsData;

static gboolean
try_credentials_sync (ECredentialsPrompter    *prompter,
                      ESource                 *source,
                      const ENamedParameters  *credentials,
                      gboolean                *out_authenticated,
                      gpointer                 user_data,
                      GCancellable            *cancellable,
                      GError                 **error)
{
  TryCredentialsData *data = user_data;
  gchar *credential_name = NULL;
  CamelAuthenticationResult result;

  g_return_val_if_fail (E_IS_SOURCE (source), FALSE);
  g_return_val_if_fail (credentials != NULL, FALSE);
  g_return_val_if_fail (out_authenticated != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (CAMEL_IS_SERVICE (data->service), FALSE);

  if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
	  ESourceAuthentication *auth_extension;

	  auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	  credential_name = e_source_authentication_dup_credential_name (auth_extension);

	  if (!credential_name || !*credential_name) {
		  g_free (credential_name);
		  credential_name = NULL;
	  }
  }

  camel_service_set_password (data->service, e_named_parameters_get (credentials,
	  credential_name ? credential_name : E_SOURCE_CREDENTIAL_PASSWORD));

  g_free (credential_name);

  result = camel_service_authenticate_sync (data->service, data->mechanism, cancellable, error);

  *out_authenticated = result == CAMEL_AUTHENTICATION_ACCEPTED;

  if (*out_authenticated) {
	  ESourceCredentialsProvider *credentials_provider;
	  ESource *cred_source;

	  credentials_provider = e_credentials_prompter_get_provider (prompter);
	  cred_source = e_source_credentials_provider_ref_credentials_source (credentials_provider, source);

	  if (cred_source)
		  e_source_invoke_authenticate_sync (cred_source, credentials, cancellable, NULL);

	  g_clear_object (&cred_source);
  }

  return result == CAMEL_AUTHENTICATION_REJECTED;
}


static gboolean
authenticate_sync (CamelSession  *session,
                   CamelService  *service,
                   const gchar   *mechanism,
                   GCancellable  *cancellable,
                   GError       **error)
{
  StampSession *self = STAMP_SESSION (session);
  CamelServiceAuthType *authtype = NULL;
  gboolean try_empty_password = FALSE;
  CamelAuthenticationResult result = CAMEL_AUTHENTICATION_REJECTED;
  GError *local_error = NULL;
  ESource *source;
  ESourceRegistry *registry;
  const gchar *uid;
  gboolean authenticated;
  StampSessionPrivate *priv = stamp_session_get_instance_private (self);

  registry = priv->registry;

  /* Treat a mechanism name of "none" as NULL. */
  if (g_strcmp0 (mechanism, "none") == 0)
    mechanism = NULL;

  /* APOP is one case where a non-SASL mechanism name is passed, so
   * don't bail if the CamelServiceAuthType struct comes back NULL. */
  if (mechanism != NULL)
    authtype = camel_sasl_authtype (mechanism);

  /* If the SASL mechanism does not involve a user
   * password, then it gets one shot to authenticate. */
  if (authtype != NULL && !authtype->need_password) {
    g_print ("SASL?");
    result = camel_service_authenticate_sync (service, mechanism, cancellable, &local_error);

    if (result == CAMEL_AUTHENTICATION_REJECTED)
      g_print ("FAILED\n");
  }

  /* Some SASL mechanisms can attempt to authenticate without a
   * user password being provided (e.g. single-sign-on credentials),
   * but can fall back to a user password.  Handle that case next. */
  if (mechanism != NULL) {
    CamelProvider *provider;
    CamelSasl *sasl;
    const gchar *service_name;

    provider = camel_service_get_provider (service);
    service_name = provider->protocol;

    /* XXX Would be nice if camel_sasl_try_empty_password_sync()
     *     returned the result in an "out" parameter so it's
     *     easier to distinguish errors from a "no" answer.
     * YYY There are precisely two states. Either we appear to
     *     have credentials (although we don't yet know if the
     *     server would *accept* them, of course). Or we don't
     *     have any credentials, and we can't even try. There
     *     is no middle ground.
     *     N.B. For 'have credentials', read 'the ntlm_auth
     *          helper exists and at first glance seems to
     *          be responding sanely'. */
    sasl = camel_sasl_new (service_name, mechanism, service);
    if (sasl != NULL) {
      try_empty_password =
        camel_sasl_try_empty_password_sync (
        sasl, cancellable, &local_error);
    g_object_unref (sasl);
    }
  }

  /* Abort authentication if we got cancelled.
   * Otherwise clear any errors and press on. */
  if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return FALSE;

  g_clear_error (&local_error);

  /* Find a matching ESource for this CamelService. */
  uid = camel_service_get_uid (service);
  source = e_source_registry_ref_source (registry, uid);

  if (source == NULL) {
    g_set_error (
      error, CAMEL_SERVICE_ERROR,
      CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
      ("No data source found for UID “%s”"), uid);
    return FALSE;
  }

  result = CAMEL_AUTHENTICATION_REJECTED;

  if (try_empty_password) {
	  result = camel_service_authenticate_sync (
		  service, mechanism, cancellable, error);
  }

  if (result == CAMEL_AUTHENTICATION_REJECTED) {
    ECredentialsPrompter *prompter;
			TryCredentialsData data;

    g_print ("Ask for creds\n");
    data.service = service;
    data.mechanism = mechanism;
    prompter = e_credentials_prompter_new(priv->registry);
    e_credentials_prompter_set_auto_prompt (prompter, TRUE);
    authenticated = e_credentials_prompter_loop_prompt_sync (prompter, source, E_CREDENTIALS_PROMPTER_PROMPT_FLAG_ALLOW_SOURCE_SAVE, try_credentials_sync, &data, NULL, error);

    /* e_credentials_prompter_ */
  } else {
		authenticated = (result == CAMEL_AUTHENTICATION_ACCEPTED);
	}


  return authenticated;
}

static
void on_get_folder_info (GObject      *store,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelFolderInfo) folder_info = camel_store_get_folder_info_finish (CAMEL_STORE (store), res, &error);

  if (error) {
    g_print ("%s: %s\n", G_STRFUNC, error->message);
    return;
  }

  g_print ("/*** \n");
  if (folder_info) {
      CamelFolderInfo *current_folder_info = CAMEL_FOLDER_INFO (folder_info);
            GPtrArray *messages;

      g_print ("=> %d\n", CAMEL_IS_FOLDER_INFO (folder_info));
      while (current_folder_info) {
          CamelFolder *inbox;

          g_print (" --> %s\n", current_folder_info->display_name);


          inbox = camel_store_get_folder_sync (CAMEL_STORE (store), current_folder_info->display_name, CAMEL_STORE_FOLDER_NONE, NULL, NULL);
          if (inbox) {
            CamelFolderSummary *summary;
            g_autoptr (GError) local_error = NULL;

            if (g_strcmp0 ("Junk-E-Mail", current_folder_info->display_name) == 0)
              camel_folder_refresh_info_sync (inbox, NULL, NULL);
            summary = camel_folder_get_folder_summary (inbox);
            if (summary) {
              g_print ("%s %d\n", camel_folder_get_display_name (inbox), camel_folder_summary_get_unread_count (summary));
            }
            messages = camel_folder_get_uids (inbox);
            g_print ("-> %d\n", messages->len);
          }
          current_folder_info = current_folder_info->next;
      }
  }
  g_print ("***/ \n");
}

static CamelService *
add_service (CamelSession       *session,
             const char         *uid,
             const char         *protocol,
             CamelProviderType   type,
             GError            **error)
{
  CamelService *service;
  StampSession *self = STAMP_SESSION (session);
  StampSessionPrivate *priv = stamp_session_get_instance_private (self);

  g_print ("%s: ENTER\n", G_STRFUNC);

  g_print ("%s: uid %s\n", G_STRFUNC, uid);
  service = CAMEL_SESSION_CLASS (stamp_session_parent_class)->add_service (
                                        session,
                                       uid,
                                       protocol,
                                       CAMEL_PROVIDER_STORE,
                                       NULL);

  if (CAMEL_IS_SERVICE (service)) {
    ESource *source = e_source_registry_ref_source (priv->registry, uid);
    const char *extension_name = e_source_camel_get_extension_name (protocol);
    ESource *extension_source = e_source_registry_find_extension (priv->registry, source, extension_name);

    if (extension_source)
      source = extension_source;

    e_source_camel_configure_service (source, service);

    g_object_bind_property (source, "display-name", service, "display-name", G_BINDING_SYNC_CREATE);
    if (CAMEL_IS_OFFLINE_STORE (service)) {
      StampAccount *account = stamp_account_new (CAMEL_SERVICE (service));
      priv->accounts = g_list_append (priv->accounts, account);
      g_print ("Adding account: %s\n", camel_service_get_display_name (service));
      g_signal_emit (self, signals[ACCOUNT_ADDED], 0, account, NULL);

      /* g_print ("%s:\n", camel_service_get_display_name (service)); */
      /* camel_offline_store_set_online_sync (CAMEL_OFFLINE_STORE (service), TRUE, NULL, NULL); */
      /* camel_service_connect_sync (service, NULL, NULL); */
      /* camel_store_synchronize_sync (CAMEL_STORE (service), FALSE, NULL, NULL); */
      /* camel_store_get_folder_info (CAMEL_STORE (service), NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, G_PRIORITY_DEFAULT, NULL, on_get_folder_info, NULL); */
    }
  }

  g_print ("%s: EXIT\n", G_STRFUNC);
  return service;
}

static CamelFilterDriver *
get_filter_driver (CamelSession  *session,
                   const char    *type,
                   CamelFolder   *folder,
                   GError       **error)
{
  CamelFilterDriver *filter_driver = camel_filter_driver_new (session);

  return filter_driver;
}

static void
stamp_session_class_init (StampSessionClass *klass)
{
  CamelSessionClass *session_class;

  session_class = CAMEL_SESSION_CLASS (klass);
  session_class->authenticate_sync = authenticate_sync;
  session_class->add_service = add_service;
  session_class->get_filter_driver = get_filter_driver;

  signals[ACCOUNT_ADDED] = g_signal_new ("account-added", G_OBJECT_CLASS_TYPE (klass),
                                 G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                 0, NULL, NULL, NULL,
                                 G_TYPE_NONE,
                                 1, STAMP_TYPE_ACCOUNT);

}

static void
add_source (gpointer source,
            gpointer user_data) {

  ESource *src = E_SOURCE (source);
  StampSession *self = STAMP_SESSION (user_data);
  ESourceMailAccount *extension;
  CamelService *service;
  const char *uid = e_source_get_uid (src);
  static gboolean en1 = FALSE;

  if (g_strcmp0 (uid, "vfolder") == 0) {
      return;
  }

  if (g_strcmp0 (uid, "71d13f6e1649721b66ee4768e7fff049652e956d") != 0) {
      return;
  }

  if (en1)
    return;

  en1 = TRUE;

  g_print ("%s: ENTER\n", G_STRFUNC);
  g_print ("%s: Adding source uid: %s\n", G_STRFUNC, uid);

  extension = e_source_get_extension (src, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
  add_service (CAMEL_SESSION (self),
                             uid,
                             e_source_backend_get_backend_name (E_SOURCE_BACKEND (extension)),
                             CAMEL_PROVIDER_STORE,
                             NULL);
  g_print ("%s: EXIT\n", G_STRFUNC);
}

static void
on_new_source_registry (GObject      *source_objet,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  StampSession *self = STAMP_SESSION (user_data);
  StampSessionPrivate *priv = stamp_session_get_instance_private (self);
  g_autoptr (GError) error = NULL;
  ESourceRegistry *registry = e_source_registry_new_finish (res, &error);
  GList *sources;

  g_print ("%s: ENTER\n", G_STRFUNC);
  if (error) {
    g_critical ("%s: %s", G_STRFUNC, error->message);
    /* g_task_return_error (task, g_steal_pointer (&error)); */
    return;
  }

  priv->registry = registry;

  sources = e_source_registry_list_sources (priv->registry, E_SOURCE_EXTENSION_MAIL_ACCOUNT);

  g_list_foreach (sources, add_source, self);
  g_print ("%s: EXIT\n", G_STRFUNC);
  g_signal_connect (priv->registry, "source-added", G_CALLBACK (add_source), self);

  /* g_task_return_boolean (task, TRUE); */
}

static void
start_session (GTask        *task,
               gpointer      object,
               gpointer      user_data,
               GCancellable *cancellable)
{
  StampSession *self = STAMP_SESSION (object);
  StampSessionPrivate *priv = stamp_session_get_instance_private (self);
  g_autoptr (GError) error = NULL;

  g_print ("%s: ENTER\n", G_STRFUNC);
  if (priv->registry) {
    g_warning ("CamelSession is already started\n");
    g_task_return_boolean (task, TRUE);
    return;
  }

  e_source_registry_new (cancellable, on_new_source_registry, self);
  g_print ("%s: EXIT\n", G_STRFUNC);
}

void
stamp_session_start (StampSession        *self,
                     GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  g_return_if_fail (self);

  g_print ("%s: ENTER\n", G_STRFUNC);
  task = g_task_new (G_OBJECT (self), NULL, callback, user_data);
  g_task_run_in_thread (task, start_session);
  g_print ("%s: EXIT\n", G_STRFUNC);
}

StampSession *
stamp_session_get_default (void)
{
  if (!_session) {
    g_autofree char *data_dir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_data_dir (), "stamp", NULL);
    g_autofree char *cache_dir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_cache_dir (), "stamp", NULL);

    _session = g_object_new (STAMP_TYPE_SESSION,
                             "user-data-dir", data_dir,
                             "user-cache-dir", cache_dir,
                             NULL);
  }

  return _session;
}

GList *
stamp_session_get_accounts (StampSession *self)
{
  StampSessionPrivate *priv = stamp_session_get_instance_private (self);
  GList *list = NULL;

  list = g_list_copy (priv->accounts);

  return list;
}
