/*
 * Copyright 2026 steeb-k
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * An icon in the desktop's tray, for getting the window back once it
 * has been closed and Post carries on in the background.
 *
 * GTK 4 has nothing for this and the AppIndicator libraries are GTK 3,
 * so this speaks the two D-Bus interfaces a tray is made of itself:
 * org.kde.StatusNotifierItem for the icon, and com.canonical.dbusmenu
 * for the menu behind it, which the desktop draws rather than us.
 *
 * The item lives on a connection of its own. The protocol has no way
 * to take an item back, a tray only drops one when the connection it
 * came from goes away, and the application's own connection is not
 * going anywhere while Post runs.
 *
 * The icon is handed over as pixels rather than as a name to look up.
 * A name is less to send, but it leaves two things to the tray that do
 * not always happen: finding the file, which a tray that was running
 * before Post was installed may not manage until it is restarted, and
 * recolouring a symbolic icon to suit the panel, without which the dark
 * drawing is lost on a dark one. The name is only the fallback for when
 * nobody has given us the pixels.
 */

#define G_LOG_DOMAIN "stamp-tray"

#include "stamp-tray.h"

#include <glib/gi18n.h>

#define WATCHER_NAME   "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH   "/StatusNotifierWatcher"
#define ITEM_INTERFACE "org.kde.StatusNotifierItem"
#define ITEM_PATH      "/StatusNotifierItem"
#define MENU_INTERFACE "com.canonical.dbusmenu"
#define MENU_PATH      "/MenuBar"

/* The ids the menu's entries go by; the root is 0 by definition. */
enum {
  MENU_ROOT,
  MENU_TOGGLE,
  MENU_QUIT,
  N_MENU_ITEMS
};

static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='" ITEM_INTERFACE "'>"
  "    <property name='Category' type='s' access='read'/>"
  "    <property name='Id' type='s' access='read'/>"
  "    <property name='Title' type='s' access='read'/>"
  "    <property name='Status' type='s' access='read'/>"
  "    <property name='IconName' type='s' access='read'/>"
  "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
  "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
  "    <property name='ItemIsMenu' type='b' access='read'/>"
  "    <property name='Menu' type='o' access='read'/>"
  "    <method name='ProvideXdgActivationToken'>"
  "      <arg name='token' type='s' direction='in'/>"
  "    </method>"
  "    <method name='Activate'>"
  "      <arg name='x' type='i' direction='in'/>"
  "      <arg name='y' type='i' direction='in'/>"
  "    </method>"
  "    <method name='SecondaryActivate'>"
  "      <arg name='x' type='i' direction='in'/>"
  "      <arg name='y' type='i' direction='in'/>"
  "    </method>"
  "    <method name='ContextMenu'>"
  "      <arg name='x' type='i' direction='in'/>"
  "      <arg name='y' type='i' direction='in'/>"
  "    </method>"
  "    <method name='Scroll'>"
  "      <arg name='delta' type='i' direction='in'/>"
  "      <arg name='orientation' type='s' direction='in'/>"
  "    </method>"
  "    <signal name='NewTitle'/>"
  "    <signal name='NewIcon'/>"
  "    <signal name='NewToolTip'/>"
  "    <signal name='NewStatus'>"
  "      <arg name='status' type='s'/>"
  "    </signal>"
  "  </interface>"
  "  <interface name='" MENU_INTERFACE "'>"
  "    <property name='Version' type='u' access='read'/>"
  "    <property name='TextDirection' type='s' access='read'/>"
  "    <property name='Status' type='s' access='read'/>"
  "    <property name='IconThemePath' type='as' access='read'/>"
  "    <method name='GetLayout'>"
  "      <arg name='parentId' type='i' direction='in'/>"
  "      <arg name='recursionDepth' type='i' direction='in'/>"
  "      <arg name='propertyNames' type='as' direction='in'/>"
  "      <arg name='revision' type='u' direction='out'/>"
  "      <arg name='layout' type='(ia{sv}av)' direction='out'/>"
  "    </method>"
  "    <method name='GetGroupProperties'>"
  "      <arg name='ids' type='ai' direction='in'/>"
  "      <arg name='propertyNames' type='as' direction='in'/>"
  "      <arg name='properties' type='a(ia{sv})' direction='out'/>"
  "    </method>"
  "    <method name='GetProperty'>"
  "      <arg name='id' type='i' direction='in'/>"
  "      <arg name='name' type='s' direction='in'/>"
  "      <arg name='value' type='v' direction='out'/>"
  "    </method>"
  "    <method name='Event'>"
  "      <arg name='id' type='i' direction='in'/>"
  "      <arg name='eventId' type='s' direction='in'/>"
  "      <arg name='data' type='v' direction='in'/>"
  "      <arg name='timestamp' type='u' direction='in'/>"
  "    </method>"
  "    <method name='EventGroup'>"
  "      <arg name='events' type='a(isvu)' direction='in'/>"
  "      <arg name='idErrors' type='ai' direction='out'/>"
  "    </method>"
  "    <method name='AboutToShow'>"
  "      <arg name='id' type='i' direction='in'/>"
  "      <arg name='needUpdate' type='b' direction='out'/>"
  "    </method>"
  "    <method name='AboutToShowGroup'>"
  "      <arg name='ids' type='ai' direction='in'/>"
  "      <arg name='updatesNeeded' type='ai' direction='out'/>"
  "      <arg name='idErrors' type='ai' direction='out'/>"
  "    </method>"
  "    <signal name='ItemsPropertiesUpdated'>"
  "      <arg name='updatedProps' type='a(ia{sv})'/>"
  "      <arg name='removedProps' type='a(ias)'/>"
  "    </signal>"
  "    <signal name='LayoutUpdated'>"
  "      <arg name='revision' type='u'/>"
  "      <arg name='parent' type='i'/>"
  "    </signal>"
  "    <signal name='ItemActivationRequested'>"
  "      <arg name='id' type='i'/>"
  "      <arg name='timestamp' type='u'/>"
  "    </signal>"
  "  </interface>"
  "</node>";

