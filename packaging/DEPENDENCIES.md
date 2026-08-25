# glassosd — dependencies and packaging

## Runtime (what a clean Fedora install needs)

Direct, first-order packages. Everything else in `ldd` output is pulled in
transitively by these and does not need naming.

```
qt6-qtbase qt6-qtbase-gui qt6-qtdeclarative
kf6-kconfig kf6-kguiaddons kf6-ki18n kf6-kwindowsystem
kf6-kglobalaccel kf6-kstatusnotifieritem kf6-kidletime
layer-shell-qt
dbus-libs
```

Plus, not linked but required at runtime:

```
papirus-icon-theme     # third-party app icons; Breeze does not cover them
libkscreen             # kscreen-doctor, for the Meta+P switcher (Phase 5)
```

Note `Papirus-Dark` on Fedora inherits `breeze-dark`, **not** `Papirus`, so
plain `papirus-icon-theme` is the one that matters — see the Phase 2 notes.

## Build-only

```
cmake gcc-c++ extra-cmake-modules
qt6-qtbase-devel qt6-qtdeclarative-devel
kf6-kconfig-devel kf6-kguiaddons-devel kf6-ki18n-devel kf6-kwindowsystem-devel
kf6-kglobalaccel-devel kf6-kstatusnotifieritem-devel kf6-kidletime-devel
layer-shell-qt-devel dbus-devel
```

## What was trimmed

- **`KF6::CoreAddons`** — declared and linked but never used. Dropped.
- **`kf6-knotifications-devel`, `kf6-kirigami-devel`, `kf6-kiconthemes-devel`,
  `kf6-kdbusaddons-devel`** were installed early on and are **not used**:
  - KNotification is client-side only and cannot serve notifications
  - Kirigami was avoided deliberately — a custom icon set gave a consistent
    shape language instead
  - Icon lookup uses plain `QIcon::fromTheme`
  They can be removed unless something else on the system wants them.

## Why each remaining one is load-bearing

| Package | Used for | Replaceable? |
|---|---|---|
| `qt6-qtdeclarative` | QML for every surface | no |
| `layer-shell-qt` | placing surfaces as layer shells | no |
| `kf6-kwindowsystem` | `KWindowEffects` blur/contrast, XDG activation tokens | no — this *is* the glass |
| `kf6-kguiaddons` | `KModifierKeyInfo` caps/num lock over `org_kde_kwin_keystate` | only by reading `/sys` LEDs and polling |
| `kf6-kidletime` | idle detection via `ext_idle_notification_v1` | hand-rolled Wayland protocol binding |
| `kf6-kglobalaccel` | Meta+N / Meta+Shift+N | no equivalent on KDE |
| `kf6-kstatusnotifieritem` | tray entry | hand-rolled SNI D-Bus |
| `kf6-kconfig` | `glassosdrc`, live reload via `KConfigWatcher` | plain files, losing live reload |
| `kf6-ki18n` | translated OSD strings | drop translations |
| `dbus-libs` | `BecomeMonitor` on `org.kde.osdService` | **not optional** — QtDBus has no monitor API |

`Qt6::Widgets` is pulled in for exactly one thing: `QMenu`, the tray context
menu. Dropping the context menu would remove that dependency, at the cost of
losing right-click actions on the tray icon.

## Bundling

Binary is ~2.2 MB in Release. Three sensible routes:

1. **RPM (recommended on Fedora).** Everything above is already packaged in
   Fedora proper — nothing needs vendoring, no COPR required. A spec file with
   the `BuildRequires`/`Requires` lists above is the whole job.
2. **Flatpak.** Would need the KDE runtime and `layer-shell-qt` built as a
   module. Bigger, and a sandboxed notification daemon is awkward: it must own
   a well-known bus name and read `/sys` for Fn lock.
3. **Static/AppImage.** Not worth it. The Qt and KF6 libraries dwarf our 2.2 MB
   and the daemon is inherently tied to this desktop.

The reinstall bootstrap script only needs: the two `dnf install` lines, a
build-and-install step, the three config edits (`plasmarc [OSD] Enabled=false`,
Meta+P rebind, systemd unit), and the D-Bus activation override.
