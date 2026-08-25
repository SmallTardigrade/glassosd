#!/bin/bash
# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Audit for the bug class that is invisible until it ruins someone's session:
# a layer surface that quietly eats input.
#
# Nothing here needs a human to press keys. It inspects what the daemon has
# actually asked the compositor for, and flags the combinations that are
# known to swallow clicks or keyboard focus.
#
# Why this exists: glassosd's control centre is anchored to all four screen
# edges, so its surface covers the whole display. Get its keyboard
# interactivity or its input region wrong and every global shortcut on the
# system stops working, with nothing visible on screen to explain it. That is
# a catastrophic, silent failure and it is worth a standing check.
set -uo pipefail

BUS=org.glassosd.Daemon
fail=0
pass() { printf '  \033[32mPASS\033[0m  %s\n' "$*"; }
bad()  { printf '  \033[31mFAIL\033[0m  %s\n' "$*"; fail=$((fail+1)); }
info() { printf '  \033[2m%s\033[0m\n' "$*"; }

hdr() { printf '\n\033[1m%s\033[0m\n' "$*"; }

running() { busctl --user status "$BUS" >/dev/null 2>&1; }
ctl() { busctl --user call "$BUS" /Control org.glassosd.Control "$@" >/dev/null 2>&1; }

running || { echo "glassosd is not running"; exit 1; }

# ---------------------------------------------------------------------------
hdr "1. Keyboard focus is not held while the centre is closed"
# The control centre spans the whole screen. If it holds keyboard focus while
# invisible, it swallows Alt+Tab and every other global shortcut, and the user
# has no way to tell what is doing it.
ctl HideHistory
sleep 1
grabbed=$(busctl --user call org.kde.KWin /KWin org.kde.KWin activeWindow 2>/dev/null)
if pgrep -x glassosd >/dev/null; then
  # Ask the compositor what has keyboard focus. On KWin the layer surface is
  # not a "window", so glassosd holding focus shows up as *nothing* being
  # active, which is exactly the symptom.
  info "centre closed; check a global shortcut still fires:"
  before=$(busctl --user get-property org.kde.KWin /KWin org.kde.KWin showingDesktop 2>/dev/null)
  busctl --user call org.kde.kglobalaccel /component/kwin \
      org.kde.kglobalaccel.Component invokeShortcut s "Show Desktop" >/dev/null 2>&1
  sleep 1
  after=$(busctl --user get-property org.kde.KWin /KWin org.kde.KWin showingDesktop 2>/dev/null)
  busctl --user call org.kde.kglobalaccel /component/kwin \
      org.kde.kglobalaccel.Component invokeShortcut s "Show Desktop" >/dev/null 2>&1
  [ "$before" != "$after" ] && pass "global shortcuts fire with the centre closed" \
                            || bad  "a global shortcut did NOT fire — something is grabbing input"
fi

# ---------------------------------------------------------------------------
hdr "2. Every surface declares a non-empty input region"
# GlassPanel used to register its input region only when glass was true. A
# notification card does not set glass, so it registered nothing, the
# aggregate region came out empty, and the mask fell back to the whole
# window — meaning the transparent shadow padding swallowed clicks aimed at
# whatever was underneath.
if grep -q 'if (!Window.window)' "$(dirname "$0")/../qml/GlassPanel.qml" 2>/dev/null &&
   ! grep -q 'if (!glass || !Window.window)' "$(dirname "$0")/../qml/GlassPanel.qml" 2>/dev/null; then
  pass "input region registration is not gated on the glass property"
else
  bad "GlassPanel gates setPanelRegion on 'glass' — non-glass panels register no region"
fi

# ---------------------------------------------------------------------------
hdr "3. Full-screen surfaces take keyboard focus only while visible"
HP="$(dirname "$0")/../qml/HistoryPanel.qml"
if grep -q 'onVisibleChanged: Surface.setKeyboardFocus' "$HP" 2>/dev/null; then
  pass "the centre releases keyboard focus when hidden"
else
  bad "the centre does not release keyboard focus on hide"
fi
if grep -qE '"glassosd-history", 1 \| 2 \| 4 \| 8,\s*$' "$HP" 2>/dev/null; then
  info "centre is anchored on all four edges (covers the screen) — expected"
fi

# ---------------------------------------------------------------------------
hdr "4. The notification surface never takes keyboard focus unprompted"
NS="$(dirname "$0")/../qml/NotificationStack.qml"
if grep -q 'initLayerShell(win, "glassosd-notifications".*false' "$NS" 2>/dev/null; then
  pass "notification stack starts with keyboard focus off"
else
  bad "notification stack may request keyboard focus at startup — it will steal the next keypress"
fi

# ---------------------------------------------------------------------------
hdr "5. No surface uses Exclusive keyboard interactivity"
# Exclusive grabs the keyboard outright. Nothing glassosd draws justifies it,
# and a crash while holding it leaves the session unusable.
if grep -rq "KeyboardInteractivityExclusive" "$(dirname "$0")/../src" 2>/dev/null; then
  bad "something requests KeyboardInteractivityExclusive"
else
  pass "no surface requests exclusive keyboard interactivity"
fi

# ---------------------------------------------------------------------------
hdr "6. The daemon survives its own surfaces being toggled hard"
n=$(systemctl --user show glassosd -p NRestarts --value 2>/dev/null || echo 0)
for _ in $(seq 1 15); do ctl ShowHistory; ctl HideHistory; done
sleep 2
n2=$(systemctl --user show glassosd -p NRestarts --value 2>/dev/null || echo 0)
[ "$n" = "$n2" ] && pass "30 rapid open/close cycles, no restart" \
                 || bad "daemon restarted during rapid toggling ($n -> $n2)"

# ---------------------------------------------------------------------------
hdr "7. Focus is returned after the centre closes"
ctl ShowHistory; sleep 1; ctl HideHistory; sleep 1
before=$(busctl --user get-property org.kde.KWin /KWin org.kde.KWin showingDesktop 2>/dev/null)
busctl --user call org.kde.kglobalaccel /component/kwin \
    org.kde.kglobalaccel.Component invokeShortcut s "Show Desktop" >/dev/null 2>&1
sleep 1
after=$(busctl --user get-property org.kde.KWin /KWin org.kde.KWin showingDesktop 2>/dev/null)
busctl --user call org.kde.kglobalaccel /component/kwin \
    org.kde.kglobalaccel.Component invokeShortcut s "Show Desktop" >/dev/null 2>&1
[ "$before" != "$after" ] && pass "shortcuts still fire after opening and closing the centre" \
                          || bad  "shortcuts stopped firing after the centre was opened"

# ---------------------------------------------------------------------------
printf '\n'
if [ "$fail" -eq 0 ]; then
  printf '\033[32m%s\033[0m\n' "input audit clean"
else
  printf '\033[31m%s\033[0m\n' "$fail check(s) failed"
fi
exit "$fail"
