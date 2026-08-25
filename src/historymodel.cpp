/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "historymodel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

HistoryModel::HistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    /* Debounced: a burst of thirty notifications should produce one write,
       not thirty. */
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
        save();
    });
    load();
}

QString HistoryModel::storePath() const
{
    /* GenericDataLocation + an explicit name, because AppDataLocation nests
       organisation *and* application and both are "glassosd", which produced
       ~/.local/share/glassosd/glassosd/. */
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/glassosd");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/history.json");
}

void HistoryModel::setCapacity(int n)
{
    m_capacity = qMax(1, n);
    while (m_all.size() > m_capacity) {
        m_all.removeLast();
    }
    rebuild();
}

void HistoryModel::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    m_all.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Notification n;
        n.id = o.value(QStringLiteral("id")).toInt();
        n.appName = o.value(QStringLiteral("app")).toString();
        n.desktopEntry = o.value(QStringLiteral("desktopEntry")).toString();
        n.appIcon = o.value(QStringLiteral("icon")).toString();
        n.summary = o.value(QStringLiteral("summary")).toString();
        n.body = o.value(QStringLiteral("body")).toString();
        n.urgency = static_cast<Urgency>(o.value(QStringLiteral("urgency")).toInt(1));
        n.received = QDateTime::fromString(o.value(QStringLiteral("at")).toString(), Qt::ISODate);
        m_all.append(n);
        if (m_all.size() >= m_capacity) {
            break;
        }
    }
    rebuild();
}

