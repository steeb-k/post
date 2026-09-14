#!/bin/sh
#
# Add a CI build to the apps.kznjk.com flatpak repo and sign the result.
#
#   tools/flatpak-publish.sh BUILD-REPO.tar OUTPUT-DIR
#
# The live repo is mirrored over HTTPS, the build's app and locale refs are
# committed on top of it, and the whole repo is signed and packed into
# OUTPUT-DIR as flatpak-repo.tar plus flatpak-repo.json. The server pulls
# those from the GitHub release and applies them only if its summary is still
# the one recorded as base_summary, so a publish can never overwrite something
# that changed on the server after the mirror was taken.
#
# The signing key comes from GPG_PRIVATE_KEY (armored) and GPG_PASSPHRASE.
# Needs flatpak, ostree, gpg and curl.
#
set -eu

BUILD_TAR=$(realpath "$1")
OUT=$(realpath "$2")
REPO_URL=${REPO_URL:-https://apps.kznjk.com/repo}
KEY=${GPG_KEY_ID:-D6A05D0395B0401C5134203655F485F01EDE0F50}
TITLE=apps.kznjk.com

WORK=$(mktemp -d)
trap 'gpgconf --kill gpg-agent 2>/dev/null || true; rm -rf "$WORK"' EXIT
REPO=$WORK/repo
mkdir -p "$OUT"

# ostree signs through gpgme, which can't be handed a passphrase. Preset it
# in the agent instead, for every keygrip so a signing subkey works too.
export GNUPGHOME="$WORK/gnupg"
mkdir -m 700 "$GNUPGHOME"
echo allow-preset-passphrase > "$GNUPGHOME/gpg-agent.conf"
printf '%s\n' "$GPG_PRIVATE_KEY" | gpg --batch --quiet --import
gpg --batch --list-secret-keys "$KEY" > /dev/null
PRESET=$(gpgconf --list-dirs libexecdir)/gpg-preset-passphrase
gpg --batch --with-colons --with-keygrip --list-secret-keys "$KEY" |
    awk -F: '$1 == "grp" { print $10 }' |
    while read -r grip; do
        printf '%s' "$GPG_PASSPHRASE" | "$PRESET" --preset "$grip"
    done
gpg --batch --export "$KEY" > "$WORK/key.gpg"

mkdir "$WORK/build"
tar -C "$WORK/build" -xf "$BUILD_TAR"

ostree init --repo="$REPO" --mode=archive-z2
status=$(curl -sS -o "$WORK/live-summary" -w '%{http_code}' "$REPO_URL/summary")
case $status in
200)
    base=$(sha256sum < "$WORK/live-summary" | cut -d' ' -f1)
    ostree remote add --repo="$REPO" --gpg-import="$WORK/key.gpg" \
        --set=gpg-verify-summary=true live "$REPO_URL"
    ostree pull --repo="$REPO" --mirror --depth=-1 live
    ostree remote delete --repo="$REPO" live
    ;;
404)
    echo "No repo at $REPO_URL yet, starting a new one."
    base=none
    ;;
*)
    echo "Fetching $REPO_URL/summary failed with HTTP $status." >&2
    exit 1
    ;;
esac

# Debug symbols stay out: they'd be most of the repo and nobody installs them.
refs=$(ostree refs --repo="$WORK/build" | grep -E '^(app|runtime)/' | grep -v '\.Debug/')
echo "Committing:"
echo "$refs"
# shellcheck disable=SC2086
flatpak build-commit-from --src-repo="$WORK/build" --gpg-sign="$KEY" \
    --no-update-summary "$REPO" $refs
flatpak build-update-repo --gpg-sign="$KEY" --title="$TITLE" \
    --generate-static-deltas --prune "$REPO"

# Check the result the way a client would: signed summary, every ref there.
export FLATPAK_USER_DIR="$WORK/flatpak"
flatpak remote-add --user --gpg-import="$WORK/key.gpg" check "file://$REPO"
listed=$(flatpak remote-ls --user --all --columns=ref check)
for ref in $refs; do
    echo "$listed" | grep -qxF "$ref" || {
        echo "$ref is missing from the signed summary." >&2
        exit 1
    }
done

tar -C "$REPO" --exclude=./tmp --exclude=./.lock -cf "$OUT/flatpak-repo.tar" .
cat > "$OUT/flatpak-repo.json" <<EOF
{
  "base_summary": "$base",
  "summary": "$(sha256sum < "$REPO/summary" | cut -d' ' -f1)",
  "tar_sha256": "$(sha256sum < "$OUT/flatpak-repo.tar" | cut -d' ' -f1)"
}
EOF
cat "$OUT/flatpak-repo.json"