struct _StampTray {
  GObject parent_instance;

  gchar *application_id;
  gboolean enabled;
  gboolean window_shown;
  GVariant *pixmaps;

  GCancellable *cancellable;
  GDBusConnection *connection;
  guint item_id;
  guint menu_id;
  guint watcher_id;

  guint menu_revision;
  gchar *activation_token;
};

enum {
  PROP_0,
  PROP_ENABLED,
  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

enum {
  ACTIVATE,
  QUIT,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (StampTray, stamp_tray, G_TYPE_OBJECT);

static void
item_method_call (GDBusConnection       *connection,
                  const gchar           *sender,
                  const gchar           *object_path,
                  const gchar           *interface_name,
                  const gchar           *method_name,
                  GVariant              *parameters,
                  GDBusMethodInvocation *invocation,
                  gpointer               user_data)
{
  StampTray *self = STAMP_TRAY (user_data);
  gboolean activate = g_strcmp0 (method_name, "Activate") == 0;

  if (g_strcmp0 (method_name, "ProvideXdgActivationToken") == 0) {
    /* Handed over just ahead of the click it belongs to */
    g_clear_pointer (&self->activation_token, g_free);
    g_variant_get (parameters, "(s)", &self->activation_token);
  }

  /* A middle click and the wheel mean nothing here, and ContextMenu is
   * for an item that draws its own menu, where ours is the desktop's.
   * They are answered all the same, and ahead of "activate", as what
   * that sets off may take its time. */
  g_dbus_method_invocation_return_value (invocation, NULL);

  if (activate) {
    g_autofree gchar *token = g_steal_pointer (&self->activation_token);

    g_signal_emit (self, signals[ACTIVATE], 0, token);
  }
}

static GVariant *
item_get_property (GDBusConnection  *connection,
                   const gchar      *sender,
                   const gchar      *object_path,
                   const gchar      *interface_name,
                   const gchar      *property_name,
                   GError          **error,
                   gpointer          user_data)
{
  StampTray *self = STAMP_TRAY (user_data);

  if (g_strcmp0 (property_name, "Category") == 0)
    return g_variant_new_string ("Communications");

  if (g_strcmp0 (property_name, "Id") == 0)
    return g_variant_new_string (self->application_id);

  if (g_strcmp0 (property_name, "Title") == 0)
    return g_variant_new_string (g_get_application_name ());

  if (g_strcmp0 (property_name, "Status") == 0)
    return g_variant_new_string ("Active");

  /* A tray takes the name over the pixels if it is given both */
  if (g_strcmp0 (property_name, "IconName") == 0) {
    if (self->pixmaps)
      return g_variant_new_string ("");

    return g_variant_new_take_string (g_strconcat (self->application_id, "-symbolic", NULL));
  }

  if (g_strcmp0 (property_name, "IconPixmap") == 0) {
    if (self->pixmaps)
      return g_variant_ref (self->pixmaps);

    return g_variant_new_array (G_VARIANT_TYPE ("(iiay)"), NULL, 0);
  }

  if (g_strcmp0 (property_name, "ToolTip") == 0) {
    GVariantBuilder pixmaps;

    g_variant_builder_init (&pixmaps, G_VARIANT_TYPE ("a(iiay)"));

    return g_variant_new ("(sa(iiay)ss)", "", &pixmaps, g_get_application_name (), "");
  }

  if (g_strcmp0 (property_name, "ItemIsMenu") == 0)
    return g_variant_new_boolean (FALSE);

  if (g_strcmp0 (property_name, "Menu") == 0)
    return g_variant_new_object_path (MENU_PATH);

  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No property %s", property_name);

  return NULL;
}

static const GDBusInterfaceVTable item_vtable = {
  .method_call = item_method_call,
  .get_property = item_get_property,
};

/*
 * What one entry looks like. An empty @names asks for everything; the
 * properties left out are the ones at their defaults, which is all of
 * them but the label.
 */
static GVariant *
menu_item_properties (StampTray           *self,
                      gint                 id,
                      const gchar * const *names)
{
  GVariantBuilder builder;
  const gchar *label = NULL;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

  if (id == MENU_TOGGLE)
    label = self->window_shown ? _("Hide Post") : _("Show Post");
  else if (id == MENU_QUIT)
    label = _("Quit");
  else if (!names[0] || g_strv_contains (names, "children-display"))
    g_variant_builder_add (&builder, "{sv}", "children-display", g_variant_new_string ("submenu"));

  if (label && (!names[0] || g_strv_contains (names, "label")))
    g_variant_builder_add (&builder, "{sv}", "label", g_variant_new_string (label));

  return g_variant_builder_end (&builder);
}

static GVariant *
menu_layout (StampTray           *self,
             gint                 id,
             gboolean             with_children,
             const gchar * const *names)
{
  GVariantBuilder children;

  g_variant_builder_init (&children, G_VARIANT_TYPE ("av"));

  if (id == MENU_ROOT && with_children) {
    for (gint child = MENU_ROOT + 1; child < N_MENU_ITEMS; child++)
      g_variant_builder_add (&children, "v", menu_layout (self, child, FALSE, names));
  }

  return g_variant_new ("(i@a{sv}av)", id, menu_item_properties (self, id, names), &children);
}

static void
menu_event (StampTray   *self,
            gint         id,
            const gchar *event)
{
  if (g_strcmp0 (event, "clicked") != 0)
    return;

  if (id == MENU_TOGGLE)
    g_signal_emit (self, signals[ACTIVATE], 0, NULL);
  else if (id == MENU_QUIT)
    g_signal_emit (self, signals[QUIT], 0);
}

static void
menu_method_call (GDBusConnection       *connection,
                  const gchar           *sender,
                  const gchar           *object_path,
                  const gchar           *interface_name,
                  const gchar           *method_name,
                  GVariant              *parameters,
                  GDBusMethodInvocation *invocation,
                  gpointer               user_data)
{
  StampTray *self = STAMP_TRAY (user_data);

  if (g_strcmp0 (method_name, "GetLayout") == 0) {
    g_autofree const gchar **names = NULL;
    gint id;
    gint depth;

    g_variant_get (parameters, "(ii^a&s)", &id, &depth, &names);

    if (id < MENU_ROOT || id >= N_MENU_ITEMS) {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No menu item %d", id);
      return;
    }

    g_dbus_method_invocation_return_value (invocation,
                                           g_variant_new ("(u@(ia{sv}av))",
                                                          self->menu_revision,
                                                          menu_layout (self, id, depth != 0, names)));
  } else if (g_strcmp0 (method_name, "GetGroupProperties") == 0) {
    g_autoptr (GVariantIter) ids = NULL;
    g_autofree const gchar **names = NULL;
    GVariantBuilder builder;
    gint id;

    g_variant_get (parameters, "(ai^a&s)", &ids, &names);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ia{sv})"));

    /* No ids at all asks for every entry there is */
    if (g_variant_iter_n_children (ids) == 0) {
      for (id = MENU_ROOT; id < N_MENU_ITEMS; id++)
        g_variant_builder_add (&builder, "(i@a{sv})", id, menu_item_properties (self, id, names));
    }

    while (g_variant_iter_next (ids, "i", &id)) {
      if (id >= MENU_ROOT && id < N_MENU_ITEMS)
        g_variant_builder_add (&builder, "(i@a{sv})", id, menu_item_properties (self, id, names));
    }

    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a(ia{sv}))", &builder));
  } else if (g_strcmp0 (method_name, "GetProperty") == 0) {
    const gchar *names[] = { NULL, NULL };
    g_autoptr (GVariant) item_properties = NULL;
    g_autoptr (GVariant) value = NULL;
    gint id;

    g_variant_get (parameters, "(i&s)", &id, &names[0]);

    if (id >= MENU_ROOT && id < N_MENU_ITEMS) {
      item_properties = g_variant_ref_sink (menu_item_properties (self, id, names));
      value = g_variant_lookup_value (item_properties, names[0], NULL);
    }

    if (!value) {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No property %s on menu item %d", names[0], id);
      return;
    }

    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(v)", value));
  } else if (g_strcmp0 (method_name, "Event") == 0) {
    g_autoptr (GVariant) event = g_variant_get_child_value (parameters, 1);
    gint id;

    g_variant_get_child (parameters, 0, "i", &id);
    g_dbus_method_invocation_return_value (invocation, NULL);

    menu_event (self, id, g_variant_get_string (event, NULL));
  } else if (g_strcmp0 (method_name, "EventGroup") == 0) {
    g_autoptr (GVariant) events = g_variant_get_child_value (parameters, 0);

    g_dbus_method_invocation_return_value (invocation, g_variant_new_parsed ("(@ai [],)"));

    for (gsize i = 0; i < g_variant_n_children (events); i++) {
      g_autoptr (GVariant) entry = g_variant_get_child_value (events, i);
      g_autoptr (GVariant) event = g_variant_get_child_value (entry, 1);
      gint id;

      g_variant_get_child (entry, 0, "i", &id);
      menu_event (self, id, g_variant_get_string (event, NULL));
    }
  } else if (g_strcmp0 (method_name, "AboutToShow") == 0) {
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", FALSE));
  } else if (g_strcmp0 (method_name, "AboutToShowGroup") == 0) {
    g_dbus_method_invocation_return_value (invocation, g_variant_new_parsed ("(@ai [], @ai [])"));
  }
}

