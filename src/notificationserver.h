/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    org.freedesktop.Notifications, hand-rolled.

    KF6Notifications is client-side only — it sends Notify and listens for
    ActionInvoked. No KDE framework provides the server half, so the adaptor
    below implements the spec directly.
*/
#pragma once

#include "historymodel.h"
#include "notificationmodel.h"
#include "rules.h"

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QObject>
#include <QVariantMap>

class NotificationServer : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    NotificationServer(NotificationModel *model, HistoryModel *history, QObject *parent = nullptr);

    /* Claims org.freedesktop.Notifications. Returns false if something else
       already owns it — during development that is dunst, still running. */
    bool start();

    uint handleNotify(const QString &appName,
                      uint replacesId,
                      const QString &appIcon,
                      const QString &summary,
                      const QString &body,
                      const QStringList &actions,
                      const QVariantMap &hints,
                      int expireTimeout);
    void handleClose(uint id);

    NotificationModel *model() const { return m_model; }
    void loadRules(const KSharedConfig::Ptr &config) { m_rules.load(config); }

Q_SIGNALS:
    void closed(uint id, uint reason);
    void actionInvoked(uint id, const QString &key);
    void replied(uint id, const QString &text);
    void activationToken(uint id, const QString &token);

private:
    NotificationModel *m_model;
    HistoryModel *m_history;
    Rules m_rules;
    uint m_nextId = 1;
};

class NotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationsAdaptor(NotificationServer *server);

public Q_SLOTS:
    uint Notify(const QString &app_name,
                uint replaces_id,
                const QString &app_icon,
                const QString &summary,
                const QString &body,
                const QStringList &actions,
                const QVariantMap &hints,
                int expire_timeout);
    void CloseNotification(uint id);
    QStringList GetCapabilities();
    QString GetServerInformation(QString &vendor, QString &version, QString &spec_version);

Q_SIGNALS:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);
    /* KDE extension, matching libnotificationmanager's interface. */
    void NotificationReplied(uint id, const QString &text);
    void ActivationToken(uint id, const QString &activation_token);

private:
    NotificationServer *m_server;
};
