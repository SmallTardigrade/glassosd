/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Fn lock, which unlike caps and num lock is not a keyboard modifier at all —
    it is firmware state on the embedded controller. On this Lenovo Yoga the
    ideapad_laptop driver exposes it at
    /sys/bus/platform/devices/VPC2004:00/fn_lock.

    That attribute does not call sysfs_notify(), so poll()/QSocketNotifier
    never fire on it (verified: select.poll returned nothing across a 2s wait).
    Polling is the only option. The read is one byte from a warm sysfs file, so
    a slow interval costs effectively nothing.
*/
#pragma once

#include <QObject>
#include <QTimer>

class FnLockWatcher : public QObject
{
    Q_OBJECT
public:
    explicit FnLockWatcher(QObject *parent = nullptr);

    bool isAvailable() const { return !m_path.isEmpty(); }

Q_SIGNALS:
    void lockChanged(bool locked);

private:
    static QString findPath();
    bool read() const;

    QString m_path;
    QTimer m_timer;
    bool m_state = false;
};