static GVariant *
menu_get_property (GDBusConnection  *connection,
                   const gchar      *sender,
                   const gchar      *object_path,
                   const gchar      *interface_name,
                   const gchar      *property_name,
                   GError          **error,
                   gpointer          user_data)
{
  if (g_strcmp0 (property_name, "Version") == 0)
    return g_variant_new_uint32 (3);

  if (g_strcmp0 (property_name, "TextDirection") == 0) {
    /* The string GTK looks up in its own catalogue to tell which way
     * the language runs; asking it the same keeps GTK out of here. */
    const gchar *direction = g_dgettext ("gtk40", "default:LTR");

    return g_variant_new_string (g_strcmp0 (direction, "default:RTL") == 0 ? "rtl" : "ltr");
  }

  if (g_strcmp0 (property_name, "Status") == 0)
    return g_variant_new_string ("normal");

  if (g_strcmp0 (property_name, "IconThemePath") == 0)
    return g_variant_new_strv (NULL, 0);

  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No property %s", property_name);

  return NULL;
}

static const GDBusInterfaceVTable menu_vtable = {
  .method_call = menu_method_call,
  .get_property = menu_get_property,
};

static void
on_registered (GObject      *source,
               GAsyncResult *result,
               gpointer      user_data)
{
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

  if (!reply && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    g_warning ("%s: Could not register the tray icon: %s", G_STRFUNC, error->message);
}

/*
 * Runs each time a tray turns up, which is also how the icon comes
 * back after the desktop's shell has been restarted under us.
 */
static void
on_watcher_appeared (GDBusConnection *connection,
                     const gchar     *name,
                     const gchar     *name_owner,
                     gpointer         user_data)
{
  StampTray *self = STAMP_TRAY (user_data);

  /* A path rather than a name: the watcher pairs it with the connection
   * the call came from, which is all a sandboxed Post can be reached by. */
  g_dbus_connection_call (connection,
                          WATCHER_NAME,
                          WATCHER_PATH,
                          WATCHER_NAME,
                          "RegisterStatusNotifierItem",
                          g_variant_new ("(s)", ITEM_PATH),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          self->cancellable,
                          on_registered,
                          NULL);
}

static void
on_connected (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
  g_autoptr (StampTray) self = STAMP_TRAY (user_data);
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GDBusNodeInfo) node = NULL;
  g_autoptr (GError) error = NULL;

  connection = g_dbus_connection_new_for_address_finish (result, &error);

  if (!connection) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("%s: Could not connect to the session bus: %s", G_STRFUNC, error->message);
    return;
  }

  node = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
  g_assert (node != NULL);

  self->connection = g_steal_pointer (&connection);
  self->item_id = g_dbus_connection_register_object (self->connection,
                                                     ITEM_PATH,
                                                     g_dbus_node_info_lookup_interface (node, ITEM_INTERFACE),
                                                     &item_vtable,
                                                     self,
                                                     NULL,
                                                     NULL);
  self->menu_id = g_dbus_connection_register_object (self->connection,
                                                     MENU_PATH,
                                                     g_dbus_node_info_lookup_interface (node, MENU_INTERFACE),
                                                     &menu_vtable,
                                                     self,
                                                     NULL,
                                                     NULL);
  self->watcher_id = g_bus_watch_name_on_connection (self->connection,
                                                     WATCHER_NAME,
                                                     G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                     on_watcher_appeared,
                                                     NULL,
                                                     self,
                                                     NULL);
}

