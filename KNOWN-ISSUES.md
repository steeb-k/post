# Known Issues

Issues found while testing this fork. Not yet fixed.

## Profiles: snags to watch

Not reproduced failures, just the places this is thin. The schedule
itself was exercised — rule matching, midnight wrap, override expiry —
and the filtering was checked live against all three accounts.

**The editor's rule widgets have been seen, not driven.** The schedule
rows render correctly, but nothing has clicked a weekday toggle or
dragged a spin button, because there is no input-injection tool on the
development machine. The write path they use (`rule_row_apply()`) is
shared with nothing else.

**A profile that shows no mail account leaves the composer stranded.**
`load_from_combobox()` prefers an in-profile identity and falls back to
the first identity of any kind, so there is always a sender — but it
will be one the active profile hides, marked as such. Nothing warns
before sending.

**Removing an account does not clean up profiles.** Membership is
stored as a collection `ESource` uid; delete the account and the uid
stays in every profile that listed it, harmlessly but forever. If the
same account is added back it gets a new uid, so the profile will not
pick it up again — it looks like the profile forgot.

**A profile with no accounts hides everything.** That is what it says it
does, and the mail view empties correctly, but it reads as a broken app
rather than an empty profile. There is no "this profile shows nothing"
state anywhere.

**Overlapping schedules resolve by list order**, which the user cannot
see or change. See TODO.md.

**Switching profiles drops and rebuilds calendar monitors.**
`gcal_manager_refilter_calendars()` removes calendars from the timeline
and adds them back, and `gcal_timeline_add_calendar()` builds a fresh
`GcalCalendarMonitor` each time, so a calendar returning to view
refetches its range. Fine for something that happens on a profile
switch; would not be fine if anything started calling it frequently.

**A profile's layout override has only been driven from the action, not
the editor.** The combo row in the editor writes through
`stamp_profile_set_layout()` and the manager stores it in the
`profile-layouts` map, but as with the schedule widgets, nothing on this
machine can click it. What was exercised is `win.mail-layout` over
D-Bus: it moves the default, and the mail view follows it into all three
layouts without complaint. The path that was reasoned about rather than
run is a profile's turn *ending* while a layout was picked by hand
during it.

**The layout lives in a map beside the profiles, not in the profile.**
Growing the `a(sssasa(yqq))` tuple would change the key's type, and
GSettings answers a type mismatch by dropping the stored value, which
would silently delete every profile on upgrade. So `profile-layouts` is
`a{ss}` keyed by profile id, and inherits the wart the accounts list
already has: removing a profile outside the manager would leave its
layout behind.

**A two-way `g_settings_bind()` on a tree row records what the model
does, not what the user does.** The folder list bound an account row's
`expanded` straight to its GSettings key, so when a profile switch
filtered the account out of the tree, the row collapsed on its way out
and wrote `false`. The account came back collapsed, every time, and the
state degraded across switches. Expansion is now read at bind time and
written from an explicit handler that stands down while the tree is
being refiltered and when the row has already lost its item. The same
guard was needed for the per-folder `expanded-folders` list, which had
the additional bug of dereferencing that missing item.

**With every account collapsed nothing could restore a selection.** The
folder-restore path only ran when a folder row was bound, and a
collapsed account has no folder rows -- so the mail list sat empty and
the window read as broken. The restore now also runs when an account
row appears, and opens the one account the remembered folder lives in.

**The strip's "at most half the sidebar" cap only recomputes when the
agenda is toggled.** It was written to run from `size_allocate()`, which
GTK4 never calls on this widget: a widget with a layout manager --
`GtkBox` has one -- has its allocation done by the layout manager, and
the vfunc is skipped. The dead override is gone. Expanding or collapsing
the agenda recomputes the cap, so it self-corrects, but resizing the
window alone does not.

**The schedule resolves on a 60-second tick.** Deliberate — it means a
suspended laptop, a changed timezone or a stepped clock heal themselves
on the next tick instead of leaving a timer armed for a moment that has
passed. The cost is that a transition can be up to a minute late.

## Profiles do not scope calendar notifications, because there are none

A profile mutes new-mail notifications for the accounts it hides
(`stamp_profile_shows_account()` gates the `GNotification` in
`src/views/mail/folder-list/stamp-folder-item.c`). It does *not* mute
calendar event reminders — Post has never had any. The only
`GNotification` in the whole application is the new-mail one, and event
reminders would normally come from EDS's `evolution-alarm-notify`, which
the flatpak does not bundle and Post does not start.

