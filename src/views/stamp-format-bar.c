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
 * The row of rich-text buttons that sits under an editable web view.
 * Two editors want it -- the composer and the signature editor in
 * preferences -- so the buttons, the "format" action group and the
 * editing commands they run all live here rather than in either one.
 *
 * The bar wraps an Adw.WrapBox -- which is final, so it cannot simply
 * be subclassed -- and builds its own buttons into it at init. Children
 * a caller declares in its own template are forwarded into the same
 * wrap box and so land after ours, which is how the composer keeps its
 * signature, attachment and security buttons on the one wrapping row.
 */

#include "stamp-format-bar.h"

#include <glib/gi18n.h>

#define BUTTON_SPACING (6)

struct _StampFormatBar {
  GtkWidget parent_instance;

  GtkWidget *wrap_box;
  StampWebView *webview;
  WebKitEditorState *editor_state;
  GtkEventController *shortcuts;
  gulong typing_handler;
  GSimpleActionGroup *actions;
  GCancellable *cancellable;
};

static void stamp_format_bar_buildable_init (GtkBuildableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (StampFormatBar, stamp_format_bar, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, stamp_format_bar_buildable_init))

static GtkBuildableIface *parent_buildable_iface;

typedef enum {
  FORMAT_ITEM_TOGGLE,
  FORMAT_ITEM_BUTTON,
  FORMAT_ITEM_SEPARATOR,
} FormatItemKind;

typedef struct {
  FormatItemKind kind;
  const gchar *action;
  const gchar *command;
  const gchar *icon;
  const gchar *label;
} FormatItem;

static const FormatItem format_items[] = {
  { FORMAT_ITEM_TOGGLE, "bold", "bold", "format-text-bold-symbolic", N_("Bold") },
  { FORMAT_ITEM_TOGGLE, "italic", "italic", "format-text-italic-symbolic", N_("Italic") },
  { FORMAT_ITEM_TOGGLE, "underline", "underline", "format-text-underline-symbolic", N_("Underline") },
  { FORMAT_ITEM_TOGGLE, "strikethrough", "strikethrough", "format-text-strikethrough-symbolic", N_("Strike Through") },
  { FORMAT_ITEM_SEPARATOR, NULL, NULL, NULL, NULL },
  { FORMAT_ITEM_TOGGLE, "bullet-list", "insertUnorderedList", "view-list-bullet-symbolic", N_("Bullet List") },
  { FORMAT_ITEM_TOGGLE, "numbered-list", "insertOrderedList", "view-list-ordered-symbolic", N_("Numbered List") },
  { FORMAT_ITEM_BUTTON, "insert-link", NULL, "insert-link-symbolic", N_("Insert Link") },
  { FORMAT_ITEM_SEPARATOR, NULL, NULL, NULL, NULL },
  { FORMAT_ITEM_BUTTON, "remove-format", NULL, "edit-clear-symbolic", N_("Remove Format") },
};

static void
on_query_command (GObject      *source,
                  const gchar  *command,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  StampFormatBar *self = user_data;
  g_autoptr (GError) error = NULL;
  gboolean ret;

  ret = stamp_web_view_query_command_state_finish (source, res, &error);
  if (error) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Could not query command state: %s", error->message);
    return;
  }

  g_action_group_change_action_state (G_ACTION_GROUP (self->actions), command, g_variant_new_string (ret ? command : ""));
}

static void
on_query_bullet_list_command (GObject      *source,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  on_query_command (source, "insertUnorderedList", res, user_data);
}

static void
on_query_numbered_list_command (GObject      *source,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  on_query_command (source, "insertOrderedList", res, user_data);
}

/*
 * A toggle lights up when its action state matches its target, and the
 * target is the editing command, so "on" is the command name and "off"
 * is the empty string.
 */
