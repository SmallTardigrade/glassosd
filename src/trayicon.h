/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    System tray entry: a persistent way into the notification centre, and a
    visible indicator of Do Not Disturb.

    Until now the centre was reachable only by Meta+N — discoverable if you
    remember it, invisible if you do not.
*/
#pragma once

#include "historymodel.h"
#include "notificationmodel.h"

#include <QObject>

class KStatusNotifierItem;

class TrayIcon : public QObject
{
    Q_OBJECT
public:
    TrayIcon(NotificationModel *notifications, HistoryModel *history, QObject *parent = nullptr);

private:
    void refresh();

    KStatusNotifierItem *m_item = nullptr;
    NotificationModel *m_notifications;
    HistoryModel *m_history;
};
