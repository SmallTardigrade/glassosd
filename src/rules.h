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
    bool skipDisplay = false;
    bool historyIgnore = false;

    bool matches(const Notification &n) const;
};

class Rules
{
public:
    void load(const KSharedConfig::Ptr &config);
    void apply(Notification &n) const;
    int count() const { return m_rules.size(); }

private:
    QList<Rule> m_rules;
};
