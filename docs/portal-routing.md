<!--
SPDX-FileCopyrightText: 2026 glassosd contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Sandboxed apps and the portal problem

**This catches everyone, and it is not specific to glassosd** — it affects
dunst and swaync identically.

Flatpaks and other sandboxed apps do not talk to `org.freedesktop.Notifications`
directly. They go through `xdg-desktop-portal`, which hands off to a *backend*.
On KDE that backend is `plasmanotify`, which delivers straight into
plasmashell's own notification system. Those notifications **never reach the
notification daemon at all** — they appear in Plasma's popups instead of yours,
and nothing in your daemon's log will mention them.

Check which backend you have:

```bash
grep -r "impl.portal.Notification" /usr/share/xdg-desktop-portal/ ~/.config/xdg-desktop-portal/ 2>/dev/null
```

If it says `plasmanotify`, route it through the gtk backend, which forwards to
whichever daemon owns the name:

```bash
sudo dnf install xdg-desktop-portal-gtk      # if not already installed
mkdir -p ~/.config/xdg-desktop-portal
cat > ~/.config/xdg-desktop-portal/portals.conf <<'EOF'
[preferred]
default=kde
org.freedesktop.impl.portal.Notification=gtk
EOF
cp ~/.config/xdg-desktop-portal/portals.conf ~/.config/xdg-desktop-portal/kde-portals.conf
systemctl --user restart xdg-desktop-portal.service
```

Restart any running Flatpaks afterwards. Verify it took:

```bash
journalctl --user -u xdg-desktop-portal -n 50 | grep -i notification
#  Using gtk.portal for org.freedesktop.impl.portal.Notification
```

`glassosd-setup` detects and offers to do this for you.

To diagnose a specific app end to end — which daemon owns the name, which
portal backend is live, whether the app's sandbox blocks the bus, and whether
anything actually arrives while you trigger it:

```bash
verify-routing.sh me.proton.Mail
```

### Apps that draw their own notifications

Separately, an app whose sandbox blocks the notification bus *and* whose
toolkit predates portal support will render its own notification window —
usually unstyled, in a corner you did not choose. Proton Mail is a common
example.

```bash
flatpak info --show-permissions <app.id> | grep -A3 "Session Bus"
```

No `org.freedesktop.Notifications=talk` there means the app cannot reach any
daemon. libnotify 0.8+ uses the portal automatically and needs no permission,
so fix the portal routing above first. If the app still draws its own, grant it
directly — note this does widen that app's sandbox:

```bash
flatpak override --user --talk-name=org.freedesktop.Notifications <app.id>
```

Undo with `flatpak override --user --reset <app.id>`. Either way, **fully quit
and restart the app** — Electron and Chromium probe for a notification server
once at startup and cache the answer for the process lifetime.
