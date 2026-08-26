/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Past notifications.

    Everything is recorded on arrival, before the queue decides what merges
    into what. That ordering is the point: coalescing shows one card for a
    burst of twelve, and the other eleven have to remain individually readable
    somewhere or the count is just a number that lost its contents.
*/
#pragma once

#include "notification.h"

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QQmlEngine>
#include <QTimer>

class HistoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int total READ total NOTIFY changed)
    /* Arrived since the centre was last opened. total() is the whole backlog
       — up to HistoryLength — so it says nothing about whether there is
       anything new, which is the only question a tray badge answers. */
    Q_PROPERTY(int unread READ unread NOTIFY changed)
    /* Set to a group key to show only that app's entries — this is what the
       "+N more from <App>" line opens into. Empty shows everything. */
    Q_PROPERTY(QString groupFilter READ groupFilter WRITE setGroupFilter NOTIFY changed)
    Q_PROPERTY(QString groupFilterLabel READ groupFilterLabel NOTIFY changed)
    /* Whether the drill-in should also show the per-app settings panel. */
    Q_PROPERTY(bool appSettingsVisible READ appSettingsVisible NOTIFY changed)
    Q_PROPERTY(bool panelOpen READ panelOpen WRITE setPanelOpen NOTIFY panelOpenChanged)
    Q_PROPERTY(bool newestFirst READ newestFirst NOTIFY changed)

public:
    /* The view is a flat list of rows where a row is either a group header or
       an entry. Building it here rather than using ListView sections is what
       makes collapse possible: a collapsed group emits its header and none of
       its entries, which section delegates cannot express. */
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AppNameRole,
        SummaryRole,
        BodyRole,
        IconSourceRole,
        UrgencyRole,
        WhenRole,
        IsHeaderRole,
        GroupKeyRole,
        GroupCountRole,
        CollapsedRole,
        HeaderIconRole,
    };
    Q_ENUM(Roles)

    explicit HistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int total() const { return m_all.size(); }
    int unread() const { return m_unread; }
    bool panelOpen() const { return m_panelOpen; }
    void setPanelOpen(bool open);
    Q_INVOKABLE void togglePanel() { setPanelOpen(!m_panelOpen); }

    /* false (the default) puts the newest at the bottom so the centre can
       open already scrolled to it. */
    /* The highest id present in the persisted file. Notification ids restart
       from 1 on every launch, so without seeding the server above this a new
       notification collides with an unrelated old entry and overwrites it. */
    uint maxLoadedId() const { return m_maxLoadedId; }

    void setNewestFirst(bool on);
    bool newestFirst() const { return m_newestFirst; }
    QString groupFilter() const { return m_filter; }
    QString groupFilterLabel() const { return m_filterLabel; }
    void setGroupFilter(const QString &key);

    void record(const Notification &n);

    /* Persisted to disk so history survives a restart — dunst keeps history
       across reloads and losing everything on a daemon restart is the single
       most annoying gap we had. */
    void load();
    void save() const;
    void setCapacity(int n);
    ImageData imageFor(uint id) const;

    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void clearFilter() { setGroupFilter({}); }

    /* Open the centre on one app's notifications.

       withSettings decides which question is being asked. The gear asks "how
       should this app behave"; "N more notifications" asks "what were the
       other four" — and answering the second with the settings panel, folded,
       is why that button felt like it went somewhere else entirely. */
    Q_INVOKABLE void showGroup(const QString &key, bool withSettings);
    bool appSettingsVisible() const { return m_appSettingsVisible; }
    /* Clicking an entry in the centre. Tries the original notification's
       default action first — that only lands if the notification is still
       live — and otherwise launches the application it came from, which is
       what the click was almost certainly for. */
    Q_INVOKABLE void activateEntry(int row);

    Q_INVOKABLE void toggleGroup(const QString &key);
    Q_INVOKABLE void clearGroup(const QString &key);
    void setAutoCollapseOver(int n) { m_autoCollapseOver = qMax(0, n); rebuild(); }

    /* Re-read the per-app "always collapsed" rules from glassosdrc. Called
       whenever the settings panel or glassosdctl writes a rule. Only this one
       rule action is a display property rather than a property of an incoming
       notification, so it is read here rather than through Rules::apply. */
    void reloadRules();

private:
    /* appName, not groupKey: the rules engine matches on appname, and the
       settings panel writes the rule under the app's name. */
    bool isCollapsed(const QString &key, const QString &appName, int count) const;

public:

Q_SIGNALS:
    void changed();
    void panelOpenChanged();
    /* Handled in main.cpp: the server knows whether the id is still live. */
    void entryActivated(uint id, const QString &desktopEntry);

private:
    void rebuild();

    QList<Notification> m_all;      // newest first
    struct Row {
        bool header = false;
        QString groupKey;
        QString appName;
        QString icon;
        int count = 0;
        bool collapsed = false;
        Notification entry;
    };
    QList<Row> m_rows;
    QString m_filter;
    QString m_filterLabel;
    int m_capacity = 200;
    QString storePath() const;
    mutable QTimer m_saveTimer;
    bool m_panelOpen = false;
    bool m_newestFirst = true;
    bool m_appSettingsVisible = false;
    int m_unread = 0;
    uint m_maxLoadedId = 0;
    /* Two sets, not one: a group is collapsed by default once it grows past
       the threshold, but an explicit expand has to survive rebuilds — and an
       explicit collapse has to survive a group shrinking below it. */
    QSet<QString> m_collapsed;   // explicitly collapsed by the user
    QSet<QString> m_expanded;    // explicitly expanded by the user
    /* App names carrying always_collapsed. Cached rather than read per group
       per rebuild, and refreshed by reloadRules(). */
    QSet<QString> m_alwaysCollapsed;
    int m_autoCollapseOver = 3;
};
