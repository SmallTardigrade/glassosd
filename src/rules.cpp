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
        if (g.hasKey("skip_display")) {
            r.skipDisplay = g.readEntry("skip_display", false);
        }
        r.historyIgnore = g.readEntry("history_ignore", false);
        r.sound = g.readEntry("sound", QString());
        r.focus = g.readEntry("focus", QString());
        r.run = g.readEntry("run", QString());
        r.snoozeMinutes = g.readEntry("snooze", -1);

        m_rules.append(r);
    }
    /* The active mode's Allow/Block lists become rules, appended after the
       hand-written ones so they have the final say — a focus mode is a
       deliberate act and should not be quietly undone by a rule someone wrote
       months ago.

       Compiled rather than checked separately so there is one engine and one
       set of semantics. Allow is the interesting half: hide everything, then
       put back the named few. That needs skip_display to be able to say false,
       which is why it is tri-state. */
    if (!m_focus.isEmpty()) {
        KConfigGroup fg(config, QStringLiteral("Focus %1").arg(m_focus));

        const QStringList allow = fg.readEntry("Allow", QStringList());
        if (!allow.isEmpty()) {
            Rule hideAll;
            hideAll.name = QStringLiteral("focus:%1:hide-all").arg(m_focus);
            hideAll.skipDisplay = true;
            m_rules.append(hideAll);

            for (const QString &who : allow) {
                const QString trimmed = who.trimmed();
                if (trimmed.isEmpty()) {
                    continue;
                }
                /* Matched on either name, because the thing a person types is
                   whichever of the two they happen to know. */
                Rule byApp;
                byApp.name = QStringLiteral("focus:%1:allow:%2").arg(m_focus, trimmed);
                byApp.appName = QRegularExpression(
                    QRegularExpression::wildcardToRegularExpression(trimmed),
                    QRegularExpression::CaseInsensitiveOption);
                byApp.skipDisplay = false;
                m_rules.append(byApp);

                Rule byEntry = byApp;
                byEntry.name = QStringLiteral("focus:%1:allow-entry:%2").arg(m_focus, trimmed);
                byEntry.appName.reset();
                byEntry.desktopEntry = QRegularExpression(
                    QRegularExpression::wildcardToRegularExpression(trimmed),
                    QRegularExpression::CaseInsensitiveOption);
                m_rules.append(byEntry);
            }
        }

        for (const QString &who : fg.readEntry("Block", QStringList())) {
            const QString trimmed = who.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            Rule block;
            block.name = QStringLiteral("focus:%1:block:%2").arg(m_focus, trimmed);
            block.appName = QRegularExpression(
                QRegularExpression::wildcardToRegularExpression(trimmed),
                QRegularExpression::CaseInsensitiveOption);
            block.skipDisplay = true;
            m_rules.append(block);
        }
    }

    if (!m_rules.isEmpty()) {
        qWarning("glassosd: loaded %d notification rule(s)%s", int(m_rules.size()),
                 m_focus.isEmpty() ? ""
                                   : qUtf8Printable(QStringLiteral(", focus '%1'").arg(m_focus)));
    }
}

void Rules::apply(Notification &n) const
{
    /* Every matching rule is applied in order, so later rules refine earlier
       ones rather than the first match winning — again matching dunst. */
    for (const Rule &r : m_rules) {
        /* A rule scoped to a focus mode is simply not there when that mode is
           off, which is what makes a mode a bundle rather than a set of edits
           to undo afterwards. */
        if (!r.focus.isEmpty() && r.focus != m_focus) {
            continue;
        }
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
        if (r.skipDisplay.has_value()) {
            n.skipDisplay = *r.skipDisplay;
        }
        if (r.historyIgnore) {
            n.historyIgnore = true;
        }
        if (!r.sound.isEmpty()) {
            n.sound = r.sound;
        }
        if (!r.run.isEmpty()) {
            n.run = r.run;
        }
        if (r.snoozeMinutes > 0) {
            n.snoozeMinutes = r.snoozeMinutes;
        }
    }
}
