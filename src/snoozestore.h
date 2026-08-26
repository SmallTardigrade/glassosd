/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Snoozed notifications: put away now, shown again later.

    Neither dunst nor swaync has this in any form, which is odd — everybody has
    the reflex from their phone. A snoozed notification is closed like any
    other, kept here with the time it should come back, and re-inserted into
    the model when that time arrives.

    Persisted, because a wake time that only survives while the daemon happens
    to keep running is not a promise worth making: a snooze set before a logout
    has to still be there afterwards. Anything already due when the file is
    read comes straight back, which is the honest answer for a machine that was
    asleep or shut down through the wake time.
*/
#pragma once

#include "notification.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVector>

class SnoozeStore : public QObject
{
    Q_OBJECT
public:
    explicit SnoozeStore(QObject *parent = nullptr);

    /* Minutes is read from config by the caller so this class does not need to
       know the config exists. */
    void snooze(const Notification &n, int minutes);
    int count() const { return int(m_items.size()); }

    /* Bring everything back now, whatever it was waiting for. */
    int wakeAll();

Q_SIGNALS:
    /* Connected to NotificationModel::insert in main.cpp: a woken notification
       arrives exactly like a new one rather than through a second path that
       would have to re-implement rules, coalescing and the queue. */
    void woke(const Notification &n);
    void changed();

private:
    struct Item {
        Notification n;
        QDateTime wakeAt;
    };

    void load();
    void save() const;
    void rearm();
    void fire();
    QString storePath() const;

    QVector<Item> m_items;
    QTimer m_timer;
};
