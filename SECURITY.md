<!--
SPDX-FileCopyrightText: 2026 glassosd contributors
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Security policy

## Reporting a vulnerability

Please use GitHub's [private vulnerability reporting][pvr] rather than a public
issue: **Security → Report a vulnerability** on this repository. That opens a
private thread visible only to the maintainer.

Expect a first reply within a week. If a fix is warranted it will be released
before the report is made public, and you will be credited unless you would
rather not be.

## What is in scope

glassosd is a session daemon that owns `org.freedesktop.Notifications` and
renders arbitrary content supplied by any application on the session bus.
The interesting surface is therefore **untrusted input from that bus**:

- Notification summary and body text, including the markup subset
- Raw image data passed through the `image-data` / `icon_data` hints
- Icon paths and `desktop-entry` hints
- Action keys and inline-reply text
- D-Bus message structures on `org.glassosd.Control`
- Theme and config files, which are parsed at startup and on change

Things like a malformed image hint crashing the daemon, a body string escaping
the markup sanitiser, or a crafted D-Bus message causing memory corruption are
all in scope.

## What is not

- **A notification daemon shows what applications send it.** An application
  posting misleading text is that application's problem, not a glassosd
  vulnerability. Notifications are not a trusted display surface.
- Anything requiring an attacker who already has your session bus. Session bus
  access is already sufficient to read your keyring, take screenshots and run
  programs as you.
- `Command=` in a `[Button …]` group running arbitrary shell. That is the
  documented purpose of the field, and the config file is yours.
- Bugs in Qt, KDE Frameworks or your compositor. Please report those upstream.

## Supported versions

The latest release only. glassosd is pre-1.0 and there are no maintenance
branches.

[pvr]: https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability
