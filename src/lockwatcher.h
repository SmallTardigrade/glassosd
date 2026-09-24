/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Whether the session is locked.

    Notifications carry hints asking not to be shown, or not to be readable,
    on a lock screen — an application saying "the balance in this message is
    nobody else's business". Honouring them needs the one thing glassosd never
    tracked: it could always *start* a lock, and never knew when one was up.

    org.freedesktop.ScreenSaver is the interface to ask. KWin owns it, GNOME
    owns it, and anything implementing the freedesktop spec owns it. Where no
    one does — a bare wlroots session with swaylock and nothing else — there
    is no way to observe another client's lock, so the session is reported
    unlocked and the hints cannot be honoured. Saying so is better than
    guessing: a notification wrongly treated as private disappears with no
    explanation.
*/
#pragma once

#include <QDBusServiceWatcher>
#include <QObject>

class LockWatcher : public QObject
{
    Q_OBJECT
public:
    explicit LockWatcher(QObject *parent = nullptr);

    bool locked() const { return m_locked; }

    /* Whether anything is answering for the lock state at all. Used once at
       startup to say so, because "the hints are being ignored" is otherwise
       indistinguishable from "no application sent one". */
    bool available() const { return m_available; }

Q_SIGNALS:
    void lockedChanged(bool locked);

private Q_SLOTS:
    /* A real slot rather than a lambda: QDBusConnection::connect() takes the
       SLOT() form only. */
    void onActiveChanged(bool active);

private:
    void attach();
    void detach();
    void refresh();
    void setLocked(bool locked);

    QDBusServiceWatcher *m_watcher = nullptr;
    bool m_locked = false;
    bool m_available = false;
};
