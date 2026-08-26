/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    dunstrc-equivalent per-app rules.

    This is what makes stack tags actually useful. The freedesktop hint only
    helps when the *sending app* sets it, and most do not — the existing
    dunstrc's wacom-pen rule works by matching on appname and summary and
    assigning the tag itself. Without this, that configuration is lost.
*/
#pragma once

#include "notification.h"

#include <KSharedConfig>
#include <QList>
#include <QRegularExpression>

struct Rule {
    QString name;

    /* Matchers. An unset field means "don't care"; every set field must match,
       which is dunst's behaviour. Patterns are globs, not regexes, again
       matching dunst (and letting dunstrc entries be copied across verbatim). */
    std::optional<QRegularExpression> appName;
    std::optional<QRegularExpression> summary;
    std::optional<QRegularExpression> body;
    std::optional<QRegularExpression> category;
    std::optional<QRegularExpression> desktopEntry;
    int matchUrgency = -1;

    /* Actions. */
    QString setStackTag;
    int timeoutMs = -2;      // -2 == leave alone
    int setUrgency = -1;
    /* Tri-state, not a bool: a rule has to be able to say "show this" as well
       as "hide this", or an allow-list is impossible — it needs a catch-all
       that hides everything followed by rules that put a few back. Unset
       leaves whatever an earlier rule decided. */
    std::optional<bool> skipDisplay;
    bool historyIgnore = false;
    /* Sound-theme name to play instead of whatever category and urgency would
       have chosen. Empty leaves the choice alone; "none" silences this rule. */
    QString sound;
    /* Only applied while this focus mode is active. Empty means always. */
    QString focus;
    /* Run on match. Split into arguments here rather than handed to a shell,
       so a summary containing a semicolon is an argument and not a command. */
    QString run;
    /* Defer on arrival: don't show it now, bring it back in N minutes. */
    int snoozeMinutes = -1;

    bool matches(const Notification &n) const;
};

class Rules
{
public:
    void load(const KSharedConfig::Ptr &config);
    /* Which focus mode is on. Rules naming a different one are skipped, and
       the active mode's Allow/Block lists are compiled into rules of their
       own — see load(). */
    void setActiveFocus(const QString &focus) { m_focus = focus; }
    QString activeFocus() const { return m_focus; }
    void apply(Notification &n) const;
    int count() const { return m_rules.size(); }

private:
    QList<Rule> m_rules;
    QString m_focus;
};