static void
set_toggle_state (StampFormatBar *self,
                  const gchar    *action,
                  const gchar    *command,
                  gboolean        active)
{
  g_action_group_change_action_state (G_ACTION_GROUP (self->actions), action,
                                      g_variant_new_string (active ? command : ""));
}

void
stamp_format_bar_update_actions (StampFormatBar *self)
{
  WebKitEditorState *state;
  guint attributes;

  g_return_if_fail (STAMP_IS_FORMAT_BAR (self));

  if (!self->webview)
    return;

  /* WebKit hands us the character formatting at the caret directly, so
   * these four need no round trip through the web process. */
  state = webkit_web_view_get_editor_state (WEBKIT_WEB_VIEW (self->webview));
  attributes = state ? webkit_editor_state_get_typing_attributes (state) : WEBKIT_EDITOR_TYPING_ATTRIBUTE_NONE;

  set_toggle_state (self, "bold", "bold", attributes & WEBKIT_EDITOR_TYPING_ATTRIBUTE_BOLD);
  set_toggle_state (self, "italic", "italic", attributes & WEBKIT_EDITOR_TYPING_ATTRIBUTE_ITALIC);
  set_toggle_state (self, "underline", "underline", attributes & WEBKIT_EDITOR_TYPING_ATTRIBUTE_UNDERLINE);
  set_toggle_state (self, "strikethrough", "strikethrough", attributes & WEBKIT_EDITOR_TYPING_ATTRIBUTE_STRIKETHROUGH);

  /* Being inside a list is not a typing attribute, so it still has to
   * be asked for. */
  stamp_web_view_query_command_state (self->webview, "insertUnorderedList", self->cancellable, on_query_bullet_list_command, self);
  stamp_web_view_query_command_state (self->webview, "insertOrderedList", self->cancellable, on_query_numbered_list_command, self);
}

/*
 * Fires whenever the caret lands somewhere with different formatting,
 * which is what keeps the buttons honest as the user clicks around.
 */
static void
on_typing_attributes_changed (GObject    *editor_state,
                              GParamSpec *pspec,
                              gpointer    user_data)
{
  stamp_format_bar_update_actions (STAMP_FORMAT_BAR (user_data));
}

static void
on_edit_activate (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
  StampFormatBar *self = STAMP_FORMAT_BAR (user_data);
  const gchar *command = g_variant_get_string (parameter, NULL);

  if (!self->webview)
    return;

  webkit_web_view_execute_editing_command (WEBKIT_WEB_VIEW (self->webview), command);
  stamp_format_bar_update_actions (self);
}

static void
on_remove_format_activated (GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       user_data)
{
  StampFormatBar *self = STAMP_FORMAT_BAR (user_data);

  if (!self->webview)
    return;

  stamp_web_view_execute_editor_command (self->webview, "removeformat", "");
  stamp_web_view_execute_editor_command (self->webview, "unlink", "");
}

/*
 * Browsers throw away ASCII whitespace and control characters before
 * they resolve a URL, so "java\nscript:alert(1)" runs as script. Strip
 * the same characters before looking at the scheme, or the check below
 * reads a scheme the browser never sees.
 */
static gboolean
url_is_safe (const gchar *url)
{
  g_autoptr (GString) stripped = g_string_new (NULL);
  g_autofree char *scheme = NULL;

  for (const gchar *p = url; *p; p++) {
    guchar c = (guchar) *p;

    if (!g_ascii_isspace (c) && c > 0x1f && c != 0x7f)
      g_string_append_c (stripped, *p);
  }

  scheme = g_uri_parse_scheme (stripped->str);
  if (!scheme)
    return TRUE;

  return g_ascii_strcasecmp (scheme, "javascript") != 0 &&
         g_ascii_strcasecmp (scheme, "data") != 0 &&
         g_ascii_strcasecmp (scheme, "vbscript") != 0;
}

