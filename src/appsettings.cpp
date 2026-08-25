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

namespace
{
/* Every setter needs the matcher written alongside its action, or the rule
   group exists but matches nothing. Reparse first: the config file is also
   written by glassosdctl and by hand, and a stale in-memory copy would drop
   whatever changed there when this group is synced back. */
KConfigGroup writable(const KSharedConfig::Ptr &config, const QString &group, const QString &appName)
{
    config->reparseConfiguration();
    KConfigGroup g(config, group);
    g.writeEntry("appname", appName);
    return g;
}

/* A rule group with a matcher and no actions left is dead weight in the file
   and, worse, reads as "this app is configured" when nothing is configured.
   Once the last action is cleared, take the group with it — but only when
   `appname` is genuinely all that is left, so a rule somebody hand-wrote into
   this namespace with matchers or actions we do not know about survives. */
void pruneIfEmpty(KConfigGroup &g)
{
    const QStringList keys = g.keyList();
    if (keys.size() == 1 && keys.first() == QLatin1String("appname")) {
        g.deleteGroup();
    }
}

/* Booleans are stored only when true. Writing `skip_display=false` would work,
   but it leaves the file full of rules that say nothing, and it defeats
   pruneIfEmpty above. */
void writeFlag(KConfigGroup &g, const char *key, bool on)
{
    if (on) {
        g.writeEntry(key, true);
    } else {
        g.deleteEntry(key);
    }
}
} // namespace

bool AppSettings::muted(const QString &appName) const
{
    KConfigGroup g(m_config, groupFor(appName));
    return g.readEntry("skip_display", false);
}

void AppSettings::setMuted(const QString &appName, bool on)
{
    KConfigGroup g = writable(m_config, groupFor(appName), appName);
    writeFlag(g, "skip_display", on);
    pruneIfEmpty(g);
    m_config->sync();
    Q_EMIT rulesChanged();
}

bool AppSettings::ignored(const QString &appName) const
{
    KConfigGroup g(m_config, groupFor(appName));
    return g.readEntry("history_ignore", false);
}

void AppSettings::setIgnored(const QString &appName, bool on)
{
    KConfigGroup g = writable(m_config, groupFor(appName), appName);
    writeFlag(g, "history_ignore", on);
    /* See the header: Ignore owns skip_display for as long as it is on, so
       that "ignored" cannot mean "invisible in history but still flashing on
       screen" — which is what history_ignore alone would give. */
    writeFlag(g, "skip_display", on);
    pruneIfEmpty(g);
    m_config->sync();
    Q_EMIT rulesChanged();
}

bool AppSettings::neverExpires(const QString &appName) const
{
    KConfigGroup g(m_config, groupFor(appName));
    /* -2 is the rules engine's "leave the notification's own timeout alone".
       0 is the spec's "never expire". They are different states and the
       default must not be mistaken for the second. */
    return g.readEntry("timeout", -2) == 0;
}

void AppSettings::setNeverExpires(const QString &appName, bool on)
{
    KConfigGroup g = writable(m_config, groupFor(appName), appName);
    if (on) {
        g.writeEntry("timeout", 0);
    } else {
        g.deleteEntry("timeout");
    }
    pruneIfEmpty(g);
    m_config->sync();
    Q_EMIT rulesChanged();
}

bool AppSettings::alwaysCollapsed(const QString &appName) const
{
    KConfigGroup g(m_config, groupFor(appName));
    return g.readEntry("always_collapsed", false);
}

void AppSettings::setAlwaysCollapsed(const QString &appName, bool on)
{
    KConfigGroup g = writable(m_config, groupFor(appName), appName);
    writeFlag(g, "always_collapsed", on);
    pruneIfEmpty(g);
    m_config->sync();
    Q_EMIT rulesChanged();
}
