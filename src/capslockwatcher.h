/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Caps lock state, which Plasma itself has no OSD for (bugs.kde.org 470075 —
    the only built-in surface is a tray-icon-only plasmoid).

    KWin implements the org_kde_kwin_keystate Wayland protocol and
    KModifierKeyInfoProviderWayland is compiled into libKF6GuiAddons, so this
    needs no root, no membership of the "input" group, and no polling of
    /sys/class/leds. It also works while unfocused, which matters because our
    surfaces never take focus.
*/
#pragma once

#include <QHash>
#include <QObject>

#include <KModifierKeyInfo>

class CapsLockWatcher : public QObject
{
    Q_OBJECT
public:
    explicit CapsLockWatcher(QObject *parent = nullptr);

Q_SIGNALS:
    void lockChanged(int key, bool locked);

private:
    KModifierKeyInfo m_info;
    QHash<int, bool> m_state;
};
