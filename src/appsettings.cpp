/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "appsettings.h"

#include <KConfigGroup>

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("glassosdrc")))
{
}

QString AppSettings::groupFor(const QString &appName) const
{
    /* Namespaced so UI-created rules are obviously distinct from hand-written
       ones, and so clearing them cannot clobber a rule someone crafted. */
    return QStringLiteral("Rule app-%1").arg(appName);
}

bool AppSettings::muted(const QString &appName) const
{
    KConfigGroup g(m_config, groupFor(appName));
    return g.readEntry("skip_display", false);
}

void AppSettings::setMuted(const QString &appName, bool on)
{
    KConfigGroup g(m_config, groupFor(appName));
    g.writeEntry("appname", appName);   // the matcher the rules engine needs
    g.writeEntry("skip_display", on);
    g.sync();
    Q_EMIT rulesChanged();
}

bool AppSettings::historyIgnored(const QString &appName) const
{
    KConfigGroup g(m_config, groupFor(appName));
    return g.readEntry("history_ignore", false);
}

void AppSettings::setHistoryIgnored(const QString &appName, bool on)
{
    KConfigGroup g(m_config, groupFor(appName));
    g.writeEntry("appname", appName);
    g.writeEntry("history_ignore", on);
    g.sync();
    Q_EMIT rulesChanged();
}

int AppSettings::timeoutSeconds(const QString &appName) const
{
    KConfigGroup g(m_config, groupFor(appName));
    return g.readEntry("timeout", -2) / 1000;
}

void AppSettings::setTimeoutSeconds(const QString &appName, int seconds)
{
    KConfigGroup g(m_config, groupFor(appName));
    g.writeEntry("appname", appName);
    if (seconds <= 0) {
        g.deleteEntry("timeout");
    } else {
        g.writeEntry("timeout", seconds * 1000);
    }
    g.sync();
    Q_EMIT rulesChanged();
}
