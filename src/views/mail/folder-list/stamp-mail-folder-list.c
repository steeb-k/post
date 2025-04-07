#include "stamp-mail-folder-list.h"

#include "stamp-item-model.h"
#include "stamp-folder-item-model.h"
#include "stamp-account-item-model.h"

#include "stamp-time-helpers.h"


#include "stamp-mail-folder-row.h"
#include "stamp-session.h"

struct _StampMailFolderList
{
  AdwBin parent_instance;

  GListStore *root_model;
  GtkWidget *folders_list;
  gboolean already_selected;
  GtkSingleSelection *selection;
  GSettings *settings;
};

G_DEFINE_FINAL_TYPE (StampMailFolderList, stamp_mail_folder_list, ADW_TYPE_BIN)

enum {
  FOLDER_SELECTED,
  LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

/* static void */
/* add_account (gpointer      data, */
/*              StampAccount *account, */
/*              gpointer      user_data) */
/* { */
  /* StampMailView *self = STAMP_MAIL_VIEW (user_data); */
  /* StampAccountItemModel *account_item = stamp_account_item_model_new (account); */
  /* GList *accounts = stamp_goa_manager_get_accounts (stamp_shell_get_goa_manager (stamp_shell_get_default ())); */

  /* g_print ("%s: ENTER\n", G_STRFUNC); */
  /* if (g_list_length (accounts) > 1 && !STAMP_IS_SESSION_ITEM_MODEL (g_list_model_get_item (G_LIST_MODEL (self->root_model), 0))) { */
  /*   g_list_store_insert (self->root_model, 0, self->session_item); */
  /* } */
  /* stamp_session_item_model_add_account (self->session_item, account); */

  /* g_list_store_append (self->root_model, account_item); */
  /* self->accounts = g_list_append (self->accounts, account); */
  /* self->accounts = g_list_append (self->accounts, account); */
/* } */


/* static void */
/* stamp_mail_view_add_account (gpointer      data, */
/*                              gpointer      user_data) */
/* { */
/*   add_account (NULL, data, user_data); */
/* } */

/* static gboolean */
/* account_compare (gconstpointer a, */
/*                  gconstpointer b) */
/* { */
  /* StampAccountItemModel *account_a = STAMP_ACCOUNT_ITEM_MODEL (a); */
  /* StampAccountItemModel *account_b = STAMP_ACCOUNT_ITEM_MODEL (b); */

  /* return stamp_account_item_model_get_account (account_a) == stamp_account_item_model_get_account (account_b); */
/*   return TRUE; */
/* } */

/* static void */
/* remove_account (gpointer      data, */
/*                 StampAccount *account, */
/*                 gpointer      user_data) */
/* { */
  /* StampMailView *self = STAMP_MAIL_VIEW (user_data); */
  /* StampAccountItemModel *account_item = stamp_account_item_model_new (account); */
  /* guint pos; */

  /* g_print ("%s: ENTER\n", G_STRFUNC); */
  /* if (!g_list_find (self->accounts, account)) */
  /*   return; */

  /* if (g_list_length (self->accounts) - 1 == 1) { */
  /*   g_list_store_remove (self->root_model, 0); */
  /* } */

  /* if (g_list_store_find_with_equal_func (self->root_model, account_item, account_compare, &pos)) */
  /*   g_list_store_remove (self->root_model, pos); */