static void
on_insert_link_response (AdwAlertDialog *dialog,
                         const gchar    *response,
                         gpointer        user_data)
{
  StampFormatBar *self = STAMP_FORMAT_BAR (user_data);
  GtkWidget *extra_child;
  GtkWidget *text_entry;
  GtkWidget *url_entry;
  const gchar *text;
  const gchar *url;

  if (g_strcmp0 (response, "insert") != 0 || !self->webview)
    return;

  extra_child = adw_alert_dialog_get_extra_child (dialog);
  text_entry = g_object_get_data (G_OBJECT (extra_child), "text-entry");
  url_entry = g_object_get_data (G_OBJECT (extra_child), "url-entry");
  text = gtk_editable_get_text (GTK_EDITABLE (text_entry));
  url = gtk_editable_get_text (GTK_EDITABLE (url_entry));

  if (!url || !*url)
    return;

  if (!url_is_safe (url)) {
    AdwDialog *refused = adw_alert_dialog_new (_("Link Not Inserted"),
                                               _("That address uses a scheme which is not allowed in a link."));

    adw_alert_dialog_add_response (ADW_ALERT_DIALOG (refused), "close", _("_Close"));
    adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (refused), "close");
    adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (refused), "close");
    adw_dialog_present (refused, GTK_WIDGET (self));
    return;
  }

  if (text && *text) {
    /* Both halves land inside markup we build by hand, so escape them:
     * a quote in either field would otherwise close the attribute. */
    g_autofree char *escaped_url = g_markup_escape_text (url, -1);
    g_autofree char *escaped_text = g_markup_escape_text (text, -1);
    g_autofree char *html = g_strdup_printf ("<a href=\"%s\">%s</a>", escaped_url, escaped_text);

    stamp_web_view_execute_editor_command (self->webview, "insertHTML", html);
  } else {
    stamp_web_view_execute_editor_command (self->webview, "createLink", url);
  }
}

static void
on_insert_link_activated (GSimpleAction *action,
                          GVariant      *parameter,
                          gpointer       user_data)
{
  StampFormatBar *self = STAMP_FORMAT_BAR (user_data);
  GtkWidget *dialog;
  GtkWidget *box;
  GtkWidget *text_entry;
  GtkWidget *url_entry;

  dialog = GTK_WIDGET (adw_alert_dialog_new (_("Insert Link"), NULL));

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

  text_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (text_entry), _("Text"));
  gtk_box_append (GTK_BOX (box), text_entry);

  url_entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (url_entry), _("URL"));
  gtk_box_append (GTK_BOX (box), url_entry);

  g_object_set_data (G_OBJECT (box), "text-entry", text_entry);
  g_object_set_data (G_OBJECT (box), "url-entry", url_entry);

  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), box);
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "cancel", _("_Cancel"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "insert", _("_Insert"));
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "insert", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "insert");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  g_signal_connect (dialog, "response", G_CALLBACK (on_insert_link_response), self);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

void
stamp_format_bar_undo (StampFormatBar *self)
{
  g_return_if_fail (STAMP_IS_FORMAT_BAR (self));

  if (self->webview)
    webkit_web_view_execute_editing_command (WEBKIT_WEB_VIEW (self->webview), WEBKIT_EDITING_COMMAND_UNDO);
}

void
stamp_format_bar_redo (StampFormatBar *self)
{
  g_return_if_fail (STAMP_IS_FORMAT_BAR (self));

  if (self->webview)
    webkit_web_view_execute_editing_command (WEBKIT_WEB_VIEW (self->webview), WEBKIT_EDITING_COMMAND_REDO);
}

static void
on_undo_activated (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
  stamp_format_bar_undo (STAMP_FORMAT_BAR (user_data));
}

static void
on_redo_activated (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
  stamp_format_bar_redo (STAMP_FORMAT_BAR (user_data));
}

