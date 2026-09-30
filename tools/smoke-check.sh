#!/usr/bin/env bash
#
# Check, from the host, that everything Post's mail and calendars depend
# on is actually working -- the things that break underneath the app
# without the app being at fault, and that all look the same from inside
# it: a certificate prompt for a valid certificate, an account that sits
# offline, a calendar that will not authenticate.
#
# Run it after anything that touches the session: a flatpak install, a
# `systemctl --user` reload, a keyring or session-helper restart, a new
# build. Each check names what to do when it fails.
#
#   ./tools/smoke-check.sh              # check the running session
#   ./tools/smoke-check.sh --launch     # start Post, wait, then check
#   ./tools/smoke-check.sh --since '10 min ago'
#
# It never prints a password. Secrets pass through variables and pipes
# only.
#
set -uo pipefail

APP=io.github.steeb_k.Post
GOA=org.gnome.OnlineAccounts
ACCOUNTS=${XDG_CONFIG_HOME:-$HOME/.config}/goa-1.0/accounts.conf
RUNDIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
LAUNCH=0
SINCE=
SETTLE=${SETTLE:-150}

while [ $# -gt 0 ]; do
  case $1 in
  --launch) LAUNCH=1 ;;
  --since) SINCE=$2; shift ;;
  -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
  *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

fails=0
pass () { printf '  ok    %s\n' "$*"; }
fail () { printf '  FAIL  %s\n' "$*"; fails=$((fails + 1)); }
hint () { printf '        -> %s\n' "$*"; }
head_ () { printf '\n%s\n' "$*"; }

# Sandbox processes: the app itself and the data server it spawns.
sandbox_pids () { pgrep -f '/app/bin/post-mail.bin|/app/libexec/evolution-' 2>/dev/null; }

# ---------------------------------------------------------------- trust store
head_ "Flatpak trust store"

if systemctl --user is-active --quiet flatpak-session-helper.service; then
  pass "flatpak-session-helper is running"
else
  fail "flatpak-session-helper is not running"
  hint "systemctl --user start flatpak-session-helper.service"
fi

sock=$(ls "$RUNDIR"/.flatpak-helper/pkcs11-flatpak-* 2>/dev/null | head -1)
if [ -S "$sock" ] && pgrep -f "server --sh -n $sock" >/dev/null; then
  pass "p11-kit trust server is listening on $(basename "$sock")"
else
  fail "no live p11-kit trust server for the session helper"
  hint "systemctl --user restart flatpak-session-helper.service, then relaunch every flatpak app"
fi

stale=0
for pid in $(sandbox_pids); do
  if grep -q 'p11-kit/pkcs11' "/proc/$pid/mountinfo" 2>/dev/null &&
     grep 'p11-kit/pkcs11' "/proc/$pid/mountinfo" | grep -q '//deleted'; then
    stale=$((stale + 1))
    fail "$(ps -o comm= -p "$pid") ($pid) is bound to a dead trust socket"
  fi
done
if [ "$stale" -eq 0 ]; then
  pass "no sandboxed Post process holds a dead trust socket"
else
  hint "flatpak ps; flatpak kill <every $APP instance>; then relaunch"
fi

# TLS from a fresh sandbox against the CalDAV host, so the check sees
# exactly what the app's own gnutls sees.
host=$(sed -n 's|^CalDavUri=https://\([^@/]*@\)\{0,1\}\([^:/]*\).*|\2|p' "$ACCOUNTS" 2>/dev/null | head -1)
host=${SMOKE_TLS_HOST:-$host}
if [ -n "$host" ]; then
  probe=$(mktemp /tmp/post-smoke-XXXXXX.py)
  cat > "$probe" <<'EOF'
import gi, sys
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib
try:
    c = Gio.SocketClient(tls=True)
    conn = c.connect_to_host(sys.argv[1], 443, None)
    print("ok", conn.get_base_io_stream().get_peer_certificate_errors())
except GLib.Error as e:
    print("err", e.message)
EOF
  out=$(timeout 60 flatpak run --command=python3 "$APP" "$probe" "$host" 2>/dev/null | tail -1)
  rm -f "$probe"
  case $out in
  "ok 0"|"ok <flags 0>"*) pass "fresh sandbox verifies https://$host" ;;
  *) fail "fresh sandbox cannot verify https://$host: ${out:-no answer}"
     hint "the runtime trusts through the host's p11-kit server; see the trust-server check above" ;;
  esac