  /* self->accounts = g_list_remove (self->accounts, account); */
/* } */



static void
on_selection_changed (GtkSelectionModel *selection,
                      guint               position,
                      guint               n_items,
                      gpointer            user_data)
{
  GtkTreeListRow *row = GTK_TREE_LIST_ROW (gtk_single_selection_get_selected_item (GTK_SINGLE_SELECTION (selection)));
  StampItemModel *item = gtk_tree_list_row_get_item (row);
  StampMailFolderList *self = STAMP_MAIL_FOLDER_LIST (user_data);

  g_print ("%s: ENTER %d %d %d\n", G_STRFUNC,
           STAMP_IS_FOLDER_ITEM_MODEL (item),
           STAMP_IS_ACCOUNT_ITEM_MODEL (item),
           STAMP_IS_ITEM_MODEL (item)
           );

  if (STAMP_IS_FOLDER_ITEM_MODEL (item)) {
    GHashTable *folder_name_per_account_uid = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    CamelFolderInfo *folder_info = stamp_folder_item_model_get_folder_info (STAMP_FOLDER_ITEM_MODEL (item));
    StampAccount *account = stamp_folder_item_model_get_account (STAMP_FOLDER_ITEM_MODEL (item));

    g_print ("Loading folder\n");

    g_print ("%s: Setting %s %s\n", G_STRFUNC, stamp_account_get_name (account), folder_info->full_name);
    g_settings_set (self->settings, "selected-folder", "(ss)", stamp_account_get_name (account), folder_info->full_name);

    g_hash_table_insert (folder_name_per_account_uid, account, g_strdup (folder_info->full_name));
    g_signal_emit (self, signals[FOLDER_SELECTED], 0, folder_name_per_account_uid);
  }
}


static void
on_account_added (GObject      *object,
                  StampAccount *account,
                  gpointer      user_data)
{
  StampMailFolderList *self = STAMP_MAIL_FOLDER_LIST (user_data);
  /* StampSession *session = STAMP_SESSION (object); */
  StampAccountItemModel *account_item = stamp_account_item_model_new (account);
  g_print ("MOEP\n  ");

  /* if (g_list_length (stamp_session_get_accounts (session)  */
  g_list_store_append (self->root_model, account_item);
}


static void
on_session_started (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  StampSession *session = STAMP_SESSION (source_object);

  if (g_list_length (stamp_session_get_accounts (session)) > 0) {
    g_print ("We do have accounts\n");
  } else {
    g_print ("We have no accounts\n");
  }
}

static char **
g_strv_remove (const char * const *strv,
               const char         *str)
{
  char **new_strv;
  char **n;
  const char * const *s;
  guint len;

  if (!g_strv_contains (strv, str))
    return g_strdupv ((char **)strv);

  /* Needs room for one fewer string than before, plus one for trailing NULL. */
  len = g_strv_length ((char **)strv);
  new_strv = g_malloc ((len - 1 + 1) * sizeof (char *));
  n = new_strv;
  s = strv;

  while (*s != NULL) {
    if (strcmp (*s, str) != 0) {
      *n = g_strdup (*s);
      n++;
    }
    s++;
  }
  new_strv[len - 1] = NULL;

  return new_strv;
}

static char **
g_strv_append (const char * const *strv,
               const char         *str)
{
  char **new_strv;
  char **n;
  const char * const *s;
  guint len;

  if (g_strv_contains (strv, str))
    return g_strdupv ((char **)strv);

  /* Needs room for one fewer string than before, plus one for trailing NULL. */
  len = g_strv_length ((char **)strv) + 2;
  new_strv = g_malloc (len * sizeof (char *));
  n = new_strv;
  s = strv;

  while (*s != NULL) {
    *n = g_strdup (*s);
    n++;
    s++;
  }

  new_strv[len - 2] = g_strdup (str);
  new_strv[len - 1] = NULL;

  return new_strv;
}

static void
on_row_expanded (GtkTreeListRow *row,
                 GParamSpec     *pspec,
                 gpointer        user_data)
{
  GSettings *account_settings;
  g_autofree char *settings_path = NULL;
  StampItemModel *item_model = STAMP_ITEM_MODEL (gtk_tree_list_row_get_item (row));
  StampAccount *account = stamp_folder_item_model_get_account (STAMP_FOLDER_ITEM_MODEL (item_model));
  CamelFolderInfo *folder_info = stamp_folder_item_model_get_folder_info (STAMP_FOLDER_ITEM_MODEL (item_model));
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
  GStrv folders = NULL;
  g_auto (GStrv) new_folders = NULL;

  g_print ("%s: ENTER\n", G_STRFUNC);

  settings_path = g_strconcat ("/org/tabos/stamp/mail/accounts/", stamp_account_get_name (account), "/", NULL);
  account_settings = g_settings_new_with_path ("org.tabos.stamp.mail.accounts", settings_path);

  folders = g_settings_get_strv (account_settings, "expanded-folders");

  if (gtk_tree_list_row_get_expanded (row)) {
    new_folders = g_strv_append ((const char * const *)folders, folder_info->full_name);
  } else {
    new_folders = g_strv_remove ((const char * const *)folders, folder_info->full_name);
  }

  g_settings_set_strv (account_settings, "expanded-folders", (const char * const *)new_folders);
}

static void
bind_folder_cb (GtkListItemFactory *factory,
                GtkListItem        *list_item,
                gpointer            user_data)
{
  StampMailFolderList *self = STAMP_MAIL_FOLDER_LIST (user_data);
  GtkTreeListRow *row;
  GtkWidget *expander;
  StampItemModel *item_model;
  StampMailFolderRow *folder_row;
  GSettings *account_settings;
  g_autofree char *settings_path = NULL;

  expander = gtk_list_item_get_child (list_item);
  row = GTK_TREE_LIST_ROW (gtk_list_item_get_item (list_item));
  item_model = STAMP_ITEM_MODEL (gtk_tree_list_row_get_item (row));

  gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), row);

  if (STAMP_IS_ACCOUNT_ITEM_MODEL (item_model)) {
    /* GtkWidget *child = gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander)); */
    /* StampAccount *account = stamp_account_item_model_get_account (STAMP_ACCOUNT_ITEM_MODEL (item_model)) */

    /* settings_path = g_strconcat ("/org/tabos/stamp/mail/accounts/", stamp_account_get_name (account), "/", NULL); */
    /* account_settings = g_settings_new_with_path ("org.tabos.stamp.mail.accounts", settings_path); */

    folder_row = STAMP_MAIL_FOLDER_ROW (gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander)));
    stamp_mail_folder_row_bind (folder_row, item_model);
  } else if (STAMP_IS_FOLDER_ITEM_MODEL (item_model)) {
    StampAccount *account = stamp_folder_item_model_get_account (STAMP_FOLDER_ITEM_MODEL (item_model));
    CamelFolderInfo *folder_info = stamp_folder_item_model_get_folder_info (STAMP_FOLDER_ITEM_MODEL (item_model));

    settings_path = g_strconcat ("/org/tabos/stamp/mail/accounts/", stamp_account_get_name (account), "/", NULL);

    g_print ("%s: %s\n", G_STRFUNC, settings_path);
    account_settings = g_settings_new_with_path ("org.tabos.stamp.mail.accounts", settings_path);

    folder_row = STAMP_MAIL_FOLDER_ROW (gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander)));

    stamp_mail_folder_row_bind (folder_row, item_model);

    if (!self->already_selected) {
      g_autofree char *account_name = NULL;
      g_autofree char *folder_name = NULL;

      g_settings_get (self->settings, "selected-folder", "(ss)", &account_name, &folder_name);
      if (g_strcmp0 (stamp_account_get_name (account), account_name) == 0 && g_strcmp0 (folder_info->full_name, folder_name) == 0) {
        g_print ("%s: account %s\n", G_STRFUNC, account_name);
        g_print ("%s: folder %s\n", G_STRFUNC, folder_name);

        gtk_single_selection_set_selected (self->selection, gtk_list_item_get_position (list_item));
        self->already_selected = TRUE;
      }
    }

    if (g_strv_contains ((const char **)g_settings_get_strv (account_settings, "expanded-folders"), folder_info->full_name)) {
      g_print ("%s: expand %s\n", G_STRFUNC, folder_info->full_name);
      /* if (gtk_tree_list_row_get_children (row)) */
        /* gtk_tree_list_row_set_expanded (row, TRUE); */
    }

    g_signal_connect_object (row, "notify::expanded", G_CALLBACK (on_row_expanded), self, 0);
  }
  /* StampMailView *self = STAMP_MAIL_VIEW (user_data); */
  /* GtkTreeListRow *row; */
  /* GtkWidget *expander; */
  /* StampItemModel *item_model; */
  /* StampMailFolderRow *folder_row; */
  /* GSettings *account_settings; */
  /* g_autofree char *settings_path = NULL; */

  /* expander = gtk_list_item_get_child (list_item); */
  /* row = GTK_TREE_LIST_ROW (gtk_list_item_get_item (list_item)); */
  /* item_model = STAMP_ITEM_MODEL (gtk_tree_list_row_get_item (row)); */

  /* gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (expander), row); */

  /* g_print ("%s: %d %d\n", G_STRFUNC, STAMP_IS_ACCOUNT_ITEM_MODEL (item_model), STAMP_IS_FOLDER_ITEM_MODEL (item_model)); */
  /* if (STAMP_IS_ACCOUNT_ITEM_MODEL (item_model)) { */
  /*   GtkWidget *child = gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander)); */
  /*   StampAccount *account = stamp_account_item_model_get_account (STAMP_ACCOUNT_ITEM_MODEL (item_model)); */

  /*   settings_path = g_strconcat ("/org/tabos/stamp/mail/accounts/", stamp_account_get_name (account), "/", NULL); */
  /*   account_settings = g_settings_new_with_path ("org.tabos.stamp.mail.accounts", settings_path); */

    /* stamp_folder_list_item_bind (child, item); */
  /*   g_settings_bind (account_settings, "expanded", row, "expanded", G_SETTINGS_BIND_DEFAULT | G_SETTINGS_BIND_GET_NO_CHANGES); */
    /* StampFolderItemModel *folder_item = STAMP_FOLDER_ITEM_MODEL (child); */
  /*   folder_row = STAMP_MAIL_FOLDER_ROW (gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander))); */
  /*   stamp_mail_folder_row_bind (folder_row, item_model); */
  /* } else if (STAMP_IS_FOLDER_ITEM_MODEL (item_model)) { */
  /*   StampAccount *account = stamp_folder_item_model_get_account (STAMP_FOLDER_ITEM_MODEL (item_model)); */
  /*   StampMailFolder *folder = stamp_folder_item_model_get_folder (STAMP_FOLDER_ITEM_MODEL (item_model)); */

  /*   settings_path = g_strconcat ("/org/tabos/stamp/mail/accounts/", stamp_account_get_name (account), "/", NULL); */
  /*   account_settings = g_settings_new_with_path ("org.tabos.stamp.mail.accounts", settings_path); */

  /*   folder_row = STAMP_MAIL_FOLDER_ROW (gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander))); */

  /*   stamp_mail_folder_row_bind (folder_row, item_model); */

  /*   if (!self->already_selected) { */
  /*     g_autofree char *account_name = NULL; */
  /*     g_autofree char *folder_name = NULL; */

  /*     g_settings_get (self->settings, "selected-folder", "(ss)", &account_name, &folder_name); */
  /*     if (g_strcmp0 (stamp_account_get_name (account), account_name) == 0 && g_strcmp0 (stamp_mail_folder_get_name (folder), folder_name) == 0) { */
  /*       g_print ("%s: account %s\n", G_STRFUNC, account_name); */
  /*       g_print ("%s: folder %s\n", G_STRFUNC, folder_name); */

  /*       gtk_single_selection_set_selected (self->folder_selection, gtk_list_item_get_position (list_item)); */
  /*       self->already_selected = TRUE; */
  /*     } */
  /*   } */

  /*   g_print ("%s: %s\n", G_STRFUNC, stamp_mail_folder_get_name (folder)); */
  /*   if (g_strv_contains ((const char **)g_settings_get_strv (account_settings, "expanded-folders"), stamp_mail_folder_get_name (folder))) { */
  /*     g_print ("%s: expand %s\n", G_STRFUNC, stamp_mail_folder_get_name (folder)); */
      /* if (gtk_tree_list_row_get_children (row)) */
  /*       gtk_tree_list_row_set_expanded (row, TRUE); */
  /*   } */

  /*   g_signal_connect_object (row, "notify::expanded", G_CALLBACK (on_row_expanded), self, 0); */
  /* } else if (STAMP_IS_SESSION_ITEM_MODEL (item_model)) { */
  /*   folder_row = STAMP_MAIL_FOLDER_ROW (gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander))); */

  /*   stamp_mail_folder_row_bind (folder_row, item_model); */
  /* } else if (STAMP_IS_GROUPED_FOLDER_ITEM_MODEL (item_model)) { */
  /*   StampGroupedFolderItemModel *folder_item = STAMP_GROUPED_FOLDER_ITEM_MODEL (item_model); */

  /*   folder_row = STAMP_MAIL_FOLDER_ROW (gtk_tree_expander_get_child (GTK_TREE_EXPANDER (expander))); */
  /*   stamp_mail_folder_row_bind (folder_row, item_model); */
  /* } */
}