static void
stamp_tray_start (StampTray *self)
{
  g_autofree gchar *address = NULL;
  g_autoptr (GError) error = NULL;

  address = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION, NULL, &error);

  if (!address) {
    g_warning ("%s: Could not find the session bus: %s", G_STRFUNC, error->message);
    return;
  }

  self->cancellable = g_cancellable_new ();
  g_dbus_connection_new_for_address (address,
                                     G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
                                     NULL,
                                     self->cancellable,
                                     on_connected,
                                     g_object_ref (self));
}

static void
stamp_tray_stop (StampTray *self)
{
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_pointer (&self->activation_token, g_free);

  if (!self->connection)
    return;

  g_clear_handle_id (&self->watcher_id, g_bus_unwatch_name);
  g_dbus_connection_unregister_object (self->connection, self->item_id);
  g_dbus_connection_unregister_object (self->connection, self->menu_id);
  self->item_id = 0;
  self->menu_id = 0;

  /* Closing the connection is what takes the icon out of the tray */
  g_dbus_connection_close (self->connection, NULL, NULL, NULL);
  g_clear_object (&self->connection);
}

StampTray *
stamp_tray_new (const gchar *application_id)
{
  StampTray *self;

  g_return_val_if_fail (application_id != NULL, NULL);

  self = g_object_new (STAMP_TYPE_TRAY, NULL);
  self->application_id = g_strdup (application_id);

  return self;
}

