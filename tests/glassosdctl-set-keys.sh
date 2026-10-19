#!/bin/sh
# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# `glassosdctl set` warns about keys it does not recognise. Its list fell
# behind the daemon once already (Portal, LockscreenPrivacy, QuietWhileBusy,
# SnoozeMinutes and Focus all drew a false warning), so this checks it against
# the man page: every key documented in the Notifications group must be
# accepted without one and listed in the help, and a misspelt key must still
# get the warning.
#
# usage: glassosdctl-set-keys.sh <glassosdctl> <glassosdrc.5>
set -eu
ctl=$1 man=$2

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export XDG_CONFIG_HOME="$tmp"
# kwriteconfig6 --notify would otherwise tell the desktop's running glassosd
# that its config changed.
export DBUS_SESSION_BUS_ADDRESS="unix:path=$tmp/no-bus"

# The item headings (a .B straight after .TP) between the Notifications
# heading and the next .SH, with "Timeout ", " TimeoutLow" style lists split
# into separate keys. Focus is read from the same group but documented in a
# section of its own.
keys=$(awk '/^\.SH THE NOTIFICATIONS GROUP/{ing=1;next} /^\.SH/{ing=0}
            ing && prev==".TP" && /^\.B /{sub(/^\.B /,""); gsub(/[",]/," "); print}
            {prev=$0}' "$man" | tr -s ' ' '\n' | grep .)
keys="$keys
Focus"

[ -n "$keys" ] || { echo "no keys found in $man" >&2; exit 1; }

# The usage text lists the same keys; it drifted with the set list.
listed=$("$ctl" help 2>&1 | sed -n '/^Settings keys:/,/^$/p')

fail=0
for k in $keys; do
  if ! printf '%s\n' "$listed" | grep -qw -- "$k"; then
    echo "FAIL: documented key '$k' is missing from the help's Settings keys" >&2
    fail=1
  fi
  if "$ctl" set "$k" x 2>&1 >/dev/null | grep -q "is not a setting"; then
    echo "FAIL: documented key '$k' draws a warning" >&2
    fail=1
  fi
done
if ! "$ctl" set Limitt 5 2>&1 >/dev/null | grep -q "is not a setting"; then
  echo "FAIL: misspelt key 'Limitt' was accepted silently" >&2
  fail=1
fi
[ $fail -eq 0 ] && echo "$(echo "$keys" | wc -l) documented keys accepted"
exit $fail
