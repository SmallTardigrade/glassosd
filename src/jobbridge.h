/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Turns the file-operation jobs JobViewMonitor reports into progress cards.

    A job sends dozens of updates a second, so they are gathered and a card is
    refreshed a few times a second at most. A job that finishes before it has
    been running for a moment never gets a card at all: on a fast disk a copy
    is over before anyone could read it, and a card that flashes up and goes
    away is worse than none.

    The card is an ordinary notification carrying the "value" hint, replaced in
    place on every refresh, so it looks and behaves like any other progress
    notification: it stacks, honours Do Not Disturb and the rules, and does not
    go into history (it is transient — a copy that finished an hour ago is not
    something to scroll back to).

    If the user dismisses the card, that job stays dismissed. Replacing it
    again would put back something they have just asked not to see.
*/
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class NotificationServer;

class JobBridge : public QObject
{
    Q_OBJECT
public:
    explicit JobBridge(NotificationServer *server, QObject *parent = nullptr);

    /* A job must have run this long before it earns a card. */
    void setShowAfterMs(int ms) { m_showAfterMs = qMax(0, ms); }
    /* How often a visible card is refreshed. */
    void setRefreshMs(int ms) { m_timer.setInterval(qMax(50, ms)); }

    /* For the tests: whether a job currently has a card, and its id. */
    uint cardFor(const QString &path) const { return m_jobs.value(path).notificationId; }
    int jobCount() const { return m_jobs.size(); }

public Q_SLOTS:
    void jobStarted(const QString &path, const QString &appId);
    void jobUpdated(const QString &path, const QVariantMap &props);
    void jobEnded(const QString &path, quint32 code, const QString &message);
    /* Refresh every visible card that has changed. Driven by the timer. */
    void flush();

private Q_SLOTS:
    void notificationClosed(uint id, uint reason);

private:
    struct Job {
        QString appId;
        QVariantMap props;
        qint64 startedMs = 0;
        uint notificationId = 0;
        bool dirty = false;
        bool dismissed = false;
        /* The sender's speed figure jumps about; the estimate built on it would
           jump with it. An exponential average keeps the time-left readable. */
        double smoothSpeed = 0;
    };

    void present(Job &job, bool final, quint32 code = 0, const QString &message = QString());
    static QString runningSummary(const Job &job);
    static QString runningBody(const Job &job);
    static QString finishedSummary(const Job &job);
    static QString finishedBody(const Job &job);
    static int percentOf(const Job &job);
    /* Seconds until done, or -1 while it is too early to say. */
    static int secondsLeft(const Job &job);

    NotificationServer *m_server;
    QHash<QString, Job> m_jobs;
    QTimer m_timer;
    int m_showAfterMs = 700;
};