void
stamp_tray_set_enabled (StampTray *self,
                        gboolean   enabled)
{
  g_return_if_fail (STAMP_IS_TRAY (self));

  enabled = !!enabled;

  if (self->enabled == enabled)
    return;

  self->enabled = enabled;

  if (enabled)
    stamp_tray_start (self);
  else
    stamp_tray_stop (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

void
stamp_tray_set_icon (StampTray *self,
                     GVariant  *pixmaps)
{
  g_return_if_fail (STAMP_IS_TRAY (self));
  g_return_if_fail (pixmaps == NULL || g_variant_is_of_type (pixmaps, G_VARIANT_TYPE ("a(iiay)")));

  g_clear_pointer (&self->pixmaps, g_variant_unref);

  if (pixmaps)
    self->pixmaps = g_variant_ref_sink (pixmaps);

  if (self->item_id)
    g_dbus_connection_emit_signal (self->connection,
                                   NULL,
                                   ITEM_PATH,
                                   ITEM_INTERFACE,
                                   "NewIcon",
                                   NULL,
                                   NULL);
}

void
stamp_tray_set_window_shown (StampTray *self,
                             gboolean   shown)
{
  g_return_if_fail (STAMP_IS_TRAY (self));

  shown = !!shown;

  if (self->window_shown == shown)
    return;

  self->window_shown = shown;
  self->menu_revision++;

  /* A changed label alone would be ItemsPropertiesUpdated, but a new
   * layout is the one signal every tray is sure to act on. */
  if (self->menu_id)
    g_dbus_connection_emit_signal (self->connection,
                                   NULL,
                                   MENU_PATH,
                                   MENU_INTERFACE,
                                   "LayoutUpdated",
                                   g_variant_new ("(ui)", self->menu_revision, MENU_ROOT),
                                   NULL);
}

static void
stamp_tray_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  StampTray *self = STAMP_TRAY (object);

  switch (prop_id) {
    case PROP_ENABLED:
      g_value_set_boolean (value, self->enabled);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
stamp_tray_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  StampTray *self = STAMP_TRAY (object);

  switch (prop_id) {
    case PROP_ENABLED:
      stamp_tray_set_enabled (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
stamp_tray_dispose (GObject *object)
{
  StampTray *self = STAMP_TRAY (object);

  stamp_tray_stop (self);

  G_OBJECT_CLASS (stamp_tray_parent_class)->dispose (object);
}

static void
stamp_tray_finalize (GObject *object)
{
  StampTray *self = STAMP_TRAY (object);

  g_free (self->application_id);
  g_clear_pointer (&self->pixmaps, g_variant_unref);

  G_OBJECT_CLASS (stamp_tray_parent_class)->finalize (object);
}

static void
stamp_tray_class_init (StampTrayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = stamp_tray_get_property;
  object_class->set_property = stamp_tray_set_property;
  object_class->dispose = stamp_tray_dispose;
  object_class->finalize = stamp_tray_finalize;

  properties[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[ACTIVATE] =
    g_signal_new ("activate",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_STRING);

  signals[QUIT] =
    g_signal_new ("quit",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
}

static void
stamp_tray_init (StampTray *self)
{
}
