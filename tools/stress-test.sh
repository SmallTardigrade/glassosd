#!/bin/bash
# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Adversarial notification tests: malformed input, extremes, and edge cases.
n() { notify-send "$@"; sleep 0.08; }

echo "1. empty summary and body (spec says drop it)"
n -a "Empty" "" ""
echo "2. very long single-word summary (no wrap opportunity)"
n -a "LongWord" "Supercalifragilisticexpialidociousandthensomemoretexthatneverends" "body"
echo "3. 2000-character body"
n -a "Wall" "Wall of text" "$(python3 -c 'print("lorem ipsum dolor sit amet "*75)')"
echo "4. unicode, emoji, CJK, RTL"
n -a "Unicode" "🎉 Émoji ünicode 中文 العربية" "Mixed: 🚀 日本語 עברית ñ ü ß → ∑ ∆"
echo "5. markup abuse"
n -a "Markup" "<b>bold</b> <script>alert(1)</script>" "<html><tt>x</tt><i>i</i><u>u</u><blink>no</blink>"
echo "6. nonexistent icon name"
n -a "BadIcon" -i "this-icon-does-not-exist-anywhere" "Missing icon" "Should fall back gracefully"
echo "7. very long app name"
n -a "ThisApplicationNameIsAbsurdlyLongAndShouldElide" "Long app name" "body"
echo "8. negative and zero timeouts"
notify-send -a "Timeout" "Never expires (0)" "Should stay until dismissed" -t 0; sleep 0.08
notify-send -a "Timeout" "Server default (-1)" "Should use urgency default" -t -1; sleep 0.08
echo "9. all urgencies"
for u in low normal critical; do n -a "Urgency-$u" -u $u "Urgency $u" "body"; done
echo "10. rapid replaces_id churn (progress-style)"
for i in $(seq 0 10 100); do
  notify-send -a "Progress" -h int:value:$i "Copying files" "$i% complete" -t 8000
  sleep 0.12
done
echo "11. 60-notification flood from 6 apps"
for i in $(seq 1 60); do
  notify-send -a "Flood$((i % 6))" "Flood item $i" "Body $i" -t 12000
  sleep 0.03
done
echo "done"

# The suite deliberately sends notifications with expire_timeout 0 and
# critical urgency, both of which the spec requires to stay until acted on.
# Leaving them pinned on screen after a test run is just litter.
sleep 2
busctl --user call org.glassosd.Daemon /Control org.glassosd.Control DismissAll >/dev/null 2>&1 || true
echo "stress test done; popups cleared"
