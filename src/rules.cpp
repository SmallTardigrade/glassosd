/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "rules.h"

#include <KConfigGroup>
#include <QDebug>

namespace
{
std::optional<QRegularExpression> globOf(const KConfigGroup &g, const char *key)
{
    const QString pattern = g.readEntry(key, QString());
    if (pattern.isEmpty()) {
        return std::nullopt;
    }
    /* Glob, not regex — "Device Battery Low*" from the existing dunstrc must
       behave the way it does under dunst. */
    return QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern),
                              QRegularExpression::CaseInsensitiveOption);
}

bool matchOne(const std::optional<QRegularExpression> &re, const QString &value)
{
    return !re.has_value() || re->match(value).hasMatch();
}
} // namespace

bool Rule::matches(const Notification &n) const
{
    if (matchUrgency >= 0 && static_cast<int>(n.urgency) != matchUrgency) {
        return false;
    }
    return matchOne(appName, n.appName)
        && matchOne(summary, n.summary)
        && matchOne(body, n.body)
        && matchOne(category, n.category)
        && matchOne(desktopEntry, n.desktopEntry);
}

void Rules::load(const KSharedConfig::Ptr &config)
{
    m_rules.clear();
    for (const QString &groupName : config->groupList()) {
        if (!groupName.startsWith(QLatin1String("Rule "))) {
            continue;
        }
        KConfigGroup g(config, groupName);

        Rule r;
        r.name = groupName.mid(5);
        r.appName = globOf(g, "appname");
        r.summary = globOf(g, "summary");
        r.body = globOf(g, "body");
        r.category = globOf(g, "category");
        r.desktopEntry = globOf(g, "desktop_entry");
        r.matchUrgency = g.readEntry("match_urgency", -1);

        r.setStackTag = g.readEntry("set_stack_tag", QString());
        r.timeoutMs = g.readEntry("timeout", -2);
        r.setUrgency = g.readEntry("set_urgency", -1);
        r.skipDisplay = g.readEntry("skip_display", false);
        r.historyIgnore = g.readEntry("history_ignore", false);

        m_rules.append(r);
    }
    if (!m_rules.isEmpty()) {
        qWarning("glassosd: loaded %d notification rule(s)", int(m_rules.size()));
    }
}

void Rules::apply(Notification &n) const
{
    /* Every matching rule is applied in order, so later rules refine earlier
       ones rather than the first match winning — again matching dunst. */
    for (const Rule &r : m_rules) {
        if (!r.matches(n)) {
            continue;
        }
        if (!r.setStackTag.isEmpty()) {
            n.stackTag = r.setStackTag;
        }
        if (r.timeoutMs != -2) {
            n.timeoutMs = r.timeoutMs;
        }
        if (r.setUrgency >= 0) {
            n.urgency = static_cast<Urgency>(r.setUrgency);
        }
        if (r.skipDisplay) {
            n.skipDisplay = true;
        }
        if (r.historyIgnore) {
            n.historyIgnore = true;
        }
    }
}