void HistoryModel::save() const
{
    QJsonArray arr;
    for (const Notification &n : m_all) {
        QJsonObject o;
        o[QStringLiteral("id")] = int(n.id);
        o[QStringLiteral("app")] = n.appName;
        o[QStringLiteral("desktopEntry")] = n.desktopEntry;
        o[QStringLiteral("icon")] = n.appIcon;
        o[QStringLiteral("summary")] = n.summary;
        o[QStringLiteral("body")] = n.body;
        o[QStringLiteral("urgency")] = int(n.urgency);
        o[QStringLiteral("at")] = n.received.toString(Qt::ISODate);
        /* Inline image-data is deliberately not persisted: it is raw pixels
           and would bloat the file badly. Such entries fall back to the app
           icon after a restart. */
        arr.append(o);
    }
    QFile f(storePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &r = m_rows.at(index.row());

    switch (role) {
    case IsHeaderRole:   return r.header;
    case GroupKeyRole:   return r.groupKey;
    case GroupCountRole: return r.count;
    case CollapsedRole:  return r.collapsed;
    case AppNameRole:    return r.header ? r.appName : r.entry.appName;
    default:
        break;
    }
    if (r.header) {
        return {};
    }

    const Notification &n = r.entry;
    switch (role) {
    case IdRole:      return n.id;
    case SummaryRole: return n.summary;
    case BodyRole:    return n.body;
    case UrgencyRole: return static_cast<int>(n.urgency);
    case WhenRole:    return n.received.toLocalTime().toString(QStringLiteral("HH:mm"));
    case IconSourceRole:
        if (n.image.isValid()) {
            return QStringLiteral("image://notifyimage/h%1").arg(n.id);
        }
        return n.appIcon.isEmpty() ? QString() : QStringLiteral("image://icon/") + n.appIcon;
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {
        {IdRole, "histId"},
        {AppNameRole, "appName"},
        {SummaryRole, "summary"},
        {BodyRole, "body"},
        {IconSourceRole, "iconSource"},
        {UrgencyRole, "urgency"},
        {WhenRole, "when"},
        {IsHeaderRole, "isHeader"},
        {GroupKeyRole, "groupKey"},
        {GroupCountRole, "groupCount"},
        {CollapsedRole, "collapsed"},
    };
}

void HistoryModel::record(const Notification &n)
{
    if (n.historyIgnore) {
        return;
    }

    /* A replaces_id notification is the *same* message updating itself — a
       file-copy progress notification would otherwise leave a hundred entries
       behind. Update in place instead of appending. */
    for (int i = 0; i < m_all.size(); ++i) {
        if (m_all.at(i).id == n.id) {
            m_all[i] = n;
            rebuild();
            return;
        }
    }

    m_all.prepend(n);
    while (m_all.size() > m_capacity) {
        m_all.removeLast();
    }
    rebuild();
}

ImageData HistoryModel::imageFor(uint id) const
{
    for (const Notification &n : m_all) {
        if (n.id == id) {
            return n.image;
        }
    }
    return {};
}

void HistoryModel::setGroupFilter(const QString &key)
{
    if (m_filter == key) {
        return;
    }
    m_filter = key;
    rebuild();   // rebuild() resolves the label, so it stays correct as entries arrive
}

void HistoryModel::setPanelOpen(bool open)
{
    if (m_panelOpen == open) {
        return;
    }
    m_panelOpen = open;
    if (!open) {
        /* Reopening should show everything, not silently still be filtered to
           whichever app was drilled into last time. */
        setGroupFilter({});
    }
    Q_EMIT panelOpenChanged();
}

void HistoryModel::rebuild()
{
    beginResetModel();
    m_rows.clear();

    /* Resolved here rather than when the filter is set: a filter can be
       applied before any matching notification exists, and the label would
       then stay empty forever. */
    m_filterLabel.clear();
    if (!m_filter.isEmpty()) {
        for (const Notification &n : std::as_const(m_all)) {
            if (n.groupKey() == m_filter) {
                m_filterLabel = n.appName;
                break;
            }
        }
    }

    /* Groups are ordered by their most recent member, and members keep their
       own order, so the newest thing is always at the top whichever group it
       belongs to. */
    QStringList order;
    QHash<QString, QList<Notification>> byGroup;
    for (const Notification &n : std::as_const(m_all)) {
        if (!m_filter.isEmpty() && n.groupKey() != m_filter) {
            continue;
        }
        const QString key = n.groupKey();
        if (!byGroup.contains(key)) {
            order.append(key);
        }
        byGroup[key].append(n);
    }

    for (const QString &key : std::as_const(order)) {
        const QList<Notification> &items = byGroup.value(key);
        const bool collapsed = isCollapsed(key, items.size());

        Row header;
        header.header = true;
        header.groupKey = key;
        header.appName = items.first().appName;
        header.count = items.size();
        header.collapsed = collapsed;
        m_rows.append(header);

        if (collapsed) {
            continue;
        }
        for (const Notification &n : items) {
            Row row;
            row.groupKey = key;
            row.entry = n;
            m_rows.append(row);
        }
    }

    endResetModel();
    Q_EMIT changed();
    m_saveTimer.start();
}

bool HistoryModel::isCollapsed(const QString &key, int count) const
{
    if (m_expanded.contains(key)) {
        return false;
    }
    if (m_collapsed.contains(key)) {
        return true;
    }
    /* Long groups start folded so a single noisy app cannot push everything
       else off the screen. */
    return m_autoCollapseOver > 0 && count > m_autoCollapseOver;
}

void HistoryModel::toggleGroup(const QString &key)
{
    int count = 0;
    for (const Row &r : std::as_const(m_rows)) {
        if (r.header && r.groupKey == key) {
            count = r.count;
            break;
        }
    }
    if (isCollapsed(key, count)) {
        m_expanded.insert(key);
        m_collapsed.remove(key);
    } else {
        m_collapsed.insert(key);
        m_expanded.remove(key);
    }
    rebuild();
}

void HistoryModel::clearGroup(const QString &key)
{
    m_all.removeIf([&key](const Notification &n) {
        return n.groupKey() == key;
    });
    m_collapsed.remove(key);
    m_expanded.remove(key);
    rebuild();
}

void HistoryModel::removeAt(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).header) {
        return;
    }
    const uint id = m_rows.at(row).entry.id;
    m_all.removeIf([id](const Notification &n) {
        return n.id == id;
    });
    rebuild();
}

void HistoryModel::clearAll()
{
    /* Clears what is on screen. With a filter active that means only the
       filtered app, which is what the button appears to promise. */
    if (m_filter.isEmpty()) {
        m_all.clear();
    } else {
        m_all.removeIf([this](const Notification &n) {
            return n.groupKey() == m_filter;
        });
    }
    rebuild();
}