static const GActionEntry stamp_format_bar_action_entries[] = {
  { "bold", on_edit_activate, "s", "''", NULL },
  { "italic", on_edit_activate, "s", "''", NULL },
  { "underline", on_edit_activate, "s", "''", NULL },
  { "strikethrough", on_edit_activate, "s", "''", NULL },
  { "bullet-list", on_edit_activate, "s", "''", NULL },
  { "numbered-list", on_edit_activate, "s", "''", NULL },
  { "insert-link", on_insert_link_activated },
  { "remove-format", on_remove_format_activated },
  { "undo", on_undo_activated },
  { "redo", on_redo_activated },
};

static void
add_shortcut (StampFormatBar *self,
              GtkShortcutController *controller,
              const gchar    *trigger,
              GtkShortcutFunc callback)
{
  GtkShortcut *shortcut = gtk_shortcut_new (gtk_shortcut_trigger_parse_string (trigger),
                                            gtk_callback_action_new (callback, self, NULL));

  gtk_shortcut_controller_add_shortcut (controller, shortcut);
}

/*
 * The shortcuts hang off the web view rather than the window: keys
 * typed into it never reached a capture-phase controller on the
 * window at all, and the preferences dialog does not put them on the
 * path either. Capturing on the view itself runs before WebKit's own
 * bubble-phase handling, which is what would otherwise swallow them.
 * The view only sees these keys when it has the focus, so no extra
 * focus check is needed -- and nothing outside the editor is touched.
 */
static gboolean
on_undo_shortcut (GtkWidget *widget,
                  GVariant  *args,
                  gpointer   user_data)
{
  stamp_format_bar_undo (STAMP_FORMAT_BAR (user_data));

  return TRUE;
}

static gboolean
on_redo_shortcut (GtkWidget *widget,
                  GVariant  *args,
                  gpointer   user_data)
{
  stamp_format_bar_redo (STAMP_FORMAT_BAR (user_data));

  return TRUE;
}

static void
install_shortcuts (StampFormatBar *self,
                   StampWebView   *webview)
{
  GtkEventController *controller = gtk_shortcut_controller_new ();

  gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);

  add_shortcut (self, GTK_SHORTCUT_CONTROLLER (controller), "<primary>z", on_undo_shortcut);
  add_shortcut (self, GTK_SHORTCUT_CONTROLLER (controller), "<primary>y", on_redo_shortcut);
  add_shortcut (self, GTK_SHORTCUT_CONTROLLER (controller), "<primary><shift>z", on_redo_shortcut);

  self->shortcuts = g_object_ref (controller);
  gtk_widget_add_controller (GTK_WIDGET (webview), controller);
}

static void
build_buttons (StampFormatBar *self)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (format_items); i++) {
    const FormatItem *item = &format_items[i];
    GtkWidget *child;

    if (item->kind == FORMAT_ITEM_SEPARATOR) {
      child = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
      gtk_widget_add_css_class (child, "flat");
      adw_wrap_box_append (ADW_WRAP_BOX (self->wrap_box), child);
      continue;
    }

    if (item->kind == FORMAT_ITEM_TOGGLE) {
      g_autofree char *action_name = g_strconcat ("format.", item->action, NULL);

      child = gtk_toggle_button_new ();
      gtk_actionable_set_action_name (GTK_ACTIONABLE (child), action_name);
      gtk_actionable_set_action_target_value (GTK_ACTIONABLE (child), g_variant_new_string (item->command));
    } else {
      g_autofree char *action_name = g_strconcat ("format.", item->action, NULL);

      child = gtk_button_new ();
      gtk_actionable_set_action_name (GTK_ACTIONABLE (child), action_name);
    }

    gtk_button_set_icon_name (GTK_BUTTON (child), item->icon);
    gtk_widget_set_tooltip_text (child, _(item->label));
    gtk_widget_add_css_class (child, "flat");
    gtk_accessible_update_property (GTK_ACCESSIBLE (child),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, _(item->label),
                                    -1);

    adw_wrap_box_append (ADW_WRAP_BOX (self->wrap_box), child);
  }
}

