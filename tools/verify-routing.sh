#!/bin/bash
# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Answer the question "why didn't that app's notification show up?" without
# guessing.
#
# There are three separate places a notification can be lost on the way to the
# daemon, and they look identical from the outside:
#
#   1. the app never sent one
#   2. it sent one through xdg-desktop-portal, and the portal handed it to a
#      backend that delivers somewhere else entirely (KDE's plasmanotify does
#      exactly this)
#   3. its sandbox blocked the D-Bus call, so it drew its own window instead
#
# This checks the static configuration for 2 and 3, then watches the bus so
# you can see for yourself whether 1 is what is happening.
#
#   tools/verify-routing.sh            # check config, then watch for 60s
#   tools/verify-routing.sh me.proton.Mail
set -uo pipefail

APP=${1:-}
SECS=${WATCH_SECONDS:-60}

hdr()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
info() { printf '  \033[2m%s\033[0m\n' "$*"; }

# ------------------------------------------------------- who owns the name --
hdr "Notification daemon"
pid=$(busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
      org.freedesktop.DBus GetConnectionUnixProcessID s \
      org.freedesktop.Notifications 2>/dev/null | awk '{print $2}')
name=$(ps -o comm= -p "${pid:-0}" 2>/dev/null)
if [ -n "$name" ]; then ok "org.freedesktop.Notifications is owned by $name (pid $pid)"
else bad "nothing owns org.freedesktop.Notifications — no daemon is running"; fi

# ------------------------------------------------------- portal backend -----
hdr "Portal notification backend"
info "Sandboxed apps go through xdg-desktop-portal, not straight to the daemon."
backend=""
for f in "${XDG_CONFIG_HOME:-$HOME/.config}/xdg-desktop-portal/"*portals.conf \
         /etc/xdg-desktop-portal/*portals.conf \
         /usr/share/xdg-desktop-portal/*portals.conf; do
  [ -f "$f" ] || continue
  v=$(grep -oP '^org\.freedesktop\.impl\.portal\.Notification=\K.*' "$f" 2>/dev/null | head -1)
  [ -n "$v" ] && { backend=$v; info "from $f"; break; }
done

case "$backend" in
  gtk)
    ok "backend is 'gtk' — forwards to whichever daemon owns the name" ;;
  plasmanotify)
    bad "backend is 'plasmanotify'"
    warn "That delivers into plasmashell's own notification system. Portal"
    warn "notifications never reach org.freedesktop.Notifications, so your"
    warn "daemon never sees them. Fix:"
    echo "        glassosd-setup      # offers to do this"
    ;;
  "")
    warn "no explicit backend configured; the desktop default applies" ;;
  *)
    info "backend is '$backend'" ;;
esac

# Live confirmation from the portal's own log.
live=$(journalctl --user -u xdg-desktop-portal --since "-24h" --no-pager 2>/dev/null \
       | grep -oP 'Using \K\S+(?=\.portal for org\.freedesktop\.impl\.portal\.Notification)' | tail -1)
[ -n "$live" ] && info "portal log says it is using: $live"

# ------------------------------------------------------- sandbox check ------
if [ -n "$APP" ]; then
  hdr "Sandbox permissions for $APP"
  if command -v flatpak >/dev/null && flatpak info "$APP" >/dev/null 2>&1; then
    if flatpak info --show-permissions "$APP" 2>/dev/null | grep -q "org.freedesktop.Notifications"; then
      ok "$APP may talk to org.freedesktop.Notifications directly"
    else
      warn "$APP has no direct notification bus permission."
      info "That is fine on libnotify 0.8+, which uses the portal automatically."
      info "If it still draws its own notification window, grant it directly:"
      echo "        flatpak override --user --talk-name=org.freedesktop.Notifications $APP"
      info "…and fully quit the app afterwards. Electron and Chromium probe"
      info "for a notification server once at startup and cache the answer."
    fi
    running=$(pgrep -f "$APP" | head -1)
    if [ -n "$running" ]; then
      started=$(ps -o lstart= -p "$running" 2>/dev/null)
      info "currently running since:$started"
      info "if that predates your last portal or daemon change, restart it"
    fi
  else
    info "$APP is not an installed Flatpak; skipping"
  fi
fi

# ------------------------------------------------------- watch the bus ------
hdr "Watching org.freedesktop.Notifications for ${SECS}s"
info "Trigger the notification now — send yourself mail, run a build, whatever."
info "Anything that reaches the daemon appears below. Nothing appearing means"
info "the app is not sending to this bus at all."
echo

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
timeout "$SECS" dbus-monitor --session \
  "interface='org.freedesktop.Notifications',member='Notify'" > "$tmp" 2>/dev/null

# grep -c prints 0 *and* exits 1 when it matches nothing, so a `|| echo 0`
# here appends a second zero and the arithmetic test then chokes on "0\n0".
count=$(grep -c "member=Notify" "$tmp" 2>/dev/null)
count=${count:-0}
echo
if [ "$count" -gt 0 ]; then
  ok "$count notification(s) reached the bus:"
  # Notify's signature is susssasa{sv}i, so the string arguments in order are
  # app_name, app_icon, summary, body. The hints dictionary contains strings
  # too, hence the explicit in-message flag rather than a running counter.
  awk '
    /member=Notify/ { inmsg=1; n=0; app=""; sum=""; next }
    inmsg && /^ *string "/ {
      n++
      line=$0; sub(/^ *string "/,"",line); sub(/"$/,"",line)
      if (n==1) app=line
      if (n==3) { sum=line; printf "      %-22s %s\n", app, sum; inmsg=0 }
    }' "$tmp"
else
  bad "nothing reached org.freedesktop.Notifications"
  info "If the app visibly showed a notification anyway, it drew its own —"
  info "see the sandbox section above."
fi
