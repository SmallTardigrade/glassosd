/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    org.freedesktop.impl.portal.Notification — the backend half of the XDG
    desktop portal's notification interface.

    A sandboxed application cannot reach org.freedesktop.Notifications: the
    name is outside its sandbox. It talks to xdg-desktop-portal instead, which
    validates the request and hands it to whichever backend the desktop
    nominates. Without a backend the portal exports no notification interface
    at all — it has no fallback to the freedesktop spec — so a desktop running
    glassosd and nothing else leaves every Flatpak silent.

    This is a second front door, not a second pipeline. Everything it receives
    is turned into a Notification and passed to NotificationServer::deliver(),
    so rules, snooze, history, coalescing and sounds behave identically
    whichever way a notification arrived.

    What the portal path gains over Notify() is identity: app_id is derived by
    the portal from the sandbox rather than claimed by the sender, so a rule
    matching on it cannot be fooled by an application naming itself something
    else.
*/
#pragma once

#include "notification.h"

#include <QDBusAbstractAdaptor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

class NotificationModel;
class NotificationServer;

class PortalServer : public QObject
{
    Q_OBJECT
public:
    PortalServer(NotificationServer *server, NotificationModel *model, QObject *parent = nullptr);

    /* Claims org.freedesktop.impl.portal.desktop.glassosd. Returns false if
       the name is already owned, which in practice means a second glassosd. */
    bool start();

    /* Portal notifications are addressed by (app_id, string id) and ours by
       uint, so every portal notification needs an id minted on this side. The
       top bit is reserved for them: NotificationServer's sequence starts at 1
       and history never reaches 2^31, so the two can never collide and any
       id can be attributed to its interface by inspection.

       Borrowed from libnotificationmanager, which reserves the same bit. */
    static constexpr uint idMask = NotificationId::portalMask;
    static bool owns(uint id) { return NotificationId::isPortal(id); }

    /* Start above anything already in persisted history, so a portal id kept
       from a previous run is not handed out again to a different
       notification. The freedesktop side reserves its range the same way. */
    void reserveIds(uint highest);

    void addNotification(const QString &appId, const QString &portalId, const QVariantMap &props);
    void removeNotification(const QString &appId, const QString &portalId);

    /* Advertised to applications through the portal frontend so they can ask
       what this desktop understands before sending. */
    static QVariantMap supportedOptions();

Q_SIGNALS:
    /* Only for actions the application did not export. An action named
       "app.something" is delivered by activating the application over D-Bus
       instead — see invoke(). */
    void actionInvoked(const QString &appId,
                       const QString &portalId,
                       const QString &action,
                       const QVariantList &parameter);

private:
    struct Entry {
        QString appId;
        QString portalId;
        /* Per-action "target" values, handed back when the action fires. The
           application chose them and they mean nothing to us. */
        QHash<QString, QVariant> targets;
        /* The action name of the button carrying purpose im.reply-with-text,
           if the sender offered one. glassosd renders that as its inline
           reply field rather than as a button. */
        QString replyAction;
    };

    uint mint(const QString &appId, const QString &portalId, bool *replacing);
    void invoke(uint id, const QString &action, const QString &response);
    void forget(uint id);

    NotificationServer *m_server;
    NotificationModel *m_model;
    QHash<uint, Entry> m_entries;
    /* activationToken() arrives just before actionInvoked() for the same id;
       held here for the moment in between. */
    QHash<uint, QString> m_tokens;
    uint m_nextId = idMask + 1;
};

class PortalNotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Notification")
    /* The portal reads this once, while constructing its proxy, and never
       again — there is no properties-changed handler on its side. Declaring 2
       is what stops the frontend stripping sound, category, markup-body,
       display hints and button purposes on the way through. */
    Q_PROPERTY(uint version READ version CONSTANT)
    Q_PROPERTY(QVariantMap SupportedOptions READ supportedOptions CONSTANT)
public:
    explicit PortalNotificationsAdaptor(PortalServer *server);

    uint version() const { return 2; }
    QVariantMap supportedOptions() const { return PortalServer::supportedOptions(); }

public Q_SLOTS:
    void AddNotification(const QString &app_id, const QString &id, const QVariantMap &notification);
    void RemoveNotification(const QString &app_id, const QString &id);

Q_SIGNALS:
    void ActionInvoked(const QString &app_id,
                       const QString &id,
                       const QString &action,
                       const QVariantList &parameter);

private:
    PortalServer *m_server;
};
