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

    `Action` is one of the built-ins (toggle-dnd, toggle-wifi, toggle-bluetooth,
    toggle-power-saver, clear-all, lock, reboot, poweroff, logout); `Command`
    runs an arbitrary program. A button naming both
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
    /* Read straight off nmcli/rfkill each time the grid asks. Cheap enough —
       the panel is closed almost always, and a cached value would be wrong
       whenever the state changed anywhere else, which for a radio is most of
       the time. */
    static bool wifiOn();
    static bool bluetoothOn();
    static QString powerProfile();
    static void setPowerProfile(const QString &profile);
    static QString readProcess(const QString &program, const QStringList &args);

    KSharedConfig::Ptr m_config;
    std::vector<QuickButton> m_buttons;
    int m_perRow = 5;
    /* What to go back to when power saving is switched off again. Someone on
       "performance" who saves power for a while expects performance back, not
       "balanced" — and the profile daemon does not remember for us. Session
       lifetime only; on a fresh start there is nothing to restore and
       "balanced" is the safe answer. */
    mutable QString m_profileBeforeSaving;
    NotificationModel *m_notifications = nullptr;
    HistoryModel *m_history = nullptr;
};
