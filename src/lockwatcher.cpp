/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lockwatcher.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusConnectionInterface>
#include <QDBusPendingReply>

namespace
{
constexpr QLatin1String kService{"org.freedesktop.ScreenSaver"};
constexpr QLatin1String kInterface{"org.freedesktop.ScreenSaver"};
/* KWin answers on both, and so does gnome-shell's compatibility object. The
   spec'd path is the long one; the short one is what several implementations
   actually register first, so try it as well rather than depending on which. */
constexpr QLatin1String kPath{"/org/freedesktop/ScreenSaver"};
constexpr QLatin1String kAltPath{"/ScreenSaver"};
} // namespace

LockWatcher::LockWatcher(QObject *parent)
    : QObject(parent)
{
    m_watcher = new QDBusServiceWatcher(kService,
                                        QDBusConnection::sessionBus(),
                                        QDBusServiceWatcher::WatchForOwnerChange,
                                        this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { attach(); });
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { detach(); });

    if (QDBusConnection::sessionBus().interface()
        && QDBusConnection::sessionBus().interface()->isServiceRegistered(kService).value()) {
        attach();
    }
}

void LockWatcher::attach()
{
    auto bus = QDBusConnection::sessionBus();
    /* Both paths, because implementations differ on which they use and a
       connect to a path nobody signals on simply never fires. */
    for (const QLatin1String &path : {kPath, kAltPath}) {
        bus.connect(kService, path, kInterface, QStringLiteral("ActiveChanged"),
                    this, SLOT(onActiveChanged(bool)));
    }
    m_available = true;
    refresh();
}

void LockWatcher::detach()
{
    /* Whoever was answering has gone. A session with no lock service is an
       unlocked one as far as anything here can tell, and leaving the last
       known value latched would hide notifications indefinitely. */
    m_available = false;
    setLocked(false);
}

void LockWatcher::refresh()
{
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface,
                                                       QStringLiteral("GetActive"));
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                const QDBusPendingReply<bool> reply = *w;
                w->deleteLater();
                if (!reply.isError()) {
                    setLocked(reply.value());
                }
            });
}

void LockWatcher::onActiveChanged(bool active)
{
    setLocked(active);
}

void LockWatcher::setLocked(bool locked)
{
    if (m_locked == locked) {
        return;
    }
    m_locked = locked;
    Q_EMIT lockedChanged(m_locked);
}
