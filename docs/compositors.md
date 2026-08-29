<!--
SPDX-FileCopyrightText: 2026 glassosd contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Per-compositor setup

glassosd needs `wlr-layer-shell-unstable-v1`. Plasma integration is detected
at runtime and never required at build time; where a feature has no portable
equivalent there is a CLI way in. See the table in the
[README](../README.md#compositor-support).

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
