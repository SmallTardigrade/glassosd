# glassosd

**A themeable notification daemon, on-screen display and control centre for Wayland.**

Most desktops split these across three programs that look nothing like each
other: a notification daemon, whatever the compositor uses for its volume
popup, and a separate control centre. glassosd draws all three from one
stylesheet, and behaves the way a notification daemon should when the desktop
gets busy.

It ships with a glass look because that is what it was built for. It is not
locked to it — every colour, radius, shadow and font comes from a JSON theme
file, and the repository includes Material, Nord and flat themes with no
translucency at all. See [Theming](#theming).

```bash
glassosdctl theme-file material-dark   # or nord, frosted, rose, glass-dark
```

> **Blur strength is your compositor's setting, not ours.**
> `ext-background-effect-v1` lets a client name the *region* to blur, not how
> hard. On KWin: System Settings → Desktop Effects → Blur, or
> `kwriteconfig6 --file kwinrc --group Effect-blur --key BlurStrength <1-15>`.
> A stronger blur samples a wider area, so over a bright window it pulls that
> brightness in — the surface gets lighter as well as hazier.

---
## Contents

- [What it does](#what-it-does)
- [How it compares](#how-it-compares)
- [Install](#install)
- [Setup](#setup)
- [Compositor support](#compositor-support)
- [Configuring it](#configuring-it)
- [Documentation](#documentation)
- [Reporting bugs](#reporting-bugs)
- [Licence](#licence)

---

## What it does

Three modules, each of which can be turned off independently — you are not
made to take the OSD to get the notification daemon, or the other way round.

### Notifications

A full [freedesktop Desktop Notifications][spec] implementation, plus the KDE
extensions: inline reply, action icons, raw image hints and
`NotificationReplied`. Nothing about the layout is special-cased per
application — buttons, reply fields and progress bars appear because the
sending application published them.

The behaviour that matters once the desktop is actually busy:

| | |
|---|---|
| **Rate limiting** | at most *N* on screen; the rest queue behind a "+N more" indicator |
| **Burst coalescing** | 30 mails arriving at once become one card that says 30 |
| **Replace by tag** | stack tags and `replaces_id` update a card in place |
| **Idle threshold** | popups that appeared while you were away wait for you to come back |
| **Hover to hold** | the dwell timer pauses while the pointer is over a card |
| **Persistent history** | survives a restart, grouped by app, collapsible |
| **Do Not Disturb** | persists across login |
| **Per-app rules** | match on app, summary, body, category or urgency |

### On-screen display

Volume, microphone, brightness, keyboard backlight, caps/num/Fn lock,
touchpad, wifi, bluetooth and power profile — as a continuous bar or as
discrete segments.

### Control centre

Notification history grouped by app, plus optional widgets: an MPRIS media
player, volume and brightness sliders, a Do Not Disturb toggle, and a
configurable quick-action button grid.

---

---

## How it compares

glassosd exists because dunst and swaync each had half of what was wanted.

| | glassosd | dunst | swaync |
|---|---|---|---|
| Rate limiting | ✅ | ✅ | ✗ |
| Burst coalescing (wake-from-suspend) | ✅ | partial¹ | ✗ |
| Idle threshold | ✅ | ✅ | ✗ |
| Persistent history | ✅ | ✗² | ✅ |
| Control centre | ✅ | ✗ | ✅ |
| Media player widget | ✅ | ✗ | ✅ |
| Inline reply | ✅ | ✗ | ✅ |
| App icons | ✅ | ✅ | ✅ |
| Volume / brightness OSD | ✅ | ✗ | ✗³ |
| Lock-key OSD | ✅ (KWin only) | ✗ | ✗ |
| Theming | JSON | config file | CSS |
| X11 | ✗ | ✅ | ✗ |

¹ dunst's `notification_limit` queues rather than merges, so a 30-message
burst still drips through one at a time.
² dunst keeps history in memory for the session only.
³ swayosd is a separate program.

**dunst is still the right choice if you need X11.** glassosd is Wayland only.

---

---

## Install

### Fedora

```bash
sudo dnf copr enable smalltardigrade/glassosd
sudo dnf install glassosd
```

Built for Fedora 43, 44 and Rawhide (x86_64). `glassosd --help` and
`glassosdctl --help` both work without a session, so you can check what you
got before logging in to a desktop.

### Arch

Every dependency is in `extra`. A PKGBUILD sketch is in
[packaging/DEPENDENCIES.md](packaging/DEPENDENCIES.md).

### From source

See [Building](#building).

---

---

## Setup

```bash
glassosd-setup
```

That walks through everything below, asking before each change, and
`glassosd-setup --undo` reverts all of it. If you would rather do it by hand:

```bash
systemctl --user enable --now glassosd.service
```

glassosd is also D-Bus activated, so the next application to post a
notification would start it anyway — but the OSD and the control centre need
it running for the whole session.

**If you already run a notification daemon**, stop it first. Only one program
can own `org.freedesktop.Notifications`:

```bash
systemctl --user disable --now dunst.service       # or swaync.service, mako.service
```

**On Plasma**, turn off the built-in popups so they do not double up:

```bash
kwriteconfig6 --file plasmarc --group OSD --key Enabled false
```

To keep the notification daemon you already have and use glassosd only for the
OSD:

```bash
glassosdctl module notifications off
glassosdctl restart
```

### Sandboxed apps

Flatpaks and other sandboxed applications reach the notification daemon through
`xdg-desktop-portal`, and on KDE the default backend delivers into plasmashell
instead — so those notifications never arrive here, and nothing appears in the
log. This catches everyone and is not specific to glassosd.

`glassosd-setup` detects it and offers to fix it. The full explanation, and how
to diagnose one application end to end, is in
[docs/portal-routing.md](docs/portal-routing.md).

---

## Compositor support

glassosd needs `wlr-layer-shell-unstable-v1`. That covers KDE Plasma,
Hyprland, sway, river, Wayfire, labwc and anything else wlroots-based.

**It does not work on GNOME**, which does not implement layer-shell, or on
X11.

Plasma integration is detected at runtime, never required at build time.
Where a feature has no portable equivalent there is a CLI way in:

| Feature | Plasma | Elsewhere |
|---|---|---|
| Notifications, centre, history, DND, rules | ✅ | ✅ |
| Background blur | ✅ automatic | compositor config, below |
| Volume/brightness OSD | ✅ automatic | route media keys through `glassosdctl osd` |
| Global shortcuts | ✅ registered for you | bind `glassosdctl` in your config |
| Caps/Num Lock OSD | ✅ | ✗ — needs `org_kde_kwin_keystate`, KWin only |
| Tray icon | ✅ | ✅ with any StatusNotifierItem host (waybar) |

Per-compositor configuration — layer rules for blur, and media-key and shortcut
bindings for Hyprland and sway — is in
[docs/compositors.md](docs/compositors.md).

---

## Configuring it

Everything lives in `~/.config/glassosdrc`, and every key has a `glassosdctl`
equivalent that applies live, with no restart.

```bash
glassosdctl status               # what is running, and every current setting
glassosdctl set Limit 5          # popups on screen at once
glassosdctl position bottom-right
glassosdctl theme-file nord
glassosdctl test                 # fire the demo sequence
```

Themes are JSON, not CSS — QML has no CSS engine, and pretending otherwise
would mean shipping a fake one. A theme only needs the keys it changes, so a
two-line theme is a valid theme:

```json
{
  "accent": "#ff7043",
  "card.background": "#1a1a1a",
  "radius.card": 4
}
```

Saving the file re-themes the running daemon. `material-dark.json` and
`nord.json` in [`themes/`](themes/) are worked examples of turning the glass
off entirely.

**The full reference is `man glassosdrc`** — every setting, the rules engine,
focus modes, sounds, widgets, the button grid and all the theme keys.

---

## Documentation

| | |
|---|---|
| `man glassosd` | the daemon and its modules |
| `man glassosdctl` | every command |
| `man glassosdrc` | **configuration, rules, sounds, widgets, themes** |
| [docs/troubleshooting.md](docs/troubleshooting.md) | when something does not work |
| [docs/portal-routing.md](docs/portal-routing.md) | sandboxed apps whose notifications never arrive |
| [docs/compositors.md](docs/compositors.md) | Hyprland and sway setup |
| [CONTRIBUTING.md](CONTRIBUTING.md) | building from source |
| [packaging/DEPENDENCIES.md](packaging/DEPENDENCIES.md) | package names per distribution |

Everything `glassosdctl` does goes through `org.glassosd.Control` on the session
bus, so it works identically on every compositor:

```bash
busctl --user introspect org.glassosd.Daemon /Control org.glassosd.Control
```

---

## Reporting bugs

Please use the [issue tracker][issues]. Include:

```bash
glassosdctl version
glassosdctl status
journalctl --user -u glassosd -n 100 --no-pager
echo "$XDG_CURRENT_DESKTOP / $XDG_SESSION_TYPE"
```

For a rendering problem, a screenshot helps — please check it for anything
private before attaching it.

For a problem with one application's notifications, capture what it actually
sends:

```bash
dbus-monitor --session "interface='org.freedesktop.Notifications'"
```

Feature ideas and questions are better in [Discussions][discussions] than the
issue tracker.

---

---

## Building

See [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Licence

GPL-2.0-or-later.

This is not a free choice. `src/osdmodel.cpp` is a port of `shell/osd.cpp`
from [plasma-workspace][pw], which is GPL-2.0-or-later; the original authors'
copyright is carried in that file's header, and everything derived from it
inherits those terms.

The icon set in `qml/icons/` and the bundled themes are CC0-1.0, so they can be
lifted into a bar, a theme or another shell without inheriting the GPL.

The project is [REUSE][reuse] compliant — every file carries an SPDX header or
is covered by `REUSE.toml`.

---

*Developed with [Claude](https://claude.com/claude-code).*

[spec]: https://specifications.freedesktop.org/notification-spec/latest/
[pw]: https://invent.kde.org/plasma/plasma-workspace
[reuse]: https://reuse.software/
[issues]: https://github.com/SmallTardigrade/glassosd/issues
