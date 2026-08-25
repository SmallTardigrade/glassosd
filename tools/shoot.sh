#!/bin/bash
# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Capture the README screenshots inside a nested, isolated session.
#
# Shooting the real desktop puts whatever the user happens to have open into
# the picture — a privacy problem and a reproducibility one. This runs a
# throwaway kwin_wayland on its own Wayland socket, its own D-Bus session and
# its own XDG_CONFIG_HOME, so a frame can only ever contain glassosd. Nothing
# it does touches the running session.
#
#   tools/shoot.sh [outdir]
set -euo pipefail

OUT=${1:-screenshots}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=$ROOT/build/glassosd
[ -x "$BIN" ] || { echo "build glassosd first: cmake --build build" >&2; exit 1; }
command -v kwin_wayland >/dev/null || { echo "needs kwin_wayland" >&2; exit 1; }
command -v grim >/dev/null || command -v spectacle >/dev/null || {
  echo "needs grim or spectacle" >&2; exit 1; }

mkdir -p "$ROOT/$OUT"
WORK=$(mktemp -d)
SOCK=glassosd-shots-$$
cleanup() {
  [ -n "${KWIN_PID:-}" ] && kill "$KWIN_PID" 2>/dev/null || true
  rm -rf "$WORK"
}
trap cleanup EXIT

# ---- a demo config, so one frame shows the whole feature set ---------------
mkdir -p "$WORK/config" "$WORK/data/glassosd/themes"
cp "$ROOT"/themes/*.json "$WORK/data/glassosd/themes/" 2>/dev/null || true

cat > "$WORK/config/glassosdrc" <<'RC'
[Notifications]
Enabled=true
Limit=4
AutoCollapseOver=3
CoalesceThreshold=3

[Appearance]
Widgets=mpris,volume,backlight,dnd,buttons-grid
ButtonsPerRow=7
Scale=1.0
NotifyWidth=380

[Button wifi]
Icon=wifi
Tooltip=Network
Command=true
Order=0
[Button bluetooth]
Icon=bluetooth
Tooltip=Bluetooth
Command=true
Order=1
[Button dnd]
Icon=dnd
Tooltip=Do Not Disturb
Action=toggle-dnd
Order=2
[Button settings]
Icon=settings
Tooltip=Settings
Command=true
Order=3
[Button lock]
Icon=lock
Tooltip=Lock
Command=true
Order=4
[Button reboot]
Icon=reboot
Tooltip=Restart
Command=true
Order=5
[Button power]
Icon=power
Tooltip=Power Off
Command=true
Order=6
RC

# The whole session runs under its own bus, so the nested glassosd owns
# org.glassosd.Daemon and org.freedesktop.Notifications without touching the
# ones on the real session bus.
cat > "$WORK/session.sh" <<INNER
set -euo pipefail
export XDG_CONFIG_HOME="$WORK/config"
export XDG_DATA_HOME="$WORK/data"
export WAYLAND_DISPLAY="$SOCK"
export QT_QPA_PLATFORM=wayland

"$BIN" > "$WORK/osd.log" 2>&1 &
OSD=\$!
sleep 3

ctl() { busctl --user call org.glassosd.Daemon /Control org.glassosd.Control "\$@" >/dev/null 2>&1 || true; }
notify() { gdbus call --session --dest org.freedesktop.Notifications \\
    --object-path /org/freedesktop/Notifications \\
    --method org.freedesktop.Notifications.Notify \\
    "\$1" 0 "\$2" "\$3" "\$4" '[]' '{}' 6000 >/dev/null 2>&1 || true; }

shot() { # name
  sleep 1.2
  if command -v grim >/dev/null; then grim "$ROOT/$OUT/\$1.png"
  else spectacle -b -f -n -o "$ROOT/$OUT/\$1.png" >/dev/null 2>&1; fi
  echo "  captured \$1"
}

# 1. notification popups, including a grouped burst
notify "Fractal" "message-new" "Ada Lovelace" "The analytical engine weaves algebraic patterns."
notify "Thunderbird" "mail-unread" "3 new messages" "Inbox — work"
notify "System" "system-software-update" "Updates available" "14 packages can be upgraded."
shot 01-notifications

# 2. a coalesced burst — one card per app, with a count
for i in 1 2 3 4 5 6 7; do notify "Thunderbird" "mail-unread" "Message \$i" "Sender \$i"; sleep 0.15; done
shot 02-grouped

# 3. the notification centre with every widget on
ctl ShowHistory
shot 03-centre

# 4. do not disturb on
ctl SetDoNotDisturb b true
shot 04-dnd
ctl SetDoNotDisturb b false
ctl HideHistory

# 5. the OSD
ctl ShowProgress sii "volume-high" 65 100
shot 05-osd-volume

ctl ShowText ss "lock-caps" "Caps Lock On"
shot 06-osd-caps

kill \$OSD 2>/dev/null || true
INNER
chmod +x "$WORK/session.sh"

# ---- nested compositor ----------------------------------------------------
kwin_wayland --width 1200 --height 1400 --socket="$SOCK" \
             --no-lockscreen --no-global-shortcuts \
             -- dbus-run-session -- bash "$WORK/session.sh" \
             > "$WORK/kwin.log" 2>&1 &
KWIN_PID=$!

for _ in $(seq 1 60); do
  [ -S "${XDG_RUNTIME_DIR}/$SOCK" ] && break
  sleep 0.25
done
[ -S "${XDG_RUNTIME_DIR}/$SOCK" ] || {
  echo "nested compositor did not start:" >&2; cat "$WORK/kwin.log" >&2; exit 1; }

echo "shooting in an isolated session (socket $SOCK)…"
wait "$KWIN_PID" 2>/dev/null || true

echo
echo "written to $ROOT/$OUT:"
ls -1 "$ROOT/$OUT"/*.png 2>/dev/null | sed 's|.*/|  |' || echo "  (nothing — see $WORK)"
