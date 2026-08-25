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
  - [The portal problem](#sandboxed-apps-the-portal-problem)
- [Configuration](#configuration)
- [Theming](#theming)
- [Widgets](#widgets)
- [Compositor support](#compositor-support)
- [Command reference](#command-reference)
- [Troubleshooting](#troubleshooting)
- [Reporting bugs](#reporting-bugs)
- [Building](#building)
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

## Install

### Fedora

```bash
sudo dnf copr enable smalltardigrade/glassosd
sudo dnf install glassosd
```

### Arch

Every dependency is in `extra`. A PKGBUILD sketch is in
[packaging/DEPENDENCIES.md](packaging/DEPENDENCIES.md).

### From source

See [Building](#building).

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

### Sandboxed apps: the portal problem

**This one catches everyone, and it is not specific to glassosd** — it affects
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

---

## Configuration

Everything lives in `~/.config/glassosdrc`. Every key has a CLI equivalent
that applies live, with no restart:

```bash
glassosdctl status              # what is running, and every current setting
glassosdctl set Limit 5         # popups on screen at once
glassosdctl set HoverPause true # hold a popup while the pointer is on it
glassosdctl accent '#d95f02'    # auto-darkened if it would fail contrast
glassosdctl scale 1.2           # everything, proportionally
glassosdctl level bar           # or segmented
glassosdctl test                # fire the demo sequence
```

### Modules

Each half runs independently. Off means never constructed — no bus name, no
Wayland surface, no watchers.

```bash
glassosdctl module                     # show all four
glassosdctl module osd off             # notifications only
glassosdctl module notifications off   # OSD only
glassosdctl restart                    # modules are read at startup
```

| Module | What it covers |
|---|---|
| `notifications` | the daemon and its popups |
| `centre` | the control centre and tray icon |
| `osd` | volume, brightness, media OSD |
| `lockkeys` | caps, num and Fn lock OSDs |

### Per-app rules

```bash
glassosdctl rule-set signal appname=Signal set_stack_tag=chat timeout=8000
glassosdctl rule-set noisy   appname=Steam skip_display=true
glassosdctl rules
```

Rule keys: `appname`, `summary`, `body`, `category`, `desktop_entry`,
`match_urgency`, `set_stack_tag`, `timeout`, `set_urgency`, `skip_display`,
`history_ignore`, `always_collapsed`.

The same rules are what the notification centre's per-app settings panel
writes — click the sliders icon on any group header. It offers four outcomes
rather than the raw keys:

| | What it sets |
|---|---|
| **Mute** | `skip_display` — no popup, still kept in history |
| **Ignore** | `history_ignore` + `skip_display` — dropped entirely |
| **Never expire** | `timeout=0` — popups stay until dismissed |
| **Always collapsed** | `always_collapsed` — the group opens folded |

Ignore holds Mute on for as long as it is set, because a notification kept
nowhere but still flashing on screen is not what "ignore" means. Turning Ignore
off clears both; set Mute again if that was what you wanted.

---

## Theming

**glassosd does not parse CSS.** Its surfaces are QML, which has no CSS engine,
and pretending otherwise would mean shipping a fake one. What it has instead is
a JSON theme file that covers the part of a swaync `style.css` people actually
edit — the `@define-color` block, the radii, the shadows and the font.

```bash
glassosdctl themes             # list what is installed and where
glassosdctl theme-file nord    # switch
glassosdctl edit-theme         # copy the current theme to ~/.config and open it
```

Themes live in `~/.config/glassosd/themes/NAME.json`, which overrides
`~/.local/share/glassosd/themes/`, which overrides `/usr/share/glassosd/themes/`.
Saving the file re-themes the running daemon — no restart, no reload command.

### What the JSON actually changes

Every value below is looked up in your theme first and falls back to the
built-in, so a theme file **only needs the keys you want to change**. A
two-line theme is a valid theme.

**Colours** — accept `#rgb`, `#rrggbb`, `#rrggbbaa`, or `rgba(r, g, b, a)`
exactly as written in a swaync stylesheet.

| Key | What it paints |
|---|---|
| `accent` | progress fill, active toggles, focus, the lock-on chip |
| `text.primary` | notification summaries, headings |
| `text.muted` | the app-name line on a card |
| `text.secondary` | body copy, timestamps, "no notifications" |
| `osd.background` | the volume/brightness popup surface |
| `panel.background` | the control centre sheet |
| `card.background` | notification popup surface |
| `entry.background` | rows inside the control centre |
| `entry.backgroundHover` | those rows, hovered |
| `entry.edge` | the hairline separating adjacent rows |
| `surface.solid` | tooltips and other fully-opaque surfaces |
| `glass.edge` | the lit hairline around a glass surface |
| `glass.sheen` | the top-edge highlight on glass |
| `card.sheen` | the same, on a solid card |
| `edge.outer` | the outer border on non-glass surfaces |
| `control.fill` / `control.fillHover` / `control.edge` | buttons, toggles, grid cells |
| `slider.track` | the unfilled part of a slider or progress bar |
| `chip.idle` | an icon container in its inactive state |
| `card.stackEdge` / `card.stackSeam` | the peeking edges of a grouped stack |
| `urgency.critical` | the critical-urgency accent |
| `shadow.color` | drop-shadow colour |

**Numbers**

| Key | Default | Effect |
|---|---|---|
| `radius.card` | 16 | corner radius on cards and popups |
| `radius.chipRatio` | 0.32 | icon-container roundness, as a fraction of its size (0.5 = circle) |
| `radius.pill` | true | whether small controls are fully rounded |
| `edge.width` | 1 | border thickness |
| `spacing.padding` | 12 | padding inside surfaces |
| `spacing.gap` | 13 | gap between elements |
| `spacing.cardGap` | 8 | gap between stacked popups |
| `spacing.screenMargin` | 10 | distance from the screen edge |
| `spacing.osdTopMargin` | 118 | how far down the OSD sits |
| `size.icon` / `size.notifyIcon` | 21 / 30 | icon sizes |
| `size.centreWidth` | 400 | control centre width |
| `level.segments` | 16 | blocks in the segmented level indicator |
| `shadow.opacity` | 0.48 | shadow strength |
| `shadow.blur` | 40 | shadow softness |
| `shadow.offsetY` | 8 | shadow drop distance |
| `blur.saturation` | 1.8 | backdrop saturation (1.0 = off) |
| `blur.contrast` / `blur.intensity` | 0.32 / 0.92 | backdrop contrast, KWin only |
| `motion.in` / `motion.out` | 150 / 200 | animation durations, ms |
| `font.family` | Noto Sans | — |
| `font.size` | 11 | base point size |

### A minimal theme

```json
{
  "accent": "#ff7043",
  "card.background": "#1a1a1a",
  "radius.card": 4
}
```

### Referencing other keys

A value starting with `@` resolves to another key, the way `@define-color`
values get reused in a stylesheet:

```json
{
  "accent": "#88c0d0",
  "control.edge": "@glass.edge",
  "slider.track": "@control.fill"
}
```

### Making it not look like glass

Set opaque surfaces and turn the sheen off. `material-dark.json` and
`nord.json` in [`themes/`](themes/) both do this and are worth reading as
worked examples:

```json
{
  "card.background": "#211f26",
  "panel.background": "#1d1b20",
  "glass.sheen": "rgba(0, 0, 0, 0)",
  "card.sheen": "rgba(0, 0, 0, 0)",
  "blur.saturation": 1.0
}
```

There is also a one-shot escape hatch that needs no theme file. `Solidity`
pushes every glass surface toward opaque, which is what you want on a
compositor that does not blur:

```bash
glassosdctl solidity 1.0    # 0 keeps the tuned glass, 1 is fully solid
```

### If a theme does not load

A malformed file is reported with the exact byte offset, and the daemon falls
back to the next theme in the search path rather than starting unstyled:

```
glassosd: ~/.config/glassosd/themes/mine.json is not valid JSON at offset 1005:
          garbage at the end of the document
```

```bash
journalctl --user -u glassosd -f
```

---

## Widgets

The control centre's contents are a list, using swaync's widget names so a
config you are migrating reads the same way.

```bash
glassosdctl widgets "mpris,volume,backlight,dnd,buttons-grid"
```

| Widget | What it is |
|---|---|
| `mpris` | media player: art, title, artist, transport controls |
| `volume` | volume slider, click the icon to mute |
| `backlight` | brightness slider (needs `brightnessctl`) |
| `dnd` | Do Not Disturb toggle row |
| `buttons-grid` | the quick-action grid |

Order in the list is the order on screen. Anything omitted is not built.

### The button grid

Buttons are `[Button <name>]` groups in `glassosdrc`, matching the shape of the
existing rule groups:

```ini
[Button wifi]
Icon=wifi
Tooltip=Network
Command=kitty -e nmtui
Order=0

[Button dnd]
Icon=dnd
Tooltip=Do Not Disturb
Action=toggle-dnd
Order=2
```

`Command` runs any shell command. `Action` is one of the built-ins, which
need no external program and — in the case of `toggle-dnd` — render as a
toggle that reflects live state:

`toggle-dnd`, `clear-all`, `lock`, `reboot`, `poweroff`, `logout`

Bundled icon names: `wifi`, `bluetooth`, `dnd`, `settings`, `lock`, `power`,
`reboot`, `media`, `brightness`, `volume-high`, `volume-muted`, `mic-on`,
`mic-muted`. Any icon from your icon theme also works. Set `Label` instead of
`Icon` for text or a Nerd Font glyph.

```bash
glassosdctl set ButtonsPerRow 7
```

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

### Hyprland

Blur is applied by the compositor, matched on layer namespace. glassosd uses
three: `glassosd-osd`, `glassosd-notifications`, `glassosd-history`.

```
layerrule = blur on, ignore_alpha 0.2, match:namespace glassosd-osd
layerrule = blur on, ignore_alpha 0.2, match:namespace glassosd-notifications
layerrule = blur on, ignore_alpha 0.2, match:namespace glassosd-history

exec-once = glassosd

bind = SUPER, N, exec, glassosdctl history
bind = SUPER SHIFT, N, exec, glassosdctl dnd toggle

bindl = , XF86AudioRaiseVolume, exec, glassosdctl osd volume raise
bindl = , XF86AudioLowerVolume, exec, glassosdctl osd volume lower
bindl = , XF86AudioMute,        exec, glassosdctl osd volume mute
bindl = , XF86AudioMicMute,     exec, glassosdctl osd mic mute
bindl = , XF86MonBrightnessUp,   exec, glassosdctl osd brightness raise
bindl = , XF86MonBrightnessDown, exec, glassosdctl osd brightness lower
```

### sway

```
exec glassosd

bindsym $mod+n exec glassosdctl history
bindsym $mod+Shift+n exec glassosdctl dnd toggle

bindsym --locked XF86AudioRaiseVolume exec glassosdctl osd volume raise
bindsym --locked XF86AudioLowerVolume exec glassosdctl osd volume lower
bindsym --locked XF86AudioMute        exec glassosdctl osd volume mute
bindsym --locked XF86MonBrightnessUp   exec glassosdctl osd brightness raise
bindsym --locked XF86MonBrightnessDown exec glassosdctl osd brightness lower
```

sway has no compositor-side blur, and unblurred translucency over a photo
wallpaper is genuinely hard to read. Either raise `solidity` or use one of the
opaque themes:

```bash
glassosdctl theme-file nord
```

---

## Command reference

```
glassosdctl <command>

  status                 daemon state and every current setting
  version                daemon version

  get <key>              read a Notifications setting
  set <key> <value>      write one (applied immediately)

  dnd [on|off|toggle]    Do Not Disturb
  history [show|hide]    open/close the control centre

  osd volume [raise|lower|mute|<0-100>]
  osd mic [mute|<0-100>]
  osd brightness [raise|lower|<0-100>]
  osd text <icon> <text>

  module <name> [on|off] notifications, centre, osd, lockkeys
  rules / rule-set / rule-del

  theme [dark|light]     built-in light/dark palette
  theme-file [name]      load a JSON theme
  themes                 list available themes
  edit-theme             copy the current theme locally and open it
  accent [#rrggbb]
  level [segmented|bar]
  widgets [list]
  output [current|primary|NAME]
  scale [0.6-2.0]
  width [240-900]
  solidity [0.0-1.0]

  test                   fire the demo sequence
  restart / log
```

Settings keys: `Enabled`, `Limit`, `IndicateHidden`, `CoalesceThreshold`,
`CoalesceWindowMs`, `DoNotDisturb`, `AutoCollapseOver`, `HistoryLength`,
`IdleThresholdMs`, `HoverPause`.

### D-Bus

Everything `glassosdctl` does goes through `org.glassosd.Control` on the
session bus, so it works identically on every compositor:

```bash
busctl --user introspect org.glassosd.Daemon /Control org.glassosd.Control
```

---

## Troubleshooting

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
[Sandboxed apps: the portal problem](#sandboxed-apps-the-portal-problem).

**Two sets of notifications.** Another daemon is still running, or Plasma's own
OSD is still on. See [Setup](#setup).

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
compositor config. See [Compositor support](#compositor-support).

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

## Building

```bash
sudo dnf install cmake gcc-c++ ninja-build extra-cmake-modules dbus-devel \
  qt6-qtbase-devel qt6-qtdeclarative-devel layer-shell-qt-devel \
  kf6-kwindowsystem-devel kf6-kguiaddons-devel kf6-kconfig-devel \
  kf6-ki18n-devel kf6-kglobalaccel-devel kf6-kstatusnotifieritem-devel \
  kf6-kidletime-devel systemd-rpm-macros

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

Package names for Arch, Debian and openSUSE are in
[packaging/DEPENDENCIES.md](packaging/DEPENDENCIES.md). Publishing and
packaging notes are in [packaging/PUBLISHING.md](packaging/PUBLISHING.md).

Optional at runtime: `wireplumber` (or `pulseaudio-utils`) for the volume
widget, `brightnessctl` for the brightness widget, `papirus-icon-theme` for
third-party application icons.

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
[discussions]: https://github.com/SmallTardigrade/glassosd/discussions
