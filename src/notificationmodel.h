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
    Q_PROPERTY(int exitMs READ exitMs WRITE setExitMs)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ClosingRole,
        StackTagRole,
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

    /* Applications asking for quiet, via the notification server's Inhibit
       method. Kept apart from Do Not Disturb rather than folded into it: one
       is the user's choice and the other is an application's request, and
       turning DND off should not silently cancel a screen recording's
       inhibition. Popups are suppressed while either is on. */
    bool inhibited() const { return m_inhibited; }
    void setInhibited(bool inhibited);
    /* Something is holding the screen awake — a video, a game, a
       presentation. A third reason, kept apart from the other two so that
       turning any one of them off does not cancel the others. */
    bool busyQuiet() const { return m_busyQuiet; }
    void setBusyQuiet(bool on);
    bool quiet() const { return m_dnd || m_inhibited || m_busyQuiet; }

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

    /* How long a card is kept on screen after it is closed, so it can animate
       away. Set from QML so the theme's motion.out stays the one place the
       duration lives; 0 removes it immediately, as before. */
    int exitMs() const { return m_exitMs; }
    void setExitMs(int ms) { m_exitMs = qBound(0, ms, 2000); }

    /* Put this one away and let it come back later. The notification itself
       is handed out rather than kept: persistence belongs to SnoozeStore, and
       a woken notification comes back through insert() like any other so it
       meets the rules, coalescing and the queue on the way in. */
    Q_INVOKABLE void snooze(uint id);
    Q_INVOKABLE void dismiss(uint id);                       // user clicked away
    /* Clear every popup on screen and everything queued behind them.
       A notification with expire_timeout 0, or critical urgency, is required
       by the spec to stay until acted on — correct, but it means a misbehaving
       app can pin a card indefinitely with no way out but a restart. */
    Q_INVOKABLE int dismissAll();
    /* Take the popups off screen because the centre is now showing the same
       notifications properly. Closed as Expired rather than Dismissed: the
       user opened a window, they did not act on any particular notification,
       and telling every sender otherwise would be a lie about intent. */
    int hideForCentre();
    /* Hovering pauses the dwell timer: a notification should not expire out
       from under someone who is visibly reading it. Off by request for people
       who would rather popups clear on schedule regardless. */
    Q_INVOKABLE void setHovered(uint id, bool hovered);
    void setHoverPause(bool on);
    bool hoverPause() const { return m_hoverPause; }
    Q_INVOKABLE void invokeAction(uint id, const QString &key);
    /* True while the notification is still displayed or queued, which is the
       only window in which its sender is still listening for an action. */
    bool isLive(uint id) const;
    Q_INVOKABLE void sendReply(uint id, const QString &text);

    /* Activating a notification has to hand the sender an XDG activation
       token, otherwise focus-stealing prevention stops it raising its window
       and clicking the notification appears to do nothing. */
    Q_INVOKABLE void activate(QQuickWindow *window, uint id, const QString &key);

Q_SIGNALS:
    void hiddenCountChanged();
    void doNotDisturbChanged();
    void inhibitedChanged();
    void notificationClosed(uint id, uint reason);
    void actionInvoked(uint id, const QString &key);
    void replied(uint id, const QString &text);
    void activationToken(uint id, const QString &token);
    /* Wired to SnoozeStore in main.cpp. */
    void snoozeRequested(const Notification &n);
    /* Wired to SoundPlayer in main.cpp. Emitted only for notifications that
       will actually be seen, so a muted app, Do Not Disturb or an inhibition
       silences the sound along with the popup — a notification nobody is shown
       should not announce itself. */
    void soundWanted(const QString &name);

private:
    void update();
    void expireTick();
    void finishClose(uint id);
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
    bool m_inhibited = false;
    bool m_busyQuiet = false;
    int m_lastHiddenCount = 0;

    int m_idleThresholdMs = 0;
    int m_idleTimeoutId = -1;
    bool m_userIdle = false;
    int m_exitMs = 0;   // set from QML; 0 keeps the old instant removal
    int m_coalesceThreshold = 3;
    int m_coalesceWindowMs = 20000;
    QHash<QString, QList<QDateTime>> m_recentByGroup;
    /* When each distinct piece of content was last shown, for rules carrying
       repeat_window. Keyed on app + summary + body, so "9% remaining" and
       "6% remaining" are different content and each gets its own popup. */
    QHash<QString, QDateTime> m_lastShownByContent;
    QTimer m_tick;
    QSet<uint> m_hovered;
    bool m_hoverPause = true;
};
