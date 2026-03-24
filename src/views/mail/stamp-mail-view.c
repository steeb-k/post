/*
 * Copyright 2025-2026 Jan-Michael Brummer
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

#include "stamp-mail-view.h"

#include "stamp-conversation-list.h"
#include "stamp-folder-list.h"
#include "stamp-message-list.h"

#include <glib/gi18n.h>

struct _StampMailView {
  AdwBreakpointBin parent_instance;

  AdwMultiLayoutView *mail_layout;
  AdwOverlaySplitView *outer_osv;
  AdwOverlaySplitView *tablet_osv;
  AdwOverlaySplitView *mobile_osv;
  AdwNavigationView *mobile_nav;
  GtkWidget *details;
  GtkWidget *folder_list;
  GtkWidget *message_list;
  GtkWidget *conversation_list;
  GtkWidget *stack;

  GSimpleActionGroup *actions;
  GtkWidget *toast_overlay;
  StampAccount *account;

  GtkPaned *desktop_paned;
  GtkPaned *tablet_paned;

  int saved_paned_pos;
};

G_DEFINE_FINAL_TYPE (StampMailView, stamp_mail_view, ADW_TYPE_BREAKPOINT_BIN)

static AdwOverlaySplitView *
get_current_osv (StampMailView *self)
{
  const char *layout = adw_multi_layout_view_get_layout_name (self->mail_layout);

  if (g_strcmp0 (layout, "tablet") == 0)
    return self->tablet_osv;
  if (g_strcmp0 (layout, "mobile") == 0)
    return self->mobile_osv;
  return NULL;
}

/* static void */
/* on_outer_view_collapsed (AdwBreakpoint *breakpoint, */
/*                          StampMailView *self) */
/* { */
/*   if (adw_navigation_split_view_get_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->outer_view))) { */
/*     adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->inner_view), TRUE); */
/*   } */
/* } */

static void
on_details_hiddem (AdwNavigationPage *page,
                   gpointer           user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_conversation_list_unselect (STAMP_CONVERSATION_LIST (self->conversation_list));
}

/* static void */
/* on_mail_page_hidden (AdwNavigationPage *page, */
/*                      gpointer           user_data) */
/* { */
/*   StampMailView *self = STAMP_MAIL_VIEW (user_data); */

/*   stamp_folder_list_unselect (STAMP_FOLDER_LIST (self->folder_list)); */
/* } */

static void
on_sidebar_visibility_changed (StampMailView *self)
{
  AdwOverlaySplitView *osv = get_current_osv (self);
  GtkWidget *btn;
  gboolean shown;

  if (!osv)
    return;

  shown = adw_overlay_split_view_get_show_sidebar (osv);
  btn = stamp_conversation_list_get_sidebar_button (STAMP_CONVERSATION_LIST (self->conversation_list));
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (btn)) != shown)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), shown);
}

static void
close_overlay_sidebar (StampMailView *self)
{
  AdwOverlaySplitView *osv = get_current_osv (self);
  if (!osv || !adw_overlay_split_view_get_show_sidebar (osv)) return;
  adw_overlay_split_view_set_show_sidebar (osv, FALSE);
  gtk_toggle_button_set_active (
    GTK_TOGGLE_BUTTON (stamp_conversation_list_get_sidebar_button (STAMP_CONVERSATION_LIST (self->conversation_list))), FALSE);
}

static void
on_folder_selected (GtkWidget    *object,
                    StampAccount *account,
                    char         *full_name,
                    gpointer      user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  close_overlay_sidebar (self);
  self->account = account;
  stamp_conversation_list_load_folder (STAMP_CONVERSATION_LIST (self->conversation_list), account, full_name);
}

static void
on_conversation_selected (GtkWidget *object,
                          gpointer   thread_node,
                          gpointer   user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  if (thread_node) {
    const char *layout = adw_multi_layout_view_get_layout_name (self->mail_layout);

    if (g_strcmp0 (layout, "mobile") == 0 && self->mobile_nav)
      adw_navigation_view_push_by_tag (self->mobile_nav, "content");
  }
  /* adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->outer_view), TRUE); */
  stamp_message_list_set_conversation (STAMP_MESSAGE_LIST (self->message_list), self->account, (CamelFolderThreadNode *)thread_node);
}

