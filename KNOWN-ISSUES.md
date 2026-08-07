# Known Issues

Issues found while testing this fork. Not yet fixed.

## Flatpak packaging notes

Accounts come from GNOME Online Accounts and are edited in
gnome-online-accounts-gtk, which runs on the host. `stamp_launch_goa()`
starts it with `flatpak-spawn --host`, so the manifest needs
`--talk-name=org.freedesktop.Flatpak` in its finish args, and the host
has to have the package installed. Nothing checks that at build time.

The evolution-data-server module used to clean up
`/lib/evolution-data-server/*-backends`, which threw away
`libebookbackendcarddav.so` and `libecalbackendcaldav.so`. The cleanup
is gone now; the backends are a few hundred kilobytes and every one of
them is reachable from an account Post supports.

The manifest still builds gnome-online-accounts as a module and EDS with
`-DENABLE_GOA=ON`. Both are needed now, so leave them alone. What is
missing is the editor itself, since the bundled GOA has no user
interface.

### The sandbox runs its own evolution-data-server (fixed)

The manifest used to clean `/libexec` and `/share/dbus-1` out of the EDS
module and ask for `--talk-name` on the dataserver names, so the flatpak
had no registry of its own and depended on the host having
evolution-data-server installed. That is fixed, and the fix has three
parts — all of them are needed, and it does not work with only some:

1. Keep `/libexec` and `/share/dbus-1`, so the factories and their
   service files are in the bundle.
2. Build EDS with `-DDBUS_SERVICES_PREFIX=io.github.steeb_k.Post`. The
   bus names become `io.github.steeb_k.Post.org.gnome.evolution.…`,
   which both avoids colliding with a host EDS and stays inside the
   names Flatpak lets the app own.
3. Start the factories from a wrapper installed as `/app/bin/post-mail`.
   This is the part that is easy to miss: the session bus **cannot**
   activate service files that live inside the sandbox, so shipping them
   is not enough — something has to launch the daemons. The wrapper
   checks each name and starts `/app/libexec/evolution-*` if nobody owns
   it. GNOME Evolution's Flatpak does the same thing.

Verified 2026-08-06: `/app/libexec/evolution-source-registry`,
`-addressbook-factory` and `-calendar-factory` all run inside the
sandbox alongside the host's own copies, with a private registry under
`~/.var/app/io.github.steeb_k.Post/config/evolution/`, and mail,
contacts and CalDAV calendars all work.

### OAuth account setup inside the flatpak is unverified

Adding a Google or Microsoft 365 account from Post's own accounts dialog
while sandboxed has not been shown to work end to end. Password-based
providers (IMAP/SMTP, WebDAV) are unaffected, and accounts added on the
host through GNOME Settings show up in the flatpak normally, because the
sandbox uses the host's GOA daemon.

The mechanism should work as things stand. Post owns
`org.gnome.OnlineAccounts.OAuth2` while an exchange is in flight
(`--own-name=org.gnome.OnlineAccounts.*`), the browser redirect is caught
by the *host's* handler, and that handler's only job is to put the
authorization code on the session bus under that name. The handler does
not need to live inside the sandbox. What has not happened is somebody
completing a real sign-in to confirm it.

Renaming the bundled handler to `io.github.steeb_k.Post.OAuth2.desktop`
so Flatpak would export it was tried and reverted: it made Post the
system-wide default for `x-scheme-handler/goa-oauth2`, ahead of the
host's own GNOME Online Accounts. An app should not take over a
session-wide scheme.

Note that using GOA's client id is not a misuse to be corrected. GOA is
a shared system account broker, and gnome-control-center, GNOME Calendar,
Contacts and Evolution all obtain accounts through it the same way.
Registering separate client ids would only be needed for a flatpak that
ran its own GOA daemon, and for Google that means verification of a
restricted scope with an annual third-party security assessment. Not
worth it for this.

### The flatpak still needs a GNOME Online Accounts daemon on the host

The one remaining host dependency. The bundle does ship `goa-daemon`,
but `org.gnome.OnlineAccounts` is not an app-id-prefixed name and is
already owned by the host's daemon, so the same rename-and-launch trick
that works for EDS does not apply. Confirmed by inspection: the name is
owned by the host's `/usr/lib/goa-daemon`, reached through
`--talk-name=org.gnome.OnlineAccounts`.

In practice the sandbox EDS then discovers the host's GOA accounts and
everything works, and Evolution's Flatpak has the same arrangement. But
GOA is the only way Post gets an account, so on a host without the GOA
daemon the flatpak would still come up empty.

Granting `--own-name=org.gnome.OnlineAccounts` so the bundled daemon
could claim the name when it is free was tried, and does work. It was
reverted anyway, because keeping the bundled daemon reachable is not
worth what it drags in.

The thing to know here, which cost an hour to find: **Flatpak exports
D-Bus service files out of the bundle with no app-id-prefix rule**,
unlike `.desktop` files. So once the EDS module stopped cleaning
`/share/dbus-1`, GOA's `org.gnome.OnlineAccounts.service` was exported
to `~/.local/share/flatpak/exports/share/dbus-1/services/` — a directory
that sits *ahead of* `/usr/share` in `XDG_DATA_DIRS`. The host would
then activate Post's sandboxed goa-daemon instead of its own, session
wide. It is nothing to do with `--own-name`; the file being present is
enough. The gnome-online-accounts module now deletes it explicitly.

The EDS service files are fine to ship: they carry the app id, courtesy
of `DBUS_SERVICES_PREFIX`, so they only ever claim Post's own names.

This is worth remembering for any future bundled daemon: check what
lands in the exports directory, because it is host-visible.

## Appstream metadata is still incomplete after the rebrand

`data/io.github.steeb_k.Post.metainfo.xml.in` carries placeholder text
that GNOME Software and the About dialog both show:

    <summary>Mails and more</summary>
    <description>
      <p>No description</p>
    </description>

The summary is upstream's and the description has never been written.

The screenshots were dropped during the rebrand rather than left in
place, because both entries pointed at `tabos.org/stamp/stamp1.png` and
would have shown Stamp's window and old icon on Post's page. Nothing
replaces them yet, so the component has no screenshots at all.
`appstreamcli validate` still passes without them.

Worth fixing before the repository goes public.

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
    g_ptr_array_add (commandline, g_strdup ("post-mail"));
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
