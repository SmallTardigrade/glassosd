/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    Which halves of glassosd are switched on.

    Nobody should have to accept a notification daemon to get a volume OSD, or
    a volume OSD to get a notification daemon. Each module is independent: off
    means the code path is never constructed, not merely hidden, so a disabled
    module costs no bus name, no Wayland surface and no watcher.

    [Modules] in glassosdrc, all defaulting to true:

        Notifications   own org.freedesktop.Notifications, draw popups
        NotificationCentre  the history panel and its tray icon
        Osd             volume / brightness / media OSD
        LockKeys        caps, num and Fn lock OSDs
*/
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QQmlEngine>

class Modules : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool notifications READ notifications NOTIFY changed)
    Q_PROPERTY(bool notificationCentre READ notificationCentre NOTIFY changed)
    Q_PROPERTY(bool osd READ osd NOTIFY changed)
    Q_PROPERTY(bool lockKeys READ lockKeys NOTIFY changed)

public:
    explicit Modules(QObject *parent = nullptr);

    bool notifications() const { return m_notifications; }
    bool notificationCentre() const { return m_centre; }
    bool osd() const { return m_osd; }
    bool lockKeys() const { return m_lockKeys; }

    /* Read once at startup. Turning a module on or off changes which objects
       exist, so it is a restart, not a live reload — and saying so is kinder
       than silently doing half of it. */
    void load();

Q_SIGNALS:
    void changed();

private:
    KSharedConfig::Ptr m_config;
    bool m_notifications = true;
    bool m_centre = true;
    bool m_osd = true;
    bool m_lockKeys = true;
};