static void
on_dismiss_button_clicked (AdwToast *toast,
                           gpointer  user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_conversation_list_undo_trash (STAMP_CONVERSATION_LIST (self->conversation_list));
}

static void
on_conversation_trash (GtkWidget             *object,
                       StampConversationItem *item,
                       gpointer               user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  AdwToast *toast;

  stamp_conversation_list_trash (STAMP_CONVERSATION_LIST (self->conversation_list), item);
  toast = adw_toast_new (_("Conversations moved to trash"));
  adw_toast_set_button_label (ADW_TOAST (toast), _("Undo"));
  g_signal_connect (toast, "button-clicked", G_CALLBACK (on_dismiss_button_clicked), self);
  adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (self->toast_overlay), toast);
}

static void
stamp_mail_view_dispose (GObject *object)
{
  StampMailView *self = STAMP_MAIL_VIEW (object);

  g_clear_object (&self->actions);

  /* FIXME: Does not work with AdwBreakpointBin */
  /* gtk_widget_dispose_template (GTK_WIDGET (self), STAMP_TYPE_MAIL_VIEW); */

  G_OBJECT_CLASS (stamp_mail_view_parent_class)->dispose (object);
}

static void
stamp_mail_view_get_property (GObject    *obj,
                              guint       id,
                              GValue     *val,
                              GParamSpec *ps)
{
  if (id == 1) g_value_set_object (val, STAMP_MAIL_VIEW (obj)->stack);
  else G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, ps);
}

static void
stamp_mail_view_set_property (GObject      *obj,
                              guint         id,
                              const GValue *val,
                              GParamSpec   *ps)
{
  if (id == 1) g_set_object (&STAMP_MAIL_VIEW (obj)->stack, g_value_get_object (val));
  else G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, ps);
}

void
stamp_mail_view_class_init (StampMailViewClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/tabos/stamp/views/mail/stamp-mail-view.ui");

  object_class->dispose = stamp_mail_view_dispose;
  object_class->set_property = stamp_mail_view_set_property;
  object_class->get_property = stamp_mail_view_get_property;

  g_object_class_install_property (object_class, 1,
                                   g_param_spec_object ("stack", NULL, NULL,
                                                        ADW_TYPE_VIEW_STACK,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gtk_widget_class_bind_template_child (widget_class, StampMailView, mail_layout);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, folder_list);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, message_list);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, conversation_list);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, desktop_paned);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, tablet_paned);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, outer_osv);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, tablet_osv);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, mobile_osv);
  gtk_widget_class_bind_template_child (widget_class, StampMailView, mobile_nav);
  gtk_widget_class_bind_template_callback (widget_class, on_details_hiddem);
  /* gtk_widget_class_bind_template_callback (widget_class, on_mail_page_hidden); */
  /* gtk_widget_class_bind_template_callback (widget_class, on_outer_view_collapsed); */
  gtk_widget_class_bind_template_callback (widget_class, on_folder_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_conversation_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_conversation_trash);
}

static void
on_mark_read (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  GAction *mark_read_action;
  GAction *mark_unread_action;

  stamp_conversation_list_mark_read (STAMP_CONVERSATION_LIST (self->conversation_list), NULL);

  mark_read_action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-read");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (mark_read_action), FALSE);
  mark_unread_action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-unread");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (mark_unread_action), TRUE);
}

static void
on_mark_unread (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  GAction *mark_read_action;
  GAction *mark_unread_action;

  stamp_conversation_list_mark_unread (STAMP_CONVERSATION_LIST (self->conversation_list), NULL);

  mark_read_action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-read");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (mark_read_action), TRUE);
  mark_unread_action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-unread");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (mark_unread_action), FALSE);
}

static void
on_mark_unflag (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  GAction *mark_flag_action;
  GAction *mark_unflag_action;

  stamp_conversation_list_mark_unflag_selected_messages (STAMP_CONVERSATION_LIST (self->conversation_list));

  mark_flag_action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-flag");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (mark_flag_action), TRUE);
  mark_unflag_action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-unflag");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (mark_unflag_action), FALSE);
}

static void
on_mark_flag (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  GAction *mark_flag_action;
  GAction *mark_unflag_action;

  stamp_conversation_list_mark_flag_selected_messages (STAMP_CONVERSATION_LIST (self->conversation_list));

  mark_flag_action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-flag");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (mark_flag_action), FALSE);
  mark_unflag_action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "mark-unflag");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (mark_unflag_action), TRUE);
}

