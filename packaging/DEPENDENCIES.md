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

## Other distributions

Everything glassosd needs is a normal KDE Frameworks 6 / Qt 6 package, so no
distribution needs anything vendored. Package names differ:

| | Fedora | Arch | Debian/Ubuntu | openSUSE |
|---|---|---|---|---|
| layer-shell-qt | `layer-shell-qt-devel` | `layer-shell-qt` | `liblayershellqtinterface-dev` | `layer-shell-qt6-devel` |
| Qt base | `qt6-qtbase-devel` | `qt6-base` | `qt6-base-dev` | `qt6-base-devel` |
| Qt declarative | `qt6-qtdeclarative-devel` | `qt6-declarative` | `qt6-declarative-dev` | `qt6-declarative-devel` |
| ECM | `extra-cmake-modules` | `extra-cmake-modules` | `extra-cmake-modules` | `extra-cmake-modules` |
| KWindowSystem | `kf6-kwindowsystem-devel` | `kwindowsystem` | `libkf6windowsystem-dev` | `kf6-kwindowsystem-devel` |
| KGuiAddons | `kf6-kguiaddons-devel` | `kguiaddons` | `libkf6guiaddons-dev` | `kf6-kguiaddons-devel` |
| KConfig | `kf6-kconfig-devel` | `kconfig` | `libkf6config-dev` | `kf6-kconfig-devel` |
| KI18n | `kf6-ki18n-devel` | `ki18n` | `libkf6i18n-dev` | `kf6-ki18n-devel` |
| KGlobalAccel | `kf6-kglobalaccel-devel` | `kglobalaccel` | `libkf6globalaccel-dev` | `kf6-kglobalaccel-devel` |
| KStatusNotifierItem | `kf6-kstatusnotifieritem-devel` | `kstatusnotifieritem` | `libkf6statusnotifieritem-dev` | `kf6-kstatusnotifieritem-devel` |
| KIdleTime | `kf6-kidletime-devel` | `kidletime` | `libkf6idletime-dev` | `kf6-kidletime-devel` |
| libdbus | `dbus-devel` | `dbus` | `libdbus-1-dev` | `dbus-1-devel` |

Fedora and Arch names are verified against the live package databases; the
Debian and openSUSE columns follow each distribution's naming convention and
are unverified — check before relying on them.

On Arch these are all in `extra`, which makes a PKGBUILD the shortest path for
Hyprland users:

```bash
# PKGBUILD (sketch)
depends=(qt6-base qt6-declarative layer-shell-qt kwindowsystem kguiaddons
         kconfig ki18n kglobalaccel kstatusnotifieritem kidletime dbus)
makedepends=(cmake extra-cmake-modules ninja)
build()   { cmake -B build -S "$pkgname-$pkgver" -DCMAKE_INSTALL_PREFIX=/usr \
                 -DCMAKE_BUILD_TYPE=Release; cmake --build build; }
package() { DESTDIR="$pkgdir" cmake --install build; }
```

Note the KDE Frameworks packages are libraries, not a desktop: installing
`kwindowsystem` on Hyprland pulls in no part of Plasma.

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
