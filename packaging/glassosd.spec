# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later

Name:           glassosd
Version:        0.1.0
# Plain Release rather than %%autorelease: rpmautospec is not available in
# every builder, and a spec that only builds on Fedora infrastructure is not
# much use to someone packaging this for anything else.
Release:        1%{?dist}
Summary:        Glass notification daemon, OSD and notification centre for Wayland

# The OSD event translation in src/osdmodel.cpp is a port of
# plasma-workspace shell/osd.cpp, which is GPL-2.0-or-later; that licence
# governs the whole binary. The icon set in qml/icons is CC0-1.0.
License:        GPL-2.0-or-later AND CC0-1.0
URL:            https://github.com/SmallTardigrade/glassosd
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.20
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  extra-cmake-modules
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  systemd-rpm-macros

BuildRequires:  qt6-qtbase-devel >= 6.6
BuildRequires:  qt6-qtdeclarative-devel >= 6.6

BuildRequires:  kf6-kwindowsystem-devel
BuildRequires:  kf6-kguiaddons-devel
BuildRequires:  kf6-kconfig-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kglobalaccel-devel
BuildRequires:  kf6-kstatusnotifieritem-devel
BuildRequires:  kf6-kidletime-devel
BuildRequires:  layer-shell-qt-devel

# The QML runtime is dlopen()ed, so RPM's automatic dependency generator
# cannot see it.
Requires:       qt6-qtdeclarative%{?_isa}
Requires:       qt6-qtwayland%{?_isa}
# glassosdctl speaks to the daemon over the session bus.
Requires:       systemd

# Optional, and genuinely optional: glassosdctl uses kwriteconfig6 when it is
# there and edits the INI itself when it is not.
Recommends:     kf6-kconfig
# Routes sandboxed apps' portal notifications to the daemon; see the README.
Recommends:     xdg-desktop-portal-gtk
# Only needed for `glassosdctl osd volume|mic` on compositors that have no
# org.kde.osdService to monitor.
Suggests:       wireplumber
Suggests:       brightnessctl

# Deliberately no Conflicts: with dunst/mako/swaync. They share no files, and
# only one daemon can own org.freedesktop.Notifications at a time anyway — that
# is a runtime question, not an install-time one. Blocking the install would
# stop people trying glassosd without uninstalling what they have.

%description
glassosd draws four Wayland shell surfaces with one rounded, blurred, glass
visual language: notification popups, the volume/brightness OSD, lock-key
indicators, and a notification centre.

As a notification daemon it implements the freedesktop Desktop Notifications
specification plus the KDE extensions (inline reply, action icons, image
hints), and adds the behaviour that makes a daemon usable on a busy desktop:
rate limiting, replace-by-tag, per-app burst coalescing so a wake-from-suspend
storm collapses into one card per app, an idle threshold, persistent history,
Do Not Disturb, and per-app rules.

It runs on any compositor implementing wlr-layer-shell-unstable-v1 — KDE
Plasma, Hyprland, sway, river, Wayfire. Plasma integration (the native OSD
event feed, global shortcuts, KWin background blur) is detected at runtime;
everything it provides is also reachable over D-Bus for compositors that
manage their own keybindings.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -GNinja
%cmake_build

%install
%cmake_install
install -Dpm0755 tools/demo-notifications.sh %{buildroot}%{_datadir}/%{name}/demo-notifications.sh
install -Dpm0755 tools/stress-test.sh        %{buildroot}%{_datadir}/%{name}/stress-test.sh
install -Dpm0755 tools/verify-routing.sh     %{buildroot}%{_datadir}/%{name}/verify-routing.sh
install -Dpm0755 tools/input-audit.sh        %{buildroot}%{_datadir}/%{name}/input-audit.sh

%files
%license LICENSE LICENSES/CC0-1.0.txt
%doc README.md packaging/DEPENDENCIES.md
%{_bindir}/glassosd
%{_bindir}/glassosdctl
%{_bindir}/glassosd-setup
%{_mandir}/man1/glassosd.1*
%{_mandir}/man1/glassosdctl.1*
%{_mandir}/man5/glassosdrc.5*
%{_datadir}/glassosd/themes/
%{_userunitdir}/glassosd.service
%{_datadir}/dbus-1/services/org.freedesktop.Notifications.service
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/demo-notifications.sh
%{_datadir}/%{name}/stress-test.sh
%{_datadir}/%{name}/verify-routing.sh
%{_datadir}/%{name}/input-audit.sh

%post
cat <<'EOF'

glassosd is installed. It is D-Bus activated, so the next application to post
a notification will start it — but for the OSD and the notification centre you
want it running for the whole session:

    systemctl --user enable --now glassosd.service

On Plasma, turn off the built-in popups so they do not double up:
    kwriteconfig6 --file plasmarc --group OSD --key Enabled false

To keep your existing notification daemon and use glassosd for the OSD only:
    glassosdctl set Enabled false

EOF

%changelog
* Tue Aug 25 2026 glassosd contributors - 0.1.0-1
- First release.
