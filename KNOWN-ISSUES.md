# Known Issues

Issues found while testing this fork. Not yet fixed.

## Flatpak packaging notes

Accounts come from GNOME Online Accounts and are edited in
gnome-online-accounts-gtk, which runs on the host. `stamp_launch_goa()`
starts it with `flatpak-spawn --host`, so the manifest needs
`--talk-name=org.freedesktop.Flatpak` in its finish args, and the host
has to have the package installed. Nothing checks that at build time.

The evolution-data-server module cleans up
`/lib/evolution-data-server/*-backends`, which throws away
`libebookbackendcarddav.so`. A CardDAV address book added in GNOME
Online Accounts then shows up in the account but never opens. Narrow
that cleanup so the address book backends survive.

The manifest still builds gnome-online-accounts as a module and EDS with
`-DENABLE_GOA=ON`. Both are needed now, so leave them alone. What is
missing is the editor itself, since the bundled GOA has no user
interface.

Untested: whether the bundled registry or the one on the host wins. The
manifest already shares `~/.config/evolution` and allows
`org.gnome.evolution.dataserver.Sources5`, so accounts created on the
host should be visible, but if the two disagree then nothing shows up.

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
