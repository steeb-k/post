# Known Issues

Issues found while testing this fork. Not yet fixed.

## Flatpak packaging needs work for this fork

`org.tabos.stamp.json` still describes the upstream setup. Nothing here
breaks the current local build, it only matters when the fork gets
packaged again.

The web process extension fix is not affected. It uses
`STAMP_WEB_PROCESS_EXTENSIONS_DIR`, which becomes `/app/lib/stamp` in a
Flatpak build and always lives inside the sandbox.

- The evolution-data-server module cleans up
  `/lib/evolution-data-server/*-backends`, which throws away
  `libebookbackendcarddav.so`. Adding a CardDAV address book would then
  succeed but never open. Narrow that cleanup so the address book
  backends survive.
- `gnome-online-accounts` is still built as a module and EDS is still
  built with `-DENABLE_GOA=ON`, even though the fork no longer has any
  GNOME Online Accounts entry point. Keeping the EDS side means accounts
  created earlier through GOA stay visible, so decide before dropping it.
- OAuth2 and Exchange should work as is. `-DENABLE_OAUTH2=ON` is set, the
  cleanup only removes `*-backends` and not `registry-modules`, so the
  google and outlook collection backends survive, and evolution-ews is
  bundled. Note the Exchange row appears in a Flatpak build even though
  it stays hidden on a system without evolution-ews installed.
- Untested: whether account creation talks to the bundled registry or the
  one on the host. The manifest already shares `~/.config/evolution` and
  allows `org.gnome.evolution.dataserver.Sources5`, so it should work,
  but if the two disagree then accounts added in the app end up somewhere
  the host does not read.

## Folders show up empty until the app is restarted

After adding an account the folder list appears, including custom folders,
but every folder shows no mail. Closing and reopening the app shows the
mail as expected.

The debug log shows the account init finishing normally
(`on_account_ready: 'Protonmail' services ready`) with nothing that points
at the cause, so this needs a closer look. Likely candidates are the
conversation list being built before the folder summary has been fetched
for the first time, or the initial refresh not emitting a change the list
model listens to.

## Autostart crashes the app

Turning on Autostart in Preferences freezes the app and then crashes it.
Reproduced several times. The setting itself is stored.

`on_autostart` in `src/stamp-preferences.c:183` builds the command line
for the portal in a `GPtrArray` but never terminates it:

    commandline = g_ptr_array_new_with_free_func (g_free);
    g_ptr_array_add (commandline, g_strdup ("stamp"));
    g_ptr_array_add (commandline, g_strdup ("--hidden"));

libportal passes that array to `g_variant_new_strv` with a length of -1,
which means it keeps reading until it finds a NULL. The array has no NULL
at the end, so it walks off into whatever follows. The crash backtrace:

    g_utf8_validate
    g_variant_new_string
    g_variant_new_strv
    libportal.so.1
    libportal-gtk4.so.1

Sometimes it reads a zero first and only prints
`g_variant_new_string: assertion 'string != NULL' failed`, other times it
reads garbage and gets SIGSEGV. Adding a trailing NULL entry, or using
`g_ptr_array_new_null_terminated`, should be all it takes.

## Background notifications hand the wrong object to the portal

Turning on Background Notifications prints:

    GLib-GObject-CRITICAL: invalid cast from 'AdwSwitchRow' to 'XdpPortal'
    libportal-CRITICAL: xdp_portal_request_background: assertion 'XDP_IS_PORTAL (portal)' failed

`on_background_notifications` in `src/stamp-preferences.c:156` starts with

    XdpPortal *portal = XDP_PORTAL (object);

but the callback is connected to `notify::active` on the switch row, so
`object` is the row, not a portal. It never creates one. `on_autostart`
right below it does it correctly with `xdp_portal_new ()`. The request is
dropped, so background running never actually gets requested.

## Crash in the conversation list while loading a folder

Seen once, with this backtrace:

    g_str_hash
    g_hash_table_insert
    stamp_conversation_list_load_folder
    load_folder_idle

That points at `src/views/mail/conversation-list/stamp-conversation-list.c:658`

    g_hash_table_insert (self->thread_cache, g_strdup (full_name), NULL);

which runs before the `account` null check below it and does not check
`full_name`. Worth checking whether this is connected to the empty folder
issue above, since both involve loading a folder right after startup.