static void
on_reply (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (STAMP_MESSAGE_LIST (self->message_list), STAMP_COMPOSER_REPLY, parameter);
}

static void
on_reply_all (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (STAMP_MESSAGE_LIST (self->message_list), STAMP_COMPOSER_REPLY_ALL, parameter);
}

static void
on_forward (GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (STAMP_MESSAGE_LIST (self->message_list), STAMP_COMPOSER_FORWARD, parameter);
}

static void
on_trash (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  AdwToast *toast;

  stamp_conversation_list_trash (STAMP_CONVERSATION_LIST (self->conversation_list), NULL);
  toast = adw_toast_new (_("Conversations moved to trash"));
  adw_toast_set_button_label (ADW_TOAST (toast), _("Undo"));
  g_signal_connect (toast, "button-clicked", G_CALLBACK (on_dismiss_button_clicked), self);
  adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (self->toast_overlay), toast);
}

static void
on_print (GSimpleAction *action,
          GVariant      *parameter,
          gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_print (STAMP_MESSAGE_LIST (self->message_list), parameter);
}

static void
on_view_source (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_view_source (STAMP_MESSAGE_LIST (self->message_list), parameter);
}

static void
on_composer_new (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (STAMP_MESSAGE_LIST (self->message_list), STAMP_COMPOSER_NEW, parameter);
}

static void
on_edit (GSimpleAction *action,
         GVariant      *parameter,
         gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);

  stamp_message_list_compose (STAMP_MESSAGE_LIST (self->message_list), STAMP_COMPOSER_DRAFT, parameter);
}

static const GActionEntry stamp_mail_view_action_entries[] = {
  { .name = "composer-new", .activate = on_composer_new },
  { .name = "edit", .activate = on_edit },
  { .name = "forward", .activate = on_forward, .parameter_type = "s" },
  { .name = "forward-current", .activate = on_forward },
  { .name = "mark-flag", .activate = on_mark_flag, .parameter_type = "s" },
  { .name = "mark-flag-current", .activate = on_mark_flag },
  { .name = "mark-read", .activate = on_mark_read, .parameter_type = "s" },
  { .name = "mark-read-current", .activate = on_mark_read },
  { .name = "mark-unflag", .activate = on_mark_unflag, .parameter_type = "s" },
  { .name = "mark-unflag-current", .activate = on_mark_unflag },
  { .name = "mark-unread", .activate = on_mark_unread, .parameter_type = "s" },
  { .name = "mark-unread-current", .activate = on_mark_unread },
  { .name = "print", .activate = on_print, .parameter_type = "s" },
  { .name = "print-current", .activate = on_print },
  { .name = "reply", .activate = on_reply, .parameter_type = "s" },
  { .name = "reply-all", .activate = on_reply_all, .parameter_type = "s" },
  { .name = "reply-all-current", .activate = on_reply_all },
  { .name = "reply-current", .activate = on_reply },
  { .name = "trash", .activate = on_trash },
  { .name = "view-source", .activate = on_view_source },
};

static void
on_toggle_sidebar (GtkToggleButton *btn G_GNUC_UNUSED,
                   gpointer             user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  AdwOverlaySplitView *osv = get_current_osv (self);

  if (!osv)
    return;

  stamp_folder_list_unselect (STAMP_FOLDER_LIST (self->folder_list));
  adw_overlay_split_view_set_show_sidebar (osv, !adw_overlay_split_view_get_show_sidebar (osv));
}

static void
on_layout_changed (AdwMultiLayoutView *view,
                   GParamSpec *ps      G_GNUC_UNUSED,
                   gpointer            user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  const char *name = adw_multi_layout_view_get_layout_name (view);
  gboolean narrow = g_strcmp0 (name, "desktop") != 0;
  GtkWidget *btn = stamp_conversation_list_get_sidebar_button (STAMP_CONVERSATION_LIST (self->conversation_list));

  gtk_widget_set_visible (btn, narrow);

  if (!narrow && self->saved_paned_pos > 50) {
    gtk_paned_set_position (self->desktop_paned, self->saved_paned_pos);
    gtk_paned_set_position (self->tablet_paned, self->saved_paned_pos);
  }
}

