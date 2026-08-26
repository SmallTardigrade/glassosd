/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "busywatcher.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace
{
constexpr auto kService = "org.freedesktop.PowerManagement";
constexpr auto kPath = "/org/freedesktop/PowerManagement/Inhibit";
constexpr auto kIface = "org.freedesktop.PowerManagement.Inhibit";
} // namespace

BusyWatcher::BusyWatcher(QObject *parent)
    : QObject(parent)
{
    m_iface = new QDBusInterface(QString::fromLatin1(kService),
                                 QString::fromLatin1(kPath),
                                 QString::fromLatin1(kIface),
                                 QDBusConnection::sessionBus(),
                                 this);
    if (!m_iface->isValid()) {
        qInfo("glassosd: no power-management inhibit service; "
              "quiet-while-busy will do nothing");
        return;
    }

    /* Change-signalled, so nothing here polls. */
    QDBusConnection::sessionBus().connect(QString::fromLatin1(kService),
                                          QString::fromLatin1(kPath),
                                          QString::fromLatin1(kIface),
                                          QStringLiteral("HasInhibitChanged"),
                                          this,
                                          SLOT(refresh()));
    refresh();
}

void BusyWatcher::setEnabled(bool on)
{
    if (m_enabled == on) {
        return;
    }
    m_enabled = on;
    /* Turning it off has to lift the quiet immediately, or a session that was
       busy when the setting changed stays silent with nothing explaining it. */
    Q_EMIT changed(m_enabled && m_busy);
}

void BusyWatcher::refresh()
{
    if (!m_iface || !m_iface->isValid()) {
        return;
    }
    const QDBusReply<bool> reply = m_iface->call(QStringLiteral("HasInhibit"));
    if (reply.isValid()) {
        setBusy(reply.value());
    }
}

void BusyWatcher::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT changed(m_enabled && m_busy);
}
