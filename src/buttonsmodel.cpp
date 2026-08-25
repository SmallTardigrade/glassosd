/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "buttonsmodel.h"

#include "historymodel.h"
#include "notificationmodel.h"

#include <KConfigGroup>

#include <QDebug>
#include <QProcess>

ButtonsModel::ButtonsModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("glassosdrc")))
{
    load();
}

void ButtonsModel::setTargets(NotificationModel *notifications, HistoryModel *history)
{
    m_notifications = notifications;
    m_history = history;
    if (m_notifications) {
        /* A DND toggle that does not track DND changed elsewhere is worse
           than no toggle: it shows the opposite of the truth. */
        connect(m_notifications, &NotificationModel::doNotDisturbChanged, this, [this] {
            if (!m_buttons.empty()) {
                Q_EMIT dataChanged(index(0), index(int(m_buttons.size()) - 1), {ActiveRole});
            }
        });
    }
}

void ButtonsModel::load()
{
    beginResetModel();
    m_buttons.clear();
    m_config->reparseConfiguration();

    m_perRow = qBound(1, KConfigGroup(m_config, QStringLiteral("Appearance"))
                             .readEntry("ButtonsPerRow", 5), 10);

    const QStringList groups = m_config->groupList();
    for (const QString &g : groups) {
        if (!g.startsWith(QLatin1String("Button "))) {
            continue;
        }
        const KConfigGroup cg(m_config, g);
        QuickButton b;
        b.name = g.mid(7).trimmed();
        b.icon = cg.readEntry("Icon", QString());
        b.label = cg.readEntry("Label", QString());
        b.tooltip = cg.readEntry("Tooltip", b.name);
        b.command = cg.readEntry("Command", QString());
        b.action = cg.readEntry("Action", QString());
        b.order = cg.readEntry("Order", 100);

        if (b.icon.isEmpty() && b.label.isEmpty()) {
            qWarning("glassosd: [%s] has neither Icon nor Label; skipping",
                     qUtf8Printable(g));
            continue;
        }
        if (b.command.isEmpty() && b.action.isEmpty()) {
            qWarning("glassosd: [%s] has neither Command nor Action; skipping",
                     qUtf8Printable(g));
            continue;
        }
        m_buttons.push_back(b);
    }

    std::stable_sort(m_buttons.begin(), m_buttons.end(),
                     [](const QuickButton &a, const QuickButton &b) {
                         return a.order < b.order;
                     });

    endResetModel();
    Q_EMIT changed();
}

QHash<int, QByteArray> ButtonsModel::roleNames() const
{
    return {
        {NameRole, "name"},   {IconRole, "iconName"}, {LabelRole, "label"},
        {TooltipRole, "tooltip"}, {ToggleRole, "isToggle"}, {ActiveRole, "active"},
    };
}

QVariant ButtonsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= int(m_buttons.size())) {
        return {};
    }
    const QuickButton &b = m_buttons[index.row()];
    switch (role) {
    case NameRole:
        return b.name;
    case IconRole:
        return b.icon;
    case LabelRole:
        return b.label;
    case TooltipRole:
        return b.tooltip;
    case ToggleRole:
        return b.action == QLatin1String("toggle-dnd");
    case ActiveRole:
        if (b.action == QLatin1String("toggle-dnd") && m_notifications) {
            return m_notifications->doNotDisturb();
        }
        return false;
    default:
        return {};
    }
}

void ButtonsModel::runCommand(const QString &command) const
{
    /* startDetached with a shell, because the whole point of Command is that
       people write shell in it — pipes, quoting, `kitty -e sh -c '…'`. The
       daemon must not wait on it or block on its output. */
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"),
                                 {QStringLiteral("-c"), command})) {
        qWarning("glassosd: could not run button command: %s", qUtf8Printable(command));
    }
}

void ButtonsModel::activate(int row)
{
    if (row < 0 || row >= int(m_buttons.size())) {
        return;
    }
    const QuickButton &b = m_buttons[row];

    if (!b.action.isEmpty()) {
        if (b.action == QLatin1String("toggle-dnd")) {
            if (m_notifications) {
                m_notifications->setDoNotDisturb(!m_notifications->doNotDisturb());
            }
        } else if (b.action == QLatin1String("clear-all")) {
            if (m_history) {
                m_history->clearAll();
            }
        } else if (b.action == QLatin1String("lock")) {
            /* No single lock command exists across compositors. Try the
               common ones in order rather than hardcoding one and being
               wrong everywhere else. */
            runCommand(QStringLiteral(
                "loginctl lock-session 2>/dev/null || hyprlock 2>/dev/null || "
                "swaylock 2>/dev/null || qdbus6 org.kde.screensaver /ScreenSaver Lock"));
        } else if (b.action == QLatin1String("reboot")) {
            runCommand(QStringLiteral("systemctl reboot"));
        } else if (b.action == QLatin1String("poweroff")) {
            runCommand(QStringLiteral("systemctl poweroff"));
        } else if (b.action == QLatin1String("logout")) {
            runCommand(QStringLiteral("loginctl terminate-session \"$XDG_SESSION_ID\""));
        } else {
            qWarning("glassosd: unknown Action '%s' on button '%s'",
                     qUtf8Printable(b.action), qUtf8Printable(b.name));
        }
        return;
    }

    runCommand(b.command);
}
