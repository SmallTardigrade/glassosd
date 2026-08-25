/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Per-app notification settings, exposed to QML.

    These are not a new mechanism: each toggle writes a rule into glassosdrc,
    the same rules engine that already drives stack tags and timeouts. So the
    settings UI and a hand-edited config file cannot disagree, and anything the
    UI can do is equally doable from glassosdctl.

    The panel deliberately offers four *outcomes* rather than the rule keys
    behind them. "timeout=0" and "history_ignore=true" are precise and mean
    nothing to someone who has not read the rules documentation; "Never expire"
    and "Ignore" are what the person actually wants. Each outcome is still one
    rule key (Ignore is the one exception, and says why below), so the mapping
    stays inspectable in the config file.
*/
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QQmlEngine>

class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit AppSettings(QObject *parent = nullptr);

    /* Mute: no popup, still recorded. Almost always what people mean by
       "shut this app up" — they still want to find it later. */
    Q_INVOKABLE bool muted(const QString &appName) const;
    Q_INVOKABLE void setMuted(const QString &appName, bool on);

    /* Ignore: dropped entirely, neither shown nor recorded.

       This is the one control that writes two keys. history_ignore on its own
       means "show it but keep no record", which is a legitimate hand-written
       rule for transient things, but it is not what anybody choosing "Ignore"
       in a settings panel is asking for. So Ignore owns skip_display too for
       as long as it is on, and the Mute row goes read-only to show that.
       Turning Ignore back off therefore clears Mute as well; set Mute again if
       that is what you wanted. Hand-written rules can still use either key on
       its own — this coupling lives in the UI, not in the rules engine. */
    Q_INVOKABLE bool ignored(const QString &appName) const;
    Q_INVOKABLE void setIgnored(const QString &appName, bool on);

    /* Never expire: the popup stays until it is dismissed. timeout=0 is the
       freedesktop spec's own encoding of that, not an invention here. */
    Q_INVOKABLE bool neverExpires(const QString &appName) const;
    Q_INVOKABLE void setNeverExpires(const QString &appName, bool on);

    /* Always collapsed: this app's group opens folded in the centre, however
       few entries it has. For an app that is merely chatty rather than
       unwanted — where AutoCollapseOver has not kicked in yet but you still
       do not want it taking the panel. */
    Q_INVOKABLE bool alwaysCollapsed(const QString &appName) const;
    Q_INVOKABLE void setAlwaysCollapsed(const QString &appName, bool on);

Q_SIGNALS:
    void rulesChanged();

private:
    QString groupFor(const QString &appName) const;
    KSharedConfig::Ptr m_config;
};