/*
 * A caller's own buttons belong on the same wrapping row as ours, not
 * stacked beside the wrap box, so send them the same way.
 */
static void
stamp_format_bar_add_child (GtkBuildable *buildable,
                            GtkBuilder   *builder,
                            GObject      *child,
                            const gchar  *type)
{
  StampFormatBar *self = STAMP_FORMAT_BAR (buildable);

  if (self->wrap_box && GTK_IS_WIDGET (child))
    adw_wrap_box_append (ADW_WRAP_BOX (self->wrap_box), GTK_WIDGET (child));
  else
    parent_buildable_iface->add_child (buildable, builder, child, type);
}

static void
stamp_format_bar_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);

  iface->add_child = stamp_format_bar_add_child;
}

static void
stamp_format_bar_dispose (GObject *object)
{
  StampFormatBar *self = STAMP_FORMAT_BAR (object);

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  if (self->editor_state)
    g_clear_signal_handler (&self->typing_handler, self->editor_state);

  g_clear_object (&self->cancellable);
  g_clear_object (&self->actions);
  g_clear_object (&self->shortcuts);
  g_clear_weak_pointer (&self->editor_state);
  g_clear_weak_pointer (&self->webview);
  g_clear_pointer (&self->wrap_box, gtk_widget_unparent);

  G_OBJECT_CLASS (stamp_format_bar_parent_class)->dispose (object);
}

static void
stamp_format_bar_class_init (StampFormatBarClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->dispose = stamp_format_bar_dispose;

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
stamp_format_bar_init (StampFormatBar *self)
{
  self->cancellable = g_cancellable_new ();

  self->wrap_box = adw_wrap_box_new ();
  adw_wrap_box_set_child_spacing (ADW_WRAP_BOX (self->wrap_box), BUTTON_SPACING);
  adw_wrap_box_set_line_spacing (ADW_WRAP_BOX (self->wrap_box), BUTTON_SPACING);
  gtk_widget_set_parent (self->wrap_box, GTK_WIDGET (self));

  self->actions = g_simple_action_group_new ();

  g_action_map_add_action_entries (G_ACTION_MAP (self->actions),
                                   stamp_format_bar_action_entries,
                                   G_N_ELEMENTS (stamp_format_bar_action_entries),
                                   self);

  gtk_widget_insert_action_group (GTK_WIDGET (self), "format", G_ACTION_GROUP (self->actions));

  build_buttons (self);
}

GtkWidget *
stamp_format_bar_new (void)
{
  return g_object_new (STAMP_TYPE_FORMAT_BAR, NULL);
}

void
stamp_format_bar_set_webview (StampFormatBar *self,
                              StampWebView   *webview)
{
  WebKitEditorState *state;

  g_return_if_fail (STAMP_IS_FORMAT_BAR (self));

  if (self->editor_state)
    g_clear_signal_handler (&self->typing_handler, self->editor_state);

  if (self->shortcuts && self->webview)
    gtk_widget_remove_controller (GTK_WIDGET (self->webview), self->shortcuts);

  self->typing_handler = 0;
  g_clear_object (&self->shortcuts);
  g_clear_weak_pointer (&self->editor_state);
  g_set_weak_pointer (&self->webview, webview);

  if (!webview)
    return;

  install_shortcuts (self, webview);

  state = webkit_web_view_get_editor_state (WEBKIT_WEB_VIEW (webview));
  if (!state)
    return;

  g_set_weak_pointer (&self->editor_state, state);
  self->typing_handler = g_signal_connect_object (state, "notify::typing-attributes",
                                                  G_CALLBACK (on_typing_attributes_changed),
                                                  self, G_CONNECT_DEFAULT);
}

StampWebView *
stamp_format_bar_get_webview (StampFormatBar *self)
{
  g_return_val_if_fail (STAMP_IS_FORMAT_BAR (self), NULL);

  return self->webview;
}
