<!--
SPDX-FileCopyrightText: 2026 glassosd contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->
# Troubleshooting

**Nothing appears at all.**

```bash
systemctl --user status glassosd
journalctl --user -u glassosd -n 50
```

Check something else has not taken the bus name:

```bash
glassosdctl status     # "notifications: owned by …"
```

**Some apps' notifications never arrive**, and nothing appears in the log.
Almost always the portal backend. See
[Sandboxed apps and the portal problem](portal-routing.md).

**Two sets of notifications.** Another daemon is still running, or Plasma's own
OSD is still on. See [Setup](../README.md#setup).

**Surfaces are transparent and unreadable.** Your compositor is not blurring.
Raise `glassosdctl solidity 0.8`, or use an opaque theme.

**The theme file does nothing.** Watch the log while you save it — a JSON error
is reported with its byte offset:

```bash
journalctl --user -u glassosd -f
```

**Popups vanish while I am reading them.**

```bash
glassosdctl set HoverPause true
```

**Caps Lock OSD never appears off KDE.** Expected. `org_kde_kwin_keystate` is
KWin-only and has no portable equivalent.

**Global shortcuts do nothing off KDE.** Expected — bind `glassosdctl` in your
compositor config. See [per-compositor setup](compositors.md).
