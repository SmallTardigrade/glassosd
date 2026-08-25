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

    /* "Muted" means no popup but still recorded, which is almost always what
       people mean; dropping it entirely is a separate, harsher choice. */
    Q_INVOKABLE bool muted(const QString &appName) const;
    Q_INVOKABLE void setMuted(const QString &appName, bool on);

    Q_INVOKABLE bool historyIgnored(const QString &appName) const;
    Q_INVOKABLE void setHistoryIgnored(const QString &appName, bool on);

    Q_INVOKABLE int timeoutSeconds(const QString &appName) const;
    Q_INVOKABLE void setTimeoutSeconds(const QString &appName, int seconds);

Q_SIGNALS:
    void rulesChanged();

private:
    QString groupFor(const QString &appName) const;
    KSharedConfig::Ptr m_config;
};
