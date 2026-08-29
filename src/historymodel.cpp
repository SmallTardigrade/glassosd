/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "historymodel.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QDateTime>

#include <algorithm>
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

    /* Capacity before load, and read here rather than waited for.

       load() truncates to m_capacity as it reads, and this is a QML singleton
       the engine constructs, so main.cpp cannot hand a capacity to the
       constructor — it can only call setCapacity() afterwards, by which point
       the file has already been read and cut short. Anyone raising
       HistoryLength above the 200 default therefore kept the larger history
       for the session and silently lost everything past 200 on the next
       start, while glassosdctl status went on reporting the number they
       asked for.

       setCapacity() still exists for live changes; this only makes sure the
       first read already knows the answer. */
    m_capacity = qMax(1, KSharedConfig::openConfig(QStringLiteral("glassosdrc"))
                             ->group(QStringLiteral("Notifications"))
                             .readEntry("HistoryLength", 200));
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
        n.repeatCount = o.value(QStringLiteral("repeats")).toInt(1);
        m_maxLoadedId = qMax(m_maxLoadedId, n.id);
        m_all.append(n);
        if (m_all.size() >= m_capacity) {
            break;
        }
    }

    /* Sort by arrival rather than trusting the file's order. Two reasons:
       a file written by an older build can be out of order (ids used to
       collide across restarts, so a new notification would overwrite an
       unrelated entry *in that entry's position*), and sorting is the only
       thing that repairs such a file. It also means the on-disk order is not
       load-bearing, so a hand-edited history still comes back sane. */
    std::stable_sort(m_all.begin(), m_all.end(),
                     [](const Notification &a, const Notification &b) {
                         return a.received > b.received;
                     });

    /* Collapse identical entries a previous build wrote as separate rows.

       record() merges on arrival, but a file written before it did carries the
       repeats one row each — 158 copies of the same low-battery warning here.
       Nothing else would ever fold those together, since only a *new* arrival
       triggers a merge. Doing it on load is the same reasoning as the sort
       above: the on-disk format is not load-bearing, and an old or hand-edited
       file should come back sane.

       Newest wins the position, and the counts add up. */
    QHash<QString, int> firstAt;
    for (int i = 0; i < m_all.size(); ) {
        const Notification &n = m_all.at(i);
        const QString key = n.appName + QLatin1Char('\x1f')
                          + n.summary + QLatin1Char('\x1f') + n.body;
        const auto seen = firstAt.constFind(key);
        if (seen == firstAt.constEnd()) {
            firstAt.insert(key, i);
            ++i;
            continue;
        }
        m_all[*seen].repeatCount += n.repeatCount;
        m_all.removeAt(i);   // indices after i shift down, but *seen < i so it stands
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
        if (n.repeatCount > 1) {
            o[QStringLiteral("repeats")] = n.repeatCount;
        }
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
    /* Must be answered here: header rows return early below, so a case in
       the entry switch is unreachable for exactly the rows that need it. */
    case HeaderIconRole:
        return r.icon.isEmpty() ? QString() : QStringLiteral("image://icon/") + r.icon;
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
    case RepeatCountRole: return n.repeatCount;
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
        {HeaderIconRole, "headerIcon"},
        {UrgencyRole, "urgency"},
        {WhenRole, "when"},
        {RepeatCountRole, "repeatCount"},
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

    /* Not counted while the centre is open — you are looking at it. */
    if (!m_panelOpen) {
        ++m_unread;
    }

    /* A replaces_id notification is the *same* message updating itself — a
       file-copy progress notification would otherwise leave a hundred entries
       behind. Update in place instead of prepending.

       The app name has to match as well as the id. Ids are only unique within
       one run of the daemon, but history outlives restarts, so id alone let a
       fresh notification overwrite an unrelated old entry — silently, and in
       that entry's old position, which is what put the file out of time
       order. Seeding the server's id counter past maxLoadedId() prevents the
       collision; this check is the belt to that pair of braces. */
    for (int i = 0; i < m_all.size(); ++i) {
        if (m_all.at(i).id == n.id && m_all.at(i).appName == n.appName) {
            m_all[i] = n;
            rebuild();
            return;
        }
    }

    /* A byte-identical repeat becomes a count on the existing entry rather
       than another row.

       PowerDevil re-sends the same "Device Battery Low (6% Remaining)" once or
       twice a second for as long as the pen is low. repeat_window keeps that
       off the screen, but every copy still reached history, and 129 identical
       rows under one group header is the same spam in a different place.

       Matched on the text rather than on a window, because there is no honest
       window here: the interesting question is "have I already been told this",
       and the answer does not expire after five minutes. It is moved back to
       the front, since it did just happen again — history is ordered by when
       something last occurred, not by when it first did.

       Only exact matches merge. A changed percentage is a different message
       and gets its own row. */
    for (int i = 0; i < m_all.size(); ++i) {
        const Notification &old = m_all.at(i);
        if (old.appName == n.appName && old.summary == n.summary
            && old.body == n.body) {
            Notification merged = n;
            merged.repeatCount = old.repeatCount + 1;
            if (merged.appIcon.isEmpty() && !merged.image.isValid()) {
                merged.appIcon = old.appIcon;
            }
            m_all.removeAt(i);
            m_all.prepend(merged);
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

bool HistoryModel::matchesSearch(const Notification &n) const
{
    if (m_search.isEmpty()) {
        return true;
    }
    /* App, summary and body — the three things somebody actually remembers
       about a notification they are trying to find again. Case-insensitive
       because nobody recalls the capitalisation of a subject line. */
    return n.appName.contains(m_search, Qt::CaseInsensitive)
        || n.summary.contains(m_search, Qt::CaseInsensitive)
        || n.body.contains(m_search, Qt::CaseInsensitive);
}

void HistoryModel::setSearch(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (m_search == trimmed) {
        return;
    }
    m_search = trimmed;
    /* Searching is a whole-history question, so drilling into one app and then
       typing would otherwise search only that app while looking like it
       searched everything. */
    if (!m_search.isEmpty()) {
        m_filter.clear();
    }
    rebuild();
}

void HistoryModel::setGroupFilter(const QString &key)
{
    if (m_filter == key) {
        return;
    }
    m_filter = key;
    rebuild();   // rebuild() resolves the label, so it stays correct as entries arrive
}

void HistoryModel::showGroup(const QString &key, bool withSettings)
{
    if (key.isEmpty()) {
        return;
    }
    m_appSettingsVisible = withSettings;

    /* Expanded, always. "Show more" that lands you on a collapsed header is
       asking you to click twice to do the thing you already asked for, and
       auto-collapse or an always_collapsed rule would otherwise fold exactly
       the group you opened deliberately. */
    m_expanded.insert(key);
    m_collapsed.remove(key);

    m_filter = key;
    rebuild();            // resolves the label and applies the expand
    setPanelOpen(true);
    Q_EMIT changed();
}

void HistoryModel::setPanelOpen(bool open)
{
    if (m_panelOpen == open) {
        return;
    }
    m_panelOpen = open;
    if (open) {
        m_unread = 0;
        /* changed(), not just panelOpenChanged(): the tray listens to the
           former, and without this the badge stayed up after the centre had
           been opened and closed again. */
        Q_EMIT changed();
    }
    if (!open) {
        /* Reopening should show everything, not silently still be filtered to
           whichever app was drilled into last time, and not still showing a
           settings panel opened three drill-ins ago. */
        setGroupFilter({});
        m_search.clear();
        m_appSettingsVisible = false;

        /* "Always collapsed" has to mean every time the panel opens, not just
           the first. Expanding one of these groups is a look inside, not a
           change of setting, so the expand is dropped on close. Groups without
           the rule keep their expand — there the user is overriding an
           automatic guess, and re-folding it each time would be a nuisance. */
        if (!m_alwaysCollapsed.isEmpty()) {
            for (const Notification &n : std::as_const(m_all)) {
                if (m_alwaysCollapsed.contains(n.appName)) {
                    m_expanded.remove(n.groupKey());
                }
            }
        }
    }
    Q_EMIT panelOpenChanged();
}

void HistoryModel::setNewestFirst(bool on)
{
    if (m_newestFirst == on) {
        return;
    }
    m_newestFirst = on;
    rebuild();
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
    m_matchCount = 0;
    for (const Notification &n : std::as_const(m_all)) {
        if (!m_filter.isEmpty() && n.groupKey() != m_filter) {
            continue;
        }
        if (!matchesSearch(n)) {
            continue;
        }
        ++m_matchCount;
        const QString key = n.groupKey();
        if (!byGroup.contains(key)) {
            order.append(key);
        }
        byGroup[key].append(n);
    }

    /* Newest-last is the default because the centre opens scrolled to the
       bottom, the way a message thread does: the thing you just heard arrive
       is under your eyes without scrolling. Groups reverse and so do their
       members, but the header still leads its own group — reversing the flat
       row list instead would put every header underneath its entries. */
    if (!m_newestFirst) {
        std::reverse(order.begin(), order.end());
        for (auto it = byGroup.begin(); it != byGroup.end(); ++it) {
            std::reverse(it.value().begin(), it.value().end());
        }
    }

    for (const QString &key : std::as_const(order)) {
        const QList<Notification> &items = byGroup.value(key);
        const bool collapsed = isCollapsed(key, items.first().appName, items.size());

        /* The newest member of the group. items are in display order, which
           is oldest-first when the newest is being shown at the bottom. */
        const Notification &newest = m_newestFirst ? items.first() : items.last();

        /* Occurrences, not rows. Since identical repeats collapse onto one
           entry, a group can be a single row that stands for 156 arrivals, and
           the two numbers stopped meaning the same thing. */
        int occurrences = 0;
        for (const Notification &it : items) {
            occurrences += qMax(1, it.repeatCount);
        }

        /* A group of one gets no header. The header exists to say "these N
           belong together and here is how to collapse them"; with a single
           entry it says nothing the entry does not already say, and a column
           of one-item headers is most of the panel's vertical space spent on
           repeating each app's name directly above itself. The entry still
           carries the app icon, so nothing is lost.

           One arrival, though, not one row. A deduplicated row is a group of
           one by row count while standing for many notifications, and dropping
           its header left it looking like a member of whatever group happened
           to be above it — a x156 battery warning reading as part of the app
           listed overhead. If it represents more than one arrival it keeps its
           header and its own name. */
        if (items.size() == 1 && occurrences == 1) {
            Row row;
            row.groupKey = key;
            row.entry = items.first();
            m_rows.append(row);
            continue;
        }

        Row header;
        header.header = true;
        header.groupKey = key;
        header.appName = newest.appName;          // same app throughout the group
        header.icon = newest.appIcon;             // so a collapsed group is scannable
        /* The number of notifications, which is what the user counts — not
           the number of rows we chose to draw them in. */
        header.count = occurrences;
        header.collapsed = collapsed;
        m_rows.append(header);

        /* A collapsed group still shows its newest entry rather than nothing.
           Hiding every entry turned the centre into a list of app names with
           no content — a table of contents, not history. Showing the latest
           one keeps a busy day readable without making a quiet one look
           collapsed at all, which is what macOS does with a notification
           stack. The header's count and chevron still reveal the rest. */
        if (collapsed) {
            Row row;
            row.groupKey = key;
            row.entry = newest;
            m_rows.append(row);
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

bool HistoryModel::isCollapsed(const QString &key, const QString &appName, int count) const
{
    /* An explicit expand still wins over always_collapsed — the rule sets how
       the group *opens*, not a lock. setPanelOpen() drops the expand again
       when the panel closes, which is what makes "always" literal. */
    if (m_expanded.contains(key)) {
        return false;
    }
    if (m_collapsed.contains(key)) {
        return true;
    }
    if (m_alwaysCollapsed.contains(appName)) {
        return true;
    }
    /* Long groups start folded so a single noisy app cannot push everything
       else off the screen. */
    return m_autoCollapseOver > 0 && count > m_autoCollapseOver;
}

void HistoryModel::reloadRules()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("glassosdrc"));
    config->reparseConfiguration();

    QSet<QString> collapsed;
    const QStringList groups = config->groupList();
    for (const QString &groupName : groups) {
        if (!groupName.startsWith(QLatin1String("Rule "))) {
            continue;
        }
        KConfigGroup g(config, groupName);
        if (!g.readEntry("always_collapsed", false)) {
            continue;
        }
        /* Only an exact appname match is honoured. The rules engine matches
           globs, but a group is one concrete app, and expanding a glob across
           every app seen so far would make the panel's own toggle read back
           wrong for apps the user never configured. */
        const QString app = g.readEntry("appname", QString());
        if (!app.isEmpty()) {
            collapsed.insert(app);
        }
    }

    if (collapsed == m_alwaysCollapsed) {
        return;
    }
    m_alwaysCollapsed = collapsed;
    rebuild();
}

void HistoryModel::activateEntry(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).header) {
        return;
    }
    const Notification &n = m_rows.at(row).entry;
    Q_EMIT entryActivated(n.id, n.desktopEntry);
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
    /* The group key is the desktop entry where there is one, so it is not
       usable as the appname the always_collapsed rule is written under. */
    QString appName;
    for (const Notification &n : std::as_const(m_all)) {
        if (n.groupKey() == key) {
            appName = n.appName;
            break;
        }
    }
    if (isCollapsed(key, appName, count)) {
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
