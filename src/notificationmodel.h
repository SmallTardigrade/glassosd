/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    The visible notification stack, plus the waiting queue behind it.

    Behaviour is modelled on dunst's queues.c (read as raw source, not a
    summary), because dunst's popup-limiting and replace-by-tag are the reason
    it was chosen over swaync in the first place.
*/
#pragma once

#include "notification.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QList>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>

class NotificationModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /* Number queued behind the visible ones — the "+N more" indicator. */
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY hiddenCountChanged)
    Q_PROPERTY(bool doNotDisturb READ doNotDisturb WRITE setDoNotDisturb NOTIFY doNotDisturbChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AppNameRole,
        SummaryRole,
        BodyRole,
        IconSourceRole,
        UrgencyRole,
        ActionsRole,
        HasDefaultActionRole,
        GroupCountRole,
        GroupKeyRole,
        WhenRole,
        InlineReplyRole,
        ReplyPlaceholderRole,
        ReplySubmitTextRole,
        ProgressRole,
    };
    Q_ENUM(Roles)

    explicit NotificationModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int hiddenCount() const { return m_waiting.size(); }

    /* For the image://notifyimage provider — inline pixels live on the
       notification, not on disk. */
    ImageData imageFor(uint id) const;
    bool doNotDisturb() const { return m_dnd; }
    void setDoNotDisturb(bool dnd);

    /* Clamped at 0 (dunst's "unlimited"). A negative limit is an easy typo
       in a config file and would make effectiveLimit() negative, so update()
       would promote nothing and every notification would queue forever with
       no popup and no error. */
    /* Per-urgency dwell time in ms. 0 means never expire, which is what the
       spec mandates for critical. Apps that send their own expire_timeout
       still win; these are the fallback when they send -1. */
    void setTimeouts(int low, int normal, int critical)
    {
        m_timeoutLow = qMax(0, low);
        m_timeoutNormal = qMax(0, normal);
        m_timeoutCritical = qMax(0, critical);
    }

    void setLimit(int limit) { m_limit = qMax(0, limit); update(); }
    void setIndicateHidden(bool on) { m_indicateHidden = on; update(); }
    /* dunst's idle_threshold: while the session has been idle longer than
       this, nothing is allowed to expire. Stepping away for coffee should not
       mean returning to an empty screen. 0 disables it. */
    void setIdleThresholdMs(int ms);

    void setCoalesce(int threshold, int windowMs)
    {
        m_coalesceThreshold = qMax(0, threshold);   // 0 disables coalescing
        m_coalesceWindowMs = qMax(0, windowMs);
    }

    /* Returns the id actually used (may be the replaced one). */
    uint insert(Notification n);
    bool closeId(uint id, uint reason);

    Q_INVOKABLE void dismiss(uint id);                       // user clicked away
    /* Clear every popup on screen and everything queued behind them.
       A notification with expire_timeout 0, or critical urgency, is required
       by the spec to stay until acted on — correct, but it means a misbehaving
       app can pin a card indefinitely with no way out but a restart. */
    Q_INVOKABLE int dismissAll();
    /* Hovering pauses the dwell timer: a notification should not expire out
       from under someone who is visibly reading it. Off by request for people
       who would rather popups clear on schedule regardless. */
    Q_INVOKABLE void setHovered(uint id, bool hovered);
    void setHoverPause(bool on);
    bool hoverPause() const { return m_hoverPause; }
    Q_INVOKABLE void invokeAction(uint id, const QString &key);
    Q_INVOKABLE void sendReply(uint id, const QString &text);

    /* Activating a notification has to hand the sender an XDG activation
       token, otherwise focus-stealing prevention stops it raising its window
       and clicking the notification appears to do nothing. */
    Q_INVOKABLE void activate(QQuickWindow *window, uint id, const QString &key);

Q_SIGNALS:
    void hiddenCountChanged();
    void doNotDisturbChanged();
    void notificationClosed(uint id, uint reason);
    void actionInvoked(uint id, const QString &key);
    void replied(uint id, const QString &text);
    void activationToken(uint id, const QString &token);

private:
    void update();
    void expireTick();
    int indexOfDisplayed(uint id) const;

    /* Precedence, in dunst's order: an explicit replaces_id wins, then a stack
       tag, then plain insertion. */
    bool replaceById(const Notification &n);
    bool replaceByTag(const Notification &n);

    /* Per-app burst grouping. This is the capability neither dunst nor swaync
       has, and the actual fix for wake-spam: notification_limit only converts
       a burst into a drip, and a stack tag hides everything but the newest
       without saying how many there were. */
    bool coalesce(const Notification &n);

    int effectiveLimit() const;
    int defaultTimeoutFor(Urgency u) const;

    QList<Notification> m_displayed;
    QList<Notification> m_waiting;

    int m_limit = 4;
    int m_timeoutLow = 4000;
    int m_timeoutNormal = 6000;
    int m_timeoutCritical = 0;
    bool m_indicateHidden = true;
    bool m_dnd = false;
    int m_lastHiddenCount = 0;

    int m_idleThresholdMs = 0;
    int m_idleTimeoutId = -1;
    bool m_userIdle = false;
    int m_coalesceThreshold = 3;
    int m_coalesceWindowMs = 20000;
    QHash<QString, QList<QDateTime>> m_recentByGroup;
    QTimer m_tick;
    QSet<uint> m_hovered;
    bool m_hoverPause = true;
};
