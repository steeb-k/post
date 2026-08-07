#!/usr/bin/env bash
#
# Build and run Post in a clean Arch container to check that the PKGBUILD
# actually declares everything the app needs.
#
# The container gets its own home, so none of the host's accounts, EDS
# registry or GSettings leak in. The app is then started under
# dbus-run-session, which is the part that makes this a real test: on a
# fresh bus nothing is already running, so every daemon Post depends on
# (evolution-source-registry, the calendar and addressbook factories,
# goa-daemon) has to be D-Bus activated out of the container's own
# packages. If a dependency is missing from the PKGBUILD, it fails here
# instead of silently borrowing the host's copy.
#
#   ./tools/test-clean-env.sh create    # make the container
#   ./tools/test-clean-env.sh build     # makepkg -si inside it
#   ./tools/test-clean-env.sh run       # launch on a private bus
#   ./tools/test-clean-env.sh shell     # poke around
#   ./tools/test-clean-env.sh destroy
#
set -euo pipefail

NAME=${NAME:-post-clean}
IMAGE=${IMAGE:-quay.io/archlinux/archlinux:latest}
SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CHOME=${CHOME:-$HOME/.local/share/post-clean-home}

need () {
  command -v "$1" >/dev/null || {
    echo "error: $1 is not installed. Run:  sudo pacman -S --needed --noconfirm podman distrobox" >&2
    exit 1
  }
}

case ${1:-help} in
create)
  need distrobox
  mkdir -p "$CHOME"
  distrobox create --yes --name "$NAME" --image "$IMAGE" \
    --home "$CHOME" \
    --volume "$SRC:/src:ro"
  # base-devel for makepkg; gnome-keyring so there is a Secret Service to
  # store credentials in, which is a host service everywhere and the one
  # thing we do not expect the app to bring itself.
  distrobox enter "$NAME" -- bash -lc '
    set -e
    sudo pacman -Syu --needed --noconfirm base-devel git gnome-keyring
  '
  echo "created '$NAME' with home $CHOME"
  ;;

build)
  need distrobox
  # makepkg needs somewhere writable; /src is mounted read-only on purpose
  # so the test cannot accidentally build against a dirty tree.
  distrobox enter "$NAME" -- bash -s <<'INNER'
set -e
rm -rf ~/build && mkdir -p ~/build && cp /src/PKGBUILD ~/build/
cd ~/build
POST_REPO=file:///src makepkg -si --noconfirm --needed
INNER
  ;;

run)
  need distrobox
  distrobox enter "$NAME" -- bash -s <<'INNER' 2>&1 | tee /tmp/post-clean-run.log
set -e
# distrobox propagates the host's XDG_DATA_DIRS, which puts the host's
# flatpak exports ahead of the container's own /usr/share. That leaks host
# D-Bus service files into what is supposed to be a clean distro, and they
# point at binaries that do not exist here. Use only the container's.
export XDG_DATA_DIRS=/usr/local/share:/usr/share
exec dbus-run-session -- bash -s <<'SESSION'
# A Secret Service is the one host service we still expect to exist.
eval "$(printf 'testpass' | gnome-keyring-daemon --unlock --components=secrets 2>/dev/null)"
echo "--- session bus: $DBUS_SESSION_BUS_ADDRESS"
post-mail
echo "--- daemons that got activated on this bus:"
pgrep -a -f 'evolution-|goa-daemon' || echo "  (none)"
SESSION
INNER
  ;;

shell)
  need distrobox
  distrobox enter "$NAME"
  ;;

destroy)
  need distrobox
  distrobox rm -f "$NAME" || true
  rm -rf "$CHOME"
  ;;

*)
  sed -n '2,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  ;;
esac
