# Post

A modern mail suite written in C using GTK4 and Adwaita, built on the Evolution Data Server stack.

Post is a fork of [Stamp](https://gitlab.gnome.org/jbrummer/stamp).

Post installs as its own application and shares nothing with Stamp: the
application ID is `io.github.steeb_k.Post`, the binary is `post-mail`, and
settings live under `/io/github/steeb_k/Post/` in dconf. Both can be
installed side by side without collision.

Inside the source tree, filenames, C symbols and GObject types keep the
`stamp` prefix so that fixes from upstream still apply cleanly. Only the
names that reach the outside world — application ID, binary, GSettings
schemas, GResource paths, icons and the gettext domain — carry Post's own.

## Features

- **Mail** - Send, receive, and manage mails with full MIME support via Camel
- **Contacts** - Integrated address book management using EDS (libebook)
- **Calendar** - Calendar support via GNOME Calendar integration (pending)
- **PGP and S/MIME** - Sign and encrypt emails with OpenPGP and S/MIME
- **BIMI** - Brand Indicators for Message Identification support
- **One-Click Unsubscribe** - Offer simple one click unsubscribe from mailing lists 

## Accounts

Post does not manage accounts itself. Mail, contact and calendar
accounts come from GNOME Online Accounts through the Evolution Data
Server registry, and Post shows whatever it finds there.

Adding and removing accounts is done in **gnome-online-accounts-gtk**,
which is a required runtime dependency. It is a plain GTK application
and works on any desktop, unlike the Online Accounts panel in GNOME
Settings, which refuses to start outside GNOME. The welcome page and the
accounts entry in the menu open it.

```sh
# Arch
pacman -S gnome-online-accounts-gtk
```

Using GNOME Online Accounts for this is deliberate. Providers such as
Gmail need an OAuth client that Google has verified, and restricted
scopes like full mail access require a paid security assessment that is
renewed every year. GNOME Online Accounts already ships a verified
client, so accounts work without every user or packager having to
register one.

Packagers: this must be a hard dependency, not a suggestion. In a
Flatpak build the editor lives on the host and is started through
`flatpak-spawn --host`, so the sandbox needs `--talk-name=org.freedesktop.Flatpak`
and the host has to have the package installed.

## Building

Post uses the [Meson](https://mesonbuild.com) build system.

```sh
meson setup builddir
meson compile -C builddir
meson install -C builddir
```

## License and attribution

Post is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Post is a fork of [Stamp](https://gitlab.gnome.org/jbrummer/stamp) by
Jan-Michael Brummer, and its calendar is a vendored copy of [GNOME
Calendar](https://gitlab.gnome.org/GNOME/gnome-calendar). Most of the code
you are running was written by other people. See [AUTHORS](AUTHORS) for
who wrote what, and `src/gcal/VENDORED.md` for the vendored snapshot.

Post is not affiliated with Stamp and is not endorsed by its authors.
Report bugs in Post to Post.
