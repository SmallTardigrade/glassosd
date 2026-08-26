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

#include <KConfigGroup>

#include <QDBusAbstractAdaptor>
#include <QDBusServiceWatcher>
#include <QHash>
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

    /* Start issuing ids above anything already in persisted history. Ids are
       only unique within one run, but history outlives restarts, so without
       this a fresh notification reuses an old entry's id and overwrites it. */
    void reserveIds(uint highest);

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
    void loadRules(const KSharedConfig::Ptr &config)
    {
        /* Read before load(), because load() compiles the active mode's
           Allow/Block lists into rules and needs to know which mode that is. */
        m_rules.setActiveFocus(
            KConfigGroup(config, QStringLiteral("Notifications")).readEntry("Focus", QString()));
        m_rules.load(config);
    }
    QString activeFocus() const { return m_rules.activeFocus(); }

Q_SIGNALS:
    void closed(uint id, uint reason);
    void actionInvoked(uint id, const QString &key);
    void replied(uint id, const QString &text);
    void activationToken(uint id, const QString &token);
    /* A rule asked for this one to be deferred on arrival. Wired to
       SnoozeStore in main.cpp — the server has no business owning wake times. */
    void snoozeOnArrival(const Notification &n, int minutes);

private:
    NotificationModel *m_model;
    HistoryModel *m_history;
    Rules m_rules;
    uint m_nextId = 1;

public:
    /* Notification inhibition, the KDE extension to org.freedesktop.Notifications
       that xdg-desktop-portal-kde uses to silence notifications while the
       screen is being cast. Implementing it means auto-DND while screen
       sharing costs nothing and arrives by the same route for every other
       application that already asks — screen recorders, presentation tools —
       rather than glassosd watching for one case it happened to think of.

       An inhibition belongs to the bus name that asked for it and is dropped
       when that name goes away, so an application that crashes mid-share
       cannot leave notifications silenced for the rest of the session. */
    /* The caller is taken from the live D-Bus message rather than passed in:
       NotificationServer is a QDBusContext, and trusting an application to
       name itself would let one release another's inhibition. */
    uint inhibit(const QString &desktopEntry, const QString &reason);
    /* Run a rule's run= command for this notification. */
    static void runRuleCommand(const Notification &n);
    void unInhibit(uint cookie);
    bool inhibited() const { return !m_inhibits.isEmpty(); }

private:
    struct Inhibition {
        QString service;
        QString desktopEntry;
        QString reason;
    };
    void refreshInhibited();

    QHash<uint, Inhibition> m_inhibits;
    uint m_nextInhibit = 1;
    QDBusServiceWatcher *m_inhibitWatcher = nullptr;
};

class NotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
    /* Read-only and change-signalled, as upstream declares it. */
    Q_PROPERTY(bool Inhibited READ inhibited NOTIFY inhibitedChanged)
public:
    bool inhibited() const;
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
    uint Inhibit(const QString &desktop_entry, const QString &reason, const QVariantMap &hints);
    void UnInhibit(uint cookie);
    QStringList GetCapabilities();
    QString GetServerInformation(QString &vendor, QString &version, QString &spec_version);

Q_SIGNALS:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);
    /* KDE extension, matching libnotificationmanager's interface. */
    void NotificationReplied(uint id, const QString &text);
    void ActivationToken(uint id, const QString &activation_token);
    void inhibitedChanged();

private:
    NotificationServer *m_server;
};
