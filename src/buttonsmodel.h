/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    The quick-action grid in the notification centre — swaync's buttons-grid.

    Configured as [Button <name>] groups in glassosdrc, matching the shape of
    the existing [Rule <name>] groups rather than inventing a second config
    format for the same job:

        [Button wifi]
        Icon=wifi
        Command=kitty -e nmtui
        Tooltip=Network
        Order=0

        [Button dnd]
        Icon=dnd
        Action=toggle-dnd          # built-in, needs no external program
        Order=2

    `Action` is one of the built-ins (toggle-dnd, clear-all, lock, reboot,
    poweroff); `Command` runs an arbitrary program. A button naming both
    prefers the built-in, because a built-in cannot be mis-typed into something
    that silently does nothing.
*/
#pragma once

#include <KSharedConfig>
#include <QAbstractListModel>
#include <QQmlEngine>

class NotificationModel;
class HistoryModel;

struct QuickButton {
    QString name;
    QString icon;
    QString label;
    QString tooltip;
    QString command;
    QString action;
    int order = 0;
};

class ButtonsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int perRow READ perRow NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconRole,
        LabelRole,
        TooltipRole,
        ToggleRole,   // renders as a state toggle rather than a plain button
        ActiveRole,   // current state, for toggles
    };

    explicit ButtonsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override { return int(m_buttons.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int perRow() const { return m_perRow; }

    /* Bound so toggles can report and change live state without the QML
       needing to know which built-ins are stateful. */
    void setTargets(NotificationModel *notifications, HistoryModel *history);

    Q_INVOKABLE void activate(int row);

    void load();

Q_SIGNALS:
    void changed();

private:
    void runCommand(const QString &command) const;

    KSharedConfig::Ptr m_config;
    std::vector<QuickButton> m_buttons;
    int m_perRow = 5;
    NotificationModel *m_notifications = nullptr;
    HistoryModel *m_history = nullptr;
};
