#!/bin/bash
# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Walks through every notification behaviour glassosd implements.
# Re-run any time:  ~/Documents/KDE\ Upgrades/glassosd/tools/demo-notifications.sh
say() { echo; echo "=== $1"; }

say "1/6  Three ordinary notifications — they stack, newest at the top"
notify-send -a "Mail" "Inbox" "A single ordinary notification." -t 6000; sleep 0.6
notify-send -a "Calendar" "Tomorrow 09:00" "Design review with the team." -t 6000; sleep 0.6
notify-send -a "Updates" "12 packages can be updated" "Security updates included." -t 6000
sleep 5

say "2/6  Rate limiting — 8 at once, only 3 show plus a '+N more' row"
for i in $(seq 1 8); do
  notify-send -a "Bulk$i" "Queued item $i" "Body for item number $i." -t 9000
  sleep 0.1
done
sleep 8

say "3/6  Coalescing — 10 from ONE app collapse into a single card"
for i in $(seq 1 10); do
  notify-send -a "Proton Mail" -h string:desktop-entry:protonmail \
    "New message $i" "Sender $i — subject line number $i" -t 12000
  sleep 0.12
done
sleep 9

say "4/6  Replace-by-tag rule — battery %, different text each time, one card"
for p in 12 9 6 3; do
  notify-send -a "Power Management" "Device Battery Low ($p% Remaining)" \
    "Wacom pen battery is low." -t 9000
  sleep 0.8
done
sleep 6

say "5/6  Action buttons (backgrounded: notify-send -A blocks until clicked)"
setsid nohup notify-send -a "Meetings" "Standup in 5 minutes" "Daily sync." \
  -A "join=Join" -A "later=Remind me later" -t 12000 >/dev/null 2>&1 &
sleep 8

say "6/6  Urgency — low fades fast, critical stays until dismissed"
notify-send -a "Chat" -u low "Low urgency" "Disappears after ~4s." -t 4000
sleep 0.4
notify-send -a "System" -u critical "Critical urgency" "Stays put until you dismiss it." 
echo
echo "Done. Press Meta+N to see all of it in the history panel."
