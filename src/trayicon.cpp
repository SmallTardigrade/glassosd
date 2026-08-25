/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "trayicon.h"

#include <KLocalizedString>
#include <KStatusNotifierItem>

#include <QAction>
#include <QMenu>

TrayIcon::TrayIcon(NotificationModel *notifications, HistoryModel *history, QObject *parent)
    : QObject(parent)
    , m_notifications(notifications)
    , m_history(history)
{
    m_item = new KStatusNotifierItem(QStringLiteral("glassosd"), this);
    m_item->setCategory(KStatusNotifierItem::SystemServices);
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setTitle(i18n("Notifications"));

    /* Left click opens the centre; that is the common case, so it should not
       require going through a menu. */
    connect(m_item, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &) {
        m_history->togglePanel();
    });

    auto *menu = new QMenu();

    auto *dnd = menu->addAction(i18n("Do Not Disturb"));
    dnd->setCheckable(true);
    dnd->setChecked(m_notifications->doNotDisturb());
    connect(dnd, &QAction::triggered, this, [this](bool on) {
        m_notifications->setDoNotDisturb(on);
    });
    connect(m_notifications, &NotificationModel::doNotDisturbChanged, dnd, [this, dnd] {
        dnd->setChecked(m_notifications->doNotDisturb());
    });

    auto *open = menu->addAction(i18n("Open notification centre"));
    connect(open, &QAction::triggered, this, [this] {
        m_history->setPanelOpen(true);
    });

    menu->addSeparator();
    auto *clear = menu->addAction(i18n("Clear all"));
    connect(clear, &QAction::triggered, this, [this] {
        m_history->clearAll();
    });

    m_item->setContextMenu(menu);

    connect(m_notifications, &NotificationModel::doNotDisturbChanged, this, &TrayIcon::refresh);
    connect(m_history, &HistoryModel::changed, this, &TrayIcon::refresh);
    refresh();
}

void TrayIcon::refresh()
{
    const bool dnd = m_notifications->doNotDisturb();

    /* The icon itself carries the DND state, so it is legible at a glance
       without opening anything. */
    m_item->setIconByName(dnd ? QStringLiteral("notifications-disabled")
                              : QStringLiteral("notifications"));

    const int count = m_history->total();
    m_item->setToolTip(dnd ? QStringLiteral("notifications-disabled") : QStringLiteral("notifications"),
                       i18n("Notifications"),
                       dnd ? i18n("Do Not Disturb is on")
                           : (count > 0 ? i18np("%1 notification", "%1 notifications", count)
                                        : i18n("No notifications")));
}
