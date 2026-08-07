# Maintainer: Steve Kzenjak <skaznak@protonmail.com>

# Point this at a different checkout or a remote to package something other
# than the working tree, e.g.
#   POST_REPO=https://example.org/post.git makepkg -si
_repo="${POST_REPO:-file://${startdir}}"

pkgname=post-mail-git
_pkgname=post-mail
pkgver=0.4.0
pkgrel=1
pkgdesc='Mail, contacts and calendar client built on Evolution Data Server'
arch=('x86_64' 'aarch64')
url='https://github.com/steeb-k/post'
license=('GPL-3.0-or-later')
provides=("${_pkgname}")
conflicts=("${_pkgname}")

depends=(
  # Toolkit and web view. The GTK and libadwaita floors come from the
  # vendored GNOME Calendar, not from Post's own code, and they are much
  # higher than Post alone would need.
  'gtk4>=4.21.2'
  'libadwaita>=1.8'
  'webkitgtk-6.0'
  'libportal-gtk4>=0.6'
  'gsettings-desktop-schemas'

  # Evolution Data Server. This one package supplies both the client
  # libraries Post links against (camel, libedataserver, libecal, libebook,
  # libedata-book, libebook-contacts) *and* the daemons Post cannot work
  # without: evolution-source-registry, evolution-calendar-factory,
  # evolution-addressbook-factory, and the CalDAV/CardDAV backends. Nothing
  # links those daemons, so no automatic dependency scan will find them.
  'evolution-data-server'
  'libedataserverui4'
  'libical'

  # Accounts. libgoa is the client library; gnome-online-accounts provides
  # both libgoa-backend (the provider dialogs Post now hosts in-process) and
  # goa-daemon, which owns the account store. Also D-Bus activated, so also
  # invisible to dependency scanning.
  'libgoa'
  'gnome-online-accounts'

  # Vendored GNOME Calendar. libgweather is used for one thing, the event
  # editor's timezone picker; it pulls in gweather-locations and
  # geocode-glib behind it.
  'libgweather-4'
  'fribidi'

  'glib2'
  'libsoup3'
  'libpsl'
  'nss'
  'gpgme'

  # A notification sound is the only use of GStreamer; playbin lives in
  # gst-plugins-base-libs.
  'gstreamer'
  'gst-plugins-base-libs'
)

makedepends=(
  'meson>=1.0.0'
  'ninja'
  'git'
  'blueprint-compiler'
  'appstream'
  'desktop-file-utils'
  # glib-mkenums and glib-genmarshal live here, not in glib2. Easy to miss,
  # because anything that has ever built a GNOME app already has it.
  'glib2-devel'
)

optdepends=(
  'gnome-keyring: store account credentials (any Secret Service provider will do)'
  'xdg-desktop-portal: open links and files from the message view'
)

source=("${_pkgname}::git+${_repo}")
sha256sums=('SKIP')

pkgver() {
  cd "${srcdir}/${_pkgname}"
  local _tag
  _tag=$(grep -m1 -oP "version:\s*'\K[0-9.]+" meson.build)
  printf '%s.r%s.g%s' "${_tag}" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  arch-meson "${_pkgname}" build
  meson compile -C build
}

check() {
  meson test -C build --print-errorlogs
}

package() {
  meson install -C build --destdir "${pkgdir}"
}
