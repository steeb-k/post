#!/usr/bin/env bash
#
# Prove the flatpak can run with no GNOME Online Accounts daemon on the
# host — the last host dependency Post has.
#
# A throwaway D-Bus session cannot test this: OAuth needs a browser, the
# browser lives on the real session bus, so the redirect never comes
# back. This stays on the real bus and removes the *host's* GOA daemon
# from it:
#
#   * shadows its D-Bus service file so it cannot be activated
#     (XDG_DATA_HOME is searched before /usr/share)
#   * kills it if it is running
#   * keeps a watchdog running that kills it again if anything starts it
#     directly — shadowing only blocks activation, not direct launches,
#     and without this the host daemon quietly comes back and every
#     result afterwards is meaningless
#
# The sandbox keeps its accounts in ~/.var/app/io.github.steeb_k.Post/,
# so host accounts in ~/.config/goa-1.0 are untouched and invisible to
# it. Whatever you see is what a machine with no GOA would see.
#
#   ./tools/test-without-host-goa.sh start
#   ./tools/test-without-host-goa.sh status
#   ./tools/test-without-host-goa.sh restore
#
# While active, other desktop apps using GNOME Online Accounts will see
# no accounts. `restore` undoes everything.
#
set -euo pipefail

APP=io.github.steeb_k.Post
NAME=org.gnome.OnlineAccounts
SHADOW="$HOME/.local/share/dbus-1/services/${NAME}.service"
WATCHPID="${XDG_RUNTIME_DIR:-/tmp}/post-goa-watchdog.pid"
WATCHLOG="${XDG_RUNTIME_DIR:-/tmp}/post-goa-watchdog.log"

host_goa_pids () { ps -eo pid=,cmd= | awk '$0 ~ /\/usr\/lib\/goa-daemon/ && $0 !~ /awk/ {print $1}'; }
sandbox_goa_pids () { ps -eo pid=,cmd= | awk '$0 ~ /\/app\/libexec\/goa-daemon/ && $0 !~ /awk/ {print $1}'; }

owner_cmd () {
  local o p
  o=$(gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.GetNameOwner "$NAME" 2>/dev/null | tr -d "(',)") || return 1
  p=$(gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.GetConnectionUnixProcessID "$o" 2>/dev/null | tr -d '(,)uint32 ')
  ps -o cmd= -p "$p" 2>/dev/null
}

stop_watchdog () {
  [ -f "$WATCHPID" ] || return 0
  kill "$(cat "$WATCHPID")" 2>/dev/null || true
  rm -f "$WATCHPID"
}

case ${1:-help} in
start)
  mkdir -p "$(dirname "$SHADOW")"
  printf '[D-BUS Service]\nName=%s\nExec=/bin/false\n' "$NAME" > "$SHADOW"
  echo "shadowed the host service file"

  # stop anything already running in the sandbox, then the host daemon
  flatpak kill "$APP" 2>/dev/null || true
  sleep 2
  host_goa_pids | xargs -r kill 2>/dev/null || true
  sleep 2

  stop_watchdog
  : > "$WATCHLOG"
  # Shadowing stops activation but not a direct launch, so keep watching.
  ( while :; do
      pids=$(ps -eo pid=,cmd= | awk '$0 ~ /\/usr\/lib\/goa-daemon/ && $0 !~ /awk/ {print $1}')
      if [ -n "$pids" ]; then
        echo "$(date +%H:%M:%S) host goa-daemon reappeared (pid $pids) — killed" >> "$WATCHLOG"
        echo "$pids" | xargs -r kill 2>/dev/null || true
      fi
      sleep 2
    done ) & echo $! > "$WATCHPID"
  echo "watchdog running (pid $(cat "$WATCHPID")), log: $WATCHLOG"

  echo "launching the flatpak…"
  nohup flatpak run --user "$APP" >/tmp/post-no-host-goa.log 2>&1 &
  sleep 30

  echo
  echo "--- verification -------------------------------------------"
  h=$(host_goa_pids | tr '\n' ' ');  s=$(sandbox_goa_pids | tr '\n' ' ')
  echo "  host goa-daemon    : ${h:-none}"
  echo "  sandbox goa-daemon : ${s:-none}"
  echo "  owner of $NAME:"
  echo "      $(owner_cmd || echo 'nobody')"
  if [ -n "$h" ]; then
    echo "  RESULT: INVALID — the host daemon is running; results mean nothing."
  elif [ -z "$s" ]; then
    echo "  RESULT: FAILED — the bundled daemon did not start."
  else
    echo "  RESULT: VALID — only the sandbox daemon exists."
  fi
  echo "------------------------------------------------------------"
  echo "run '$0 restore' when done."
  ;;

status)
  echo "shadow file : $([ -f "$SHADOW" ] && echo present || echo absent)"
  echo "watchdog    : $([ -f "$WATCHPID" ] && kill -0 "$(cat "$WATCHPID")" 2>/dev/null && echo "running (pid $(cat "$WATCHPID"))" || echo stopped)"
  echo "host goa    : $(host_goa_pids | tr '\n' ' ' | sed 's/^$/none/')"
  echo "sandbox goa : $(sandbox_goa_pids | tr '\n' ' ' | sed 's/^$/none/')"
  echo "name owner  : $(owner_cmd || echo nobody)"
  [ -s "$WATCHLOG" ] && { echo "watchdog kills:"; sed 's/^/  /' "$WATCHLOG"; }
  ;;

restore)
  stop_watchdog
  rm -f "$SHADOW"
  flatpak kill "$APP" 2>/dev/null || true
  sleep 2
  gdbus call --session --dest "$NAME" --object-path /org/gnome/OnlineAccounts \
    --method org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1 || true
  sleep 2
  echo "restored. name owner: $(owner_cmd || echo 'nobody yet — activates on next use')"
  echo "your host accounts in ~/.config/goa-1.0 were never touched."
  ;;

*)
  sed -n '2,29p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  ;;
esac