So the calendar half of "only notify me for this profile's accounts" is
satisfied vacuously today. Whenever event reminders are added, they must
be filtered the same way, from the calendar's parent collection:

```c
ESource *parent = gcal_calendar_get_parent_source (calendar);
if (parent && !stamp_profile_shows_account (e_source_get_uid (parent)))
  return;
```

The pieces to build it from are already in place: `StampTodayCounter`
(`src/views/calendar/stamp-today-counter.c`) shows how to subscribe to
the gcal timeline without a widget, and `GcalEvent` already parses
VALARM triggers into `gcal_event_get_alarms()`. Note that the timeline a
subscriber sees is *already* profile-filtered, so a reminder service
built on it would inherit the filtering for free — but it would then go
quiet for hidden accounts even when the user wants a reminder, which is
a design decision to make deliberately rather than inherit by accident.

## Flatpak packaging notes

Accounts come from GNOME Online Accounts. Post hosts GOA's own provider
dialogs in process through `libgoa-backend`, so it no longer shells out
to gnome-online-accounts-gtk and no longer needs
`--talk-name=org.freedesktop.Flatpak`. The account *store* is the host's
goa-daemon when there is one and the bundled daemon otherwise; see below.

The evolution-data-server module used to clean up
`/lib/evolution-data-server/*-backends`, which threw away
`libebookbackendcarddav.so` and `libecalbackendcaldav.so`. The cleanup
is gone now; the backends are a few hundred kilobytes and every one of
them is reachable from an account Post supports.

The manifest builds gnome-online-accounts as a module and EDS with
`-DENABLE_GOA=ON`; both are needed. `goabackend` is enabled too, since
that is the library carrying the provider dialogs Post now presents
itself. Neither `goa-1.0` nor `goa-backend-1.0` is in the GNOME runtime,
so bundling the module is the only way to link against them.

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

### The sandbox runs its own GNOME Online Accounts daemon (fixed)

`org.gnome.OnlineAccounts` is not an app-id-prefixed name, so the
rename-and-launch trick used for EDS does not apply to it. Instead the
manifest grants `--own-name=org.gnome.OnlineAccounts` (and `.*`), and
the wrapper starts `/app/libexec/goa-daemon` only when nobody already
owns the name. A host with its own daemon keeps winning, so accounts
stay shared there, exactly as Evolution's Flatpak behaves; a host with
no GOA at all gets the bundled one.

Verified 2026-08-07 with `tools/test-without-host-goa.sh`, which
shadows the host's service file, kills the daemon, and keeps a watchdog
killing it if anything relaunches it — shadowing blocks activation but
not a direct launch, and without the watchdog the host daemon creeps
back and every result afterwards is meaningless. Under a run the script
reported VALID, the bundled daemon served Google over OAuth2, plus
Nextcloud and a Proton Bridge IMAP/SMTP account.

One trap for self-signed servers: GOA prompts to accept the certificate
**once per leg**, IMAP and SMTP separately. If only one prompt is
answered the account is left half-provisioned — EDS then makes no
credential calls for it at all and mail shows no folders, with no error
anywhere. Removing and re-adding the account fixes it. GOA's cert
acceptance (`ImapAcceptSslErrors` / `SmtpAcceptSslErrors`) is separate
from camel's per-fingerprint exceptions in `camel_certs`; having the
latter does not help GOA.

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

## Dependencies worth trimming

Not bugs, just weight. Came out of costing what a native package would
have to pull in, and they apply to the flatpak bundle equally.

**GStreamer for notification sounds.** `src/meson.build` depends on
`gstreamer-1.0` to play a sound on new mail. That is an enormous stack
for one purpose; `libcanberra` or `gsound` does the same job in a
fraction of the size, and GNOME apps generally use the latter. Check
what else, if anything, reaches for GStreamer before swapping it.

**libgweather for time zones.** Bundled solely so the calendar can
resolve zone names, and it drags `geocode-glib` and a
`gweather-locations` database along with it — three modules in the
manifest for what the system tzdata already knows. The vendored gcal
code is what uses it, so this is a `src/gcal/**` patch, and vendored
patches are separate commits marked `/* Post: ... */`.

**Verifying a floor is not the same as declaring one.** `src/meson.build`
declared `libadwaita-1 >= 1.6` while `src/shortcuts-dialog.blp` used
`Adw.ShortcutsDialog`, which is `ADW_AVAILABLE_IN_1_8`. It built here
only because this machine has 1.9.2. Nothing catches this except
building against the declared minimum, so treat every version floor in
that file as unverified until something does.