static void
on_paned_changed (GtkPaned      *paned,
                  GParamSpec *ps G_GNUC_UNUSED,
                  gpointer       user_data)
{
  StampMailView *self = STAMP_MAIL_VIEW (user_data);
  int pos = gtk_paned_get_position (paned);

  if (pos < 50)
    return;

  self->saved_paned_pos = pos;
}


void
stamp_mail_view_init (StampMailView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  adw_multi_layout_view_set_layout_name (self->mail_layout, "desktop");

  g_signal_connect (self->mail_layout, "notify::layout-name", G_CALLBACK (on_layout_changed), self);

  self->saved_paned_pos = 360;

  g_signal_connect (self->mail_layout, "notify::layout-name", G_CALLBACK (on_layout_changed), self);
  g_signal_connect (stamp_conversation_list_get_sidebar_button (STAMP_CONVERSATION_LIST (self->conversation_list)), "clicked", G_CALLBACK (on_toggle_sidebar), self);

  /* Remember paned position */
  g_signal_connect (self->desktop_paned, "notify::position", G_CALLBACK (on_paned_changed), self);
  g_signal_connect (self->tablet_paned, "notify::position", G_CALLBACK (on_paned_changed), self);

  /* Synchronize button state with show-sidebar */
  g_signal_connect_swapped (self->tablet_osv, "notify::show-sidebar", G_CALLBACK (on_sidebar_visibility_changed), self);
  g_signal_connect_swapped (self->mobile_osv, "notify::show-sidebar", G_CALLBACK (on_sidebar_visibility_changed), self);
}

GtkWidget *
stamp_mail_view_new (void)
{
  return g_object_new (STAMP_TYPE_MAIL_VIEW, NULL);
}

typedef struct {
  const char *action;
  const char *shortcut;
} Shortcut;

static const Shortcut MailShortcuts[] = {
  { "mail.composer-new", "<primary>n" },
  { "mail.forward-current", "<primary><shift>f" },
  { "mail.reply-all-current", "<primary><shift>r" },
  { "mail.reply-current", "<primary>r" },
  { "mail.mark-flag-current", "<primary>s" },
  { "mail.mark-read-current", "<primary><shift>i" },
  { "mail.mark-unflag-current", "<primary><shift>s" },
  { "mail.mark-unread-current", "<primary><shift>u" },
  { "mail.print-current", "<primary>p" },
  { "mail.trash", "<primary>d" },
  { "mail.view-source", "<primary>s" },
};

void
stamp_mail_view_setup (StampMailView *self,
                       AdwViewStack  *stack)
{
  GtkEventController *controller;

  /* adw_view_switcher_bar_set_stack (ADW_VIEW_SWITCHER_BAR (self->view_switcher), stack); */

  self->actions = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->actions),
                                   stamp_mail_view_action_entries,
                                   G_N_ELEMENTS (stamp_mail_view_action_entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "mail", G_ACTION_GROUP (self->actions));

  /* Note: Blueprint currently (January 2026) does not handled GtkNamedActions, so add it in code */
  controller = gtk_shortcut_controller_new ();
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  for (guint idx = 0; idx < G_N_ELEMENTS (MailShortcuts); idx++) {
    GtkShortcut *shortcut;

    shortcut = gtk_shortcut_new (gtk_shortcut_trigger_parse_string (MailShortcuts[idx].shortcut), gtk_named_action_new (MailShortcuts[idx].action));
    gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller), shortcut);
  }
}

void
stamp_mail_view_search_contact (StampMailView *self,
                                const char    *mail)
{
  const char *layout = adw_multi_layout_view_get_layout_name (self->mail_layout);

  stamp_mail_conversation_list_search_contact (STAMP_CONVERSATION_LIST (self->conversation_list), mail);
  if (g_strcmp0 (layout, "mobile") == 0 && self->mobile_nav)
    adw_navigation_view_pop_to_tag (self->mobile_nav, "main");
  /* adw_navigation_split_view_set_show_content (ADW_NAVIGATION_SPLIT_VIEW (self->outer_view), FALSE); */
}

GSimpleActionGroup *
stamp_mail_view_get_action_group (StampMailView *self)
{
  return self->actions;
}

void
stamp_mail_view_show_toast (StampMailView *self,
                            const char    *message)
{
  AdwToast *toast;

  toast = adw_toast_new (message);
  adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (self->toast_overlay), toast);
}
