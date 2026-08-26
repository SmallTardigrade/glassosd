/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "trayicon.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KStatusNotifierItem>

#include <QAction>
#include <QMenu>

TrayIcon::TrayIcon(NotificationModel *notifications, HistoryModel *history, QObject *parent)
    : QObject(parent)
    , m_notifications(notifications)
    , m_history(history)
    , m_config(KSharedConfig::openConfig(QStringLiteral("glassosdrc")))
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

    m_config->reparseConfiguration();
    const bool badge = KConfigGroup(m_config, QStringLiteral("Appearance"))
                           .readEntry("TrayBadge", QStringLiteral("on")).trimmed().toLower()
                       != QLatin1String("off");

    const int unread = dnd ? 0 : m_history->unread();

    /* Three states, all named icons from the theme.

       The badge was painted at first — a red disc, composed into a pixmap and
       handed over with setOverlayIconByPixmap(). It arrives at this host as a
       red *square*: the alpha does not survive the trip, at any size, and
       adding pixmaps at every size a panel might ask for changed nothing.
       setOverlayIconByName() then rendered nothing at all.

       notification-active is Breeze's own bell-with-a-dot and goes through the
       icon theme at the far end, where it is drawn rather than transported. It
       also means the badge follows whatever icon theme is in use instead of
       being a hardcoded red that clashes with half of them.

       The cost is that a *count* cannot be shown — no icon theme ships
       numerals — so the number lives in the tooltip. */
    const QString iconName = dnd      ? QStringLiteral("notifications-disabled")
                           : (badge && unread > 0) ? QStringLiteral("notification-active")
                                                   : QStringLiteral("notifications");
    m_item->setIconByName(iconName);

    const int count = m_history->total();
    m_item->setToolTip(iconName,
                       i18n("Notifications"),
                       dnd ? i18n("Do Not Disturb is on")
                           : (unread > 0 ? i18np("%1 new notification", "%1 new notifications", unread)
                              : count > 0 ? i18np("%1 notification", "%1 notifications", count)
                                          : i18n("No notifications")));
}
