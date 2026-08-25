<!--
SPDX-FileCopyrightText: 2026 glassosd contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Contributing

## Themes

The easiest contribution, and the most useful. A theme is one JSON file —
see [Theming](README.md#theming) for every key.

```bash
glassosdctl edit-theme        # copies the current theme locally and opens it
```

Drop it in `themes/` and open a PR. Include a screenshot taken on a plain
background. Themes are CC0-1.0 so anyone can build on them.

## Code

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
systemctl --user stop glassosd     # free the bus name
./build/glassosd
```

`tools/stress-test.sh` fires the awkward cases: empty bodies, 2000-character
bodies, unwrappable single words, RTL, markup abuse, missing icons, zero and
negative timeouts, and a 60-notification flood. Run it before opening a PR;
the daemon should survive with no restarts and no warnings.

`tools/demo-notifications.sh` is the gentler version for eyeballing layout.

### House rules

- **Every file needs an SPDX header.** CI enforces this; `REUSE.toml` covers
  files that cannot carry a comment.
- **`Style.qml` is the only place literals live.** A colour or a radius written
  inline in a component is a bug — route it through `Style`, which routes it
  through `Theme`, which makes it themeable for free.
- **Comments explain why, not what.** Several values in `Style.qml` look
  arbitrary and are not; the comment saying which value was tried and why it
  failed is the point.
- **Anything Plasma-specific must degrade, not fail.** Check at runtime and log
  at info level. glassosd runs on Hyprland and sway too.
- Licence is GPL-2.0-or-later and cannot change — see
  [Licence](README.md#licence) for why.

## Reporting bugs

See the issue templates. The single most common report — "app X's notifications
never arrive" — is usually the
[portal backend](README.md#sandboxed-apps-the-portal-problem) rather than a bug.
