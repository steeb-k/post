#!/usr/bin/env bash
#
# Cross-build Post for ARM64 and produce a bundle to sideload onto a phone.
#
# The build runs on an x86_64 host under qemu user-mode emulation. What
# makes that work is the F ("fix binary") flag on the binfmt_misc
# registration: the kernel opens /usr/bin/qemu-aarch64-static once at
# registration time and keeps the descriptor, so the interpreter is still
# reachable inside flatpak-builder's mount namespace, where the host
# filesystem is not. Without F the aarch64 compiler would fail to exec
# the moment the build entered the sandbox.
#
# Everything lives under $WORKDIR rather than the source tree, because
# flatpak-builder requires its state dir and target dir to be on the same
# filesystem, and /home and /var/tmp are separate btrfs subvolumes here
# (same device, different st_dev, which is enough for it to refuse).
#
#   ./tools/build-aarch64.sh deps      # one-time host setup
#   ./tools/build-aarch64.sh build     # cross-build (~45 min on 16 cores)
#   ./tools/build-aarch64.sh bundle    # write the .flatpak
#   ./tools/build-aarch64.sh verify    # confirm it is really ARM64
#   ./tools/build-aarch64.sh clean
#
set -euo pipefail

APP_ID=io.github.steeb_k.Post
ARCH=aarch64
BRANCH=master
SRCDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="$SRCDIR/$APP_ID.json"
WORKDIR=${POST_AARCH64_WORKDIR:-/var/tmp/post-aarch64}
BUNDLE="$WORKDIR/Post-$ARCH.flatpak"

# Read the runtime out of the manifest so this never drifts from it.
runtime_version () {
    python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['runtime-version'])" "$MANIFEST"
}

cmd_deps () {
    local ver
    ver=$(runtime_version)

    if [[ ! -e /proc/sys/fs/binfmt_misc/qemu-$ARCH ]]; then
        echo "qemu-$ARCH is not registered. Install and register it:"
        echo "  sudo pacman -S --noconfirm qemu-user-static qemu-user-static-binfmt flatpak-builder"
        echo "  sudo systemctl restart systemd-binfmt"
        exit 1
    fi

    # Emulation that works on the host but not inside the sandbox is the
    # failure this checks for; see the note about the F flag above.
    if ! grep -q '^flags:.*F' /proc/sys/fs/binfmt_misc/qemu-$ARCH; then
        echo "qemu-$ARCH is registered without the F flag; the build will fail"
        echo "once it enters flatpak-builder's sandbox. Reinstall"
        echo "qemu-user-static-binfmt and restart systemd-binfmt."
        exit 1
    fi

    flatpak install -y --user flathub \
        "org.gnome.Platform/$ARCH/$ver" "org.gnome.Sdk/$ARCH/$ver"
}

cmd_build () {
    mkdir -p "$WORKDIR/state"
    # Seed the download cache from the native build so the tarballs and
    # git clones are not fetched a second time. Sources are arch
    # independent; only the build output differs.
    if [[ -d "$SRCDIR/.flatpak-builder/downloads" && ! -d "$WORKDIR/state/downloads" ]]; then
        cp -a "$SRCDIR/.flatpak-builder/downloads" "$WORKDIR/state/" || true
        cp -a "$SRCDIR/.flatpak-builder/git" "$WORKDIR/state/" 2>/dev/null || true
    fi

    rm -rf "$WORKDIR/build" "$WORKDIR/repo"
    flatpak-builder \
        --arch="$ARCH" \
        --user \
        --force-clean \
        --disable-rofiles-fuse \
        --state-dir="$WORKDIR/state" \
        --repo="$WORKDIR/repo" \
        "$WORKDIR/build" \
        "$MANIFEST"
}

cmd_bundle () {
    # --runtime-repo puts Flathub in the bundle's metadata, so installing
    # it on a phone that has no remotes configured still finds the
    # runtime instead of dying with "runtime not installed".
    flatpak build-bundle \
        --arch="$ARCH" \
        --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo \
        "$WORKDIR/repo" "$BUNDLE" "$APP_ID" "$BRANCH"
    ls -lh "$BUNDLE"
}

cmd_verify () {
    local bin="$WORKDIR/build/files/bin/post-mail.bin"
    file "$bin" | grep -q "ARM aarch64" \
        && echo "ok: binary is ARM aarch64" \
        || { echo "FAIL: $bin is not ARM aarch64"; exit 1; }
    grep -E "^runtime=" "$WORKDIR/build/metadata"
}

cmd_clean () { rm -rf "$WORKDIR"; }

case "${1:-}" in
    deps)   cmd_deps ;;
    build)  cmd_build ;;
    bundle) cmd_bundle ;;
    verify) cmd_verify ;;
    clean)  cmd_clean ;;
    *)      sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \?//' ; exit 1 ;;
esac