static GListModel *
get_child_model (void     *item,
                 gpointer  user_data)
{
  if (STAMP_IS_ITEM_MODEL (item)) {
    GListModel *model = G_LIST_MODEL (stamp_item_model_get_folder_list (STAMP_ITEM_MODEL (item)));

    return model ? g_object_ref (model) : NULL;
  }

  return NULL;
}

static gint
folders_compare (gconstpointer a,
                 gconstpointer b,
                 gpointer      user_data)
{
  StampItemModel *item_a = (StampItemModel *)(a);
  StampItemModel *item_b = (StampItemModel *)(b);

  if (STAMP_IS_FOLDER_ITEM_MODEL (item_a) && STAMP_IS_FOLDER_ITEM_MODEL (item_b)) {
    CamelFolderInfo *folder_a = stamp_folder_item_model_get_folder_info (STAMP_FOLDER_ITEM_MODEL (item_a));
    CamelFolderInfo *folder_b = stamp_folder_item_model_get_folder_info (STAMP_FOLDER_ITEM_MODEL (item_b));

    gint type_a = folder_a->flags & CAMEL_FOLDER_TYPE_MASK;
    gint type_b = folder_b->flags & CAMEL_FOLDER_TYPE_MASK;

    if (type_a == type_b) {
      const char *name_a = folder_a->display_name;
      const char *name_b = folder_b->display_name;

      return g_strcmp0 (name_a, name_b);
    }

    if (type_a == CAMEL_FOLDER_TYPE_NORMAL)
      return 1;

    if (type_b == CAMEL_FOLDER_TYPE_NORMAL)
      return -1;

    if (type_a < type_b)
      return -1;

    return 1;
  } else if (STAMP_IS_ACCOUNT_ITEM_MODEL (item_a) && STAMP_IS_ACCOUNT_ITEM_MODEL (item_b)) {
    const char *name_a = stamp_item_model_get_name (item_a);
    const char *name_b = stamp_item_model_get_name (item_b);

    return g_strcmp0 (name_a, name_b);
  } else {
    g_print ("WHAAT?\n");
  }

  return 0;
}

  static void
