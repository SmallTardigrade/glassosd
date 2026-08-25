/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "modules.h"

#include <KConfigGroup>
#include <QDebug>

Modules::Modules(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("glassosdrc")))
{
    load();
}

void Modules::load()
{
    m_config->reparseConfiguration();
    const KConfigGroup g(m_config, QStringLiteral("Modules"));

    m_notifications = g.readEntry("Notifications", true);
    m_centre = g.readEntry("NotificationCentre", true);
    m_osd = g.readEntry("Osd", true);
    m_lockKeys = g.readEntry("LockKeys", true);

    /* The centre is a view onto notification history. Without the daemon
       there is nothing to show, and an empty panel bound to a shortcut is a
       worse experience than no panel — so fold it rather than half-enable. */
    if (m_centre && !m_notifications) {
        qInfo("glassosd: NotificationCentre needs Notifications; disabling the centre");
        m_centre = false;
    }

    if (!m_notifications && !m_osd) {
        qWarning("glassosd: every module is disabled — the daemon will do nothing. "
                 "Set [Modules] Notifications=true or Osd=true in glassosdrc");
    }

    qInfo("glassosd: modules — notifications=%s centre=%s osd=%s lockkeys=%s",
          m_notifications ? "on" : "off", m_centre ? "on" : "off",
          m_osd ? "on" : "off", m_lockKeys ? "on" : "off");

    Q_EMIT changed();
}
