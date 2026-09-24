<!--
SPDX-FileCopyrightText: 2026 glassosd contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Sandboxed apps and the portal

Flatpaks and other sandboxed apps cannot talk to `org.freedesktop.Notifications`
— the name is outside their sandbox. They go through `xdg-desktop-portal`,
which validates the request and hands it to a *backend*.

There is no fallback. If no backend implements
`org.freedesktop.impl.portal.Notification`, the portal exports no notification
interface at all, and a sandboxed app's notifications go nowhere. It never
looks for a daemon on its own, however correct that daemon is.

**glassosd implements that backend**, so the fix is to point the portal at it.

## Check what you have now

```bash
grep -r "impl.portal.Notification" /usr/share/xdg-desktop-portal/ /etc/xdg-desktop-portal/ ~/.config/xdg-desktop-portal/ 2>/dev/null
```

`plasmanotify` is the KDE default and the usual cause of trouble. That name is
registered by plasmashell itself and ships no D-Bus service file, so if
plasmashell is not serving notifications, every portal notification fails
outright:

```
org.freedesktop.DBus.Error.ServiceUnknown: The name is not activatable
```

`gtk` works, but it is a relay: it translates the portal's format down to the
older freedesktop one on the way through, and reports itself as version 1. The
portal then strips `sound`, `category`, `markup-body`, every display hint and
every button purpose before they reach your daemon. `priority: high` and
`priority: normal` arrive identical.

## Point it at glassosd

`glassosd-setup` detects the situation and offers to do this. By hand:

```bash
mkdir -p ~/.config/xdg-desktop-portal
cat > ~/.config/xdg-desktop-portal/portals.conf <<'EOF'
[preferred]
default=kde
org.freedesktop.impl.portal.Notification=glassosd
EOF
cp ~/.config/xdg-desktop-portal/portals.conf ~/.config/xdg-desktop-portal/kde-portals.conf
systemctl --user restart glassosd.service
systemctl --user restart xdg-desktop-portal.service
```

Restart glassosd **before** the portal. The portal reads the backend's
interface version once, while constructing its proxy, and never looks again —
if glassosd is not on the bus at that moment it is treated as version 1 for
the life of the portal process, and you get the stripping described above. A
packaged install avoids the race with a D-Bus service file that lets the
portal start glassosd itself; the ordering above only matters when restarting
by hand.

Restart any running Flatpaks afterwards — Electron and Chromium probe once at
startup and cache the answer.

## Verify it took

```bash
busctl --user get-property org.freedesktop.portal.Desktop \
    /org/freedesktop/portal/desktop org.freedesktop.portal.Notification version
```

`u 2` means glassosd is serving it and nothing is being stripped. `u 1` means
either another backend is in use or glassosd lost the startup race above.

```bash
busctl --user get-property org.freedesktop.portal.Desktop \
    /org/freedesktop/portal/desktop org.freedesktop.portal.Notification SupportedOptions
```

That lists what glassosd advertises to applications. Empty means a version 1
backend is answering.

Send one straight through the portal:

```bash
gdbus call --session --dest org.freedesktop.portal.Desktop \
    --object-path /org/freedesktop/portal/desktop \
    --method org.freedesktop.portal.Notification.AddNotification \
    "test" "{'title': <'Portal'>, 'body': <'arrived'>}"
```

To diagnose a specific app end to end — which daemon owns the name, which
portal backend is live, whether the app's sandbox blocks the bus, and whether
anything actually arrives while you trigger it:

```bash
verify-routing.sh me.proton.Mail
```

## What the portal path gives you

The app id glassosd receives on this path is derived by the portal from the
sandbox rather than claimed by the sender, so a `desktop-entry` rule matching
a Flatpak cannot be fooled by another app naming itself the same thing. It is
the one route where that identity is a fact rather than a claim.

Not everything survives the crossing in the other direction: the portal format
has no equivalent of the `value` progress hint or of stack tags, so Flatpaks
get no progress bars and no replace-by-tag. Both still work for apps that use
`org.freedesktop.Notifications` directly.

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