setup_folder_cb (GtkListItemFactory *factory,
                 GtkListItem        *list_item,
                 gpointer            user_data)
{
  GtkWidget *row;
  GtkWidget *expander;

  expander = gtk_tree_expander_new ();
  row = stamp_mail_folder_row_new ();
  gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), row);
  gtk_list_item_set_child (list_item, expander);
}

static void
stamp_mail_folder_list_class_init (StampMailFolderListClass *klass)
{
  signals[FOLDER_SELECTED] = g_signal_new ("folder-selected", G_OBJECT_CLASS_TYPE (klass),
                                 G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                 0, NULL, NULL, NULL,
                                 G_TYPE_NONE,
                                 1, G_TYPE_HASH_TABLE);
}

static void
stamp_mail_folder_list_init (StampMailFolderList *self)
{
  StampSession *session = NULL;
  GtkTreeListModel *tree_model;
  GtkListItemFactory *factory;
  GtkSorter *sorter;
  GtkTreeListRowSorter *tree_sorter;
  GtkSortListModel *sort_model;

  session = stamp_session_get_default ();

  self->settings = g_settings_new ("org.tabos.stamp.mail");

  self->root_model = g_list_store_new (STAMP_TYPE_ITEM_MODEL);
  tree_model = gtk_tree_list_model_new (G_LIST_MODEL (self->root_model), FALSE, FALSE, get_child_model, self, NULL);

  sorter = GTK_SORTER (gtk_custom_sorter_new (folders_compare, NULL, NULL));
  tree_sorter = gtk_tree_list_row_sorter_new (sorter);
  sort_model = gtk_sort_list_model_new (G_LIST_MODEL (tree_model), GTK_SORTER (tree_sorter));
  self->selection = gtk_single_selection_new (G_LIST_MODEL (sort_model));

  /* gtk_list_view_set_model (GTK_LIST_VIEW (self->folders_list), GTK_SELECTION_MODEL (selection)); */


  g_signal_connect (self->selection, "selection-changed", G_CALLBACK (on_selection_changed), self);

  factory = gtk_signal_list_item_factory_new ();
  g_signal_connect (factory, "setup", G_CALLBACK (setup_folder_cb), self);
  g_signal_connect (factory, "bind", G_CALLBACK (bind_folder_cb), self);
  /* gtk_list_view_set_factory (GTK_LIST_VIEW (self->folders_list), factory); */

  self->folders_list = gtk_list_view_new (GTK_SELECTION_MODEL (self->selection), factory);
  gtk_widget_add_css_class (self->folders_list , "navigation-sidebar");

  adw_bin_set_child (ADW_BIN (self), self->folders_list);

  g_signal_connect (session, "account-added", G_CALLBACK (on_account_added), self);
  /* g_signal_connect (session, "account-removed", G_CALLBACK (on_account_removed), self); */

  stamp_session_start (session, NULL, on_session_started, self);
}

GtkWidget *
stamp_mail_folder_list_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_FOLDER_LIST, NULL);
}