fi

# --------------------------------------------------------------- credentials
head_ "GNOME Online Accounts"

goa_pid=$(pgrep -x goa-daemon | head -1)
kr_pid=$(pgrep -f gnome-keyring-daemon | head -1)
if [ -n "$goa_pid" ] && [ -n "$kr_pid" ]; then
  goa_start=$(ps -o lstart= -p "$goa_pid" | xargs -I{} date -d {} +%s)
  kr_start=$(ps -o lstart= -p "$kr_pid" | xargs -I{} date -d {} +%s)
  if [ "$goa_start" -ge "$kr_start" ]; then
    pass "goa-daemon ($goa_pid) started after the keyring"
  else
    fail "goa-daemon ($goa_pid) predates the keyring daemon; its keyring session is stale"
    hint "kill $goa_pid  (D-Bus respawns it)"
  fi
elif [ -z "$goa_pid" ]; then
  pass "goa-daemon is not running yet; it starts on first use"
fi

accounts=$(sed -n 's/^\[Account \(.*\)\]$/\1/p' "$ACCOUNTS" 2>/dev/null)
if [ -z "$accounts" ]; then
  fail "no accounts in $ACCOUNTS"
fi

# Reads one key of one account block.
acct_key () { awk -v a="[Account $1]" -v k="$2" '$0==a{f=1;next} /^\[/{f=0} f&&index($0,k"=")==1{print substr($0,length(k)+2)}' "$ACCOUNTS"; }

goa_call () { # account method [args...]
  local a=$1; shift
  busctl --user --no-pager --timeout=60s call "$GOA" "/org/gnome/OnlineAccounts/Accounts/$a" "$@" 2>&1
}

for a in $accounts; do
  provider=$(acct_key "$a" Provider)
  label="$a ($provider)"

  out=$(goa_call "$a" "$GOA.Account" EnsureCredentials)
  case $out in
  "i "*) pass "$label: EnsureCredentials passes" ;;
  *) fail "$label: EnsureCredentials: ${out#Call failed: }"
     case $out in
     *"certificate"*) hint "the daemon rejects a certificate this account talks to; for the local bridge set the matching AcceptSslErrors=true in $ACCOUNTS" ;;
     *"keyring"*) hint "restart goa-daemon: kill $(pgrep -x goa-daemon)" ;;
     esac ;;
  esac

  if [ "$provider" = imap_smtp ]; then
    keys="imap-password smtp-password"
  else
    keys="$a"
  fi
  for k in $keys; do
    out=$(goa_call "$a" "$GOA.PasswordBased" GetPassword s "$k")
    case $out in
    's "'*) pass "$label: GetPassword($k) returns a password" ;;
    *) fail "$label: GetPassword($k): ${out#Call failed: }"
       hint "if the keyring restarted after goa-daemon, kill goa-daemon; otherwise the account needs its password re-entered" ;;
    esac
  done

  if [ "$provider" = imap_smtp ]; then
    imap_host=$(acct_key "$a" ImapHost); imap_user=$(acct_key "$a" ImapUserName)
    smtp_host=$(acct_key "$a" SmtpHost); smtp_user=$(acct_key "$a" SmtpUserName)
    for proto in imap smtp; do
      eval "h=\$${proto}_host; u=\$${proto}_user"
      pw=$(goa_call "$a" "$GOA.PasswordBased" GetPassword s "$proto-password" | sed -n 's/^s "\(.*\)"$/\1/p')
      [ -n "$pw" ] || continue
      out=$(SMOKE_PW="$pw" SMOKE_USER="$u" SMOKE_HOST="$h" SMOKE_PROTO="$proto" python3 - <<'EOF' 2>&1
import os, ssl, imaplib, smtplib, base64
proto, host, user, pw = os.environ["SMOKE_PROTO"], os.environ["SMOKE_HOST"], os.environ["SMOKE_USER"], os.environ["SMOKE_PW"]
h, _, p = host.partition(":")
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
try:
    if proto == "imap":
        m = imaplib.IMAP4(h, int(p or 143), timeout=20); m.starttls(ctx); m.login(user, pw); m.logout()
    else:
        s = smtplib.SMTP(h, int(p or 587), timeout=20); s.starttls(context=ctx); s.login(user, pw); s.quit()
    print("ok")
except Exception as e:
    print("err %s: %s" % (type(e).__name__, str(e).replace(pw, "<password>")))
EOF
)
      unset pw
      case $out in
      ok) pass "$label: $proto login to $h works with the stored password" ;;
      *) fail "$label: $proto login to $h: ${out#err }"
         hint "is the bridge running and is this the password it currently shows?" ;;
      esac
    done
  fi
