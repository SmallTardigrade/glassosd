/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    The portal backend for when glassosd is not the notification daemon.

    It forwards each portal notification to whoever owns
    org.freedesktop.Notifications — Plasma, dunst, swaync, anything — as an
    ordinary Notify call, and maps that daemon's clicks and closes back to the
    application. xdg-desktop-portal-gtk does the same job; this one exists so
    that a portals.conf naming glassosd can never stall the portal, whatever
    state glassosd is in.

    It declares version 1, because the freedesktop interface cannot carry
    sound, display hints or button purposes. The portal frontend strips those
    before they arrive, which is the honest outcome: a relay that accepted
    them and dropped them would look like an application that never sent them.
*/
#pragma once

#include "notification.h"
#include "portalbackend.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class PortalRelay : public PortalBackend
{
    Q_OBJECT
public:
    explicit PortalRelay(QObject *parent = nullptr);

    void addNotification(const QString &appId, const QString &portalId, const QVariantMap &props) override;
    void removeNotification(const QString &appId, const QString &portalId) override;

    uint version() const override { return 1; }
    QVariantMap supportedOptions() const override { return {}; }

    /* One portal notification as the Notify arguments it becomes, plus what
       is needed to route a click back. Pure, so it can be tested without a
       bus. */
    struct NotifyCall {
        QString appName;
        QString appIcon;
        QString summary;
        QString body;
        QStringList actions;
        QVariantMap hints;
        int timeout = -1;
        QHash<QString, QVariant> targets;
        QString defaultActionName;
    };
    static NotifyCall translate(const QString &appId, const QVariantMap &props);

protected:
    const char *describe() const override
    {
        return "relaying portal notifications to the session's notification daemon";
    }

private Q_SLOTS:
    /* From whichever daemon owns org.freedesktop.Notifications. */
    void onActionInvoked(uint id, const QString &key);
    void onActivationToken(uint id, const QString &token);
    void onClosed(uint id, uint reason);

private:
    struct Entry {
        QString appId;
        QString portalId;
        QHash<QString, QVariant> targets;
        QString defaultActionName;
    };
    static QString keyFor(const QString &appId, const QString &portalId);
    void forget(uint id);

    QHash<uint, Entry> m_entries;   // by the daemon's notification id
    QHash<QString, uint> m_ids;     // (app id, portal id) -> the daemon's id
    QSet<QString> m_inFlight;       // Notify sent, reply not yet back
    QSet<QString> m_withdrawn;      // removed while its Notify was in flight
    QHash<uint, QString> m_tokens;  // ActivationToken arrives just before ActionInvoked
    bool m_warned = false;
};