done

# ------------------------------------------------------------------- the app
if [ "$LAUNCH" -eq 1 ]; then
  head_ "Launching $APP and waiting ${SETTLE}s"
  SINCE=$(date '+%Y-%m-%d %H:%M:%S')
  flatpak run "$APP" >/dev/null 2>&1 &
  sleep "$SETTLE"
fi

head_ "Post's own log"

if [ -z "$SINCE" ]; then
  pid=$(pgrep -f '/app/bin/post-mail.bin' | head -1)
  if [ -n "$pid" ]; then
    SINCE=$(ps -o lstart= -p "$pid" | xargs -I{} date -d {} '+%Y-%m-%d %H:%M:%S')
  fi
fi

if [ -z "$SINCE" ]; then
  fail "Post is not running and no --since was given, so there is no log to check"
  hint "run with --launch, or start Post and run this again"
else
  log=$(journalctl --user --no-pager -o short-iso --since "$SINCE" 2>/dev/null | grep -E 'post-mail\[')
  count () { printf '%s' "$log" | grep -c -E "$1"; }

  n=$(count "support prompt for credentials|Failed to get password from GOA")
  if [ "$n" -eq 0 ]; then pass "no credential failures since $SINCE"; else
    fail "$n credential failures since $SINCE"
    hint "see the GOA checks above; EnsureCredentials must pass before EDS hands out a password"
  fi

  n=$(count "Unacceptable TLS certificate|is not trusted|certificate authority is not known")
  if [ "$n" -eq 0 ]; then pass "no TLS trust failures"; else
    fail "$n TLS trust failures"
    hint "see the trust-store checks above"
  fi

  n=$(count "Error connecting to service|Refresh of .* went silent")
  if [ "$n" -eq 0 ]; then pass "no connection failures"; else
    fail "$n connection failures"
  fi

  if [ "$LAUNCH" -eq 1 ] || [ -n "$SINCE" ]; then
    n=$(count "refresh_next_in_queue: Refreshing INBOX")
    if [ "$n" -gt 0 ]; then pass "INBOX refreshed ($n times)"; else
      fail "INBOX never refreshed since $SINCE"
    fi
  fi
fi

# ------------------------------------------------------------------ the wire
# A login that goes nowhere looks fine in every log above. The bridge
# writes every IMAP command it receives, so ask it whether Post did any
# real work since launch: opened a folder, fetched, or sat in IDLE.
bridge_log=$(ls -t "$HOME"/.var/app/ch.protonmail.protonmail-bridge/data/protonmail/bridge-v3/logs/*_bri_*.log 2>/dev/null | head -1)
if [ -n "$bridge_log" ] && [ -n "$SINCE" ]; then
  head_ "Proton bridge"
  # mawk has no capture groups, so cut the timestamp out by position:
  # every line starts with time="YYYY-MM-DD HH:MM:SS.mmm".
  n=$(awk -v since="$SINCE" 'index($0, "time=\"") == 1 && substr($0, 7, 19) >= since && /msg="[A-Z0-9]+ (SELECT|EXAMINE|FETCH|IDLE|STATUS|UID) /' "$bridge_log" | wc -l)
  if [ "$n" -gt 0 ]; then pass "Post did real IMAP work since $SINCE ($n commands)"; else
    fail "the bridge saw no SELECT, FETCH or IDLE from Post since $SINCE; only logins or nothing"
    hint "the account is not actually connected, whatever the log says; see the credential checks above"
  fi
fi

head_ "Result"
if [ "$fails" -eq 0 ]; then
  echo "  all checks passed"
else
  echo "  $fails check(s) failed"
fi
exit $(( fails > 0 ))
