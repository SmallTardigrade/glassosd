/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "snoozestore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

SnoozeStore::SnoozeStore(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SnoozeStore::fire);
    load();
    /* Queued rather than called directly: anything already due fires as soon
       as the event loop turns, by which time whoever owns us has had a chance
       to connect to woke(). Firing from the constructor would emit into
       nothing and lose the notification for good. */
    QTimer::singleShot(0, this, &SnoozeStore::rearm);
}

QString SnoozeStore::storePath() const
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/glassosd");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/snoozed.json");
}

void SnoozeStore::snooze(const Notification &n, int minutes)
{
    Item item;
    item.n = n;
    item.wakeAt = QDateTime::currentDateTimeUtc().addSecs(qint64(qMax(1, minutes)) * 60);
    m_items.append(item);
    save();
    rearm();
    Q_EMIT changed();
}

void SnoozeStore::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Item item;
        item.wakeAt = QDateTime::fromString(o[QStringLiteral("wakeAt")].toString(),
                                            Qt::ISODate);
        if (!item.wakeAt.isValid()) {
            continue;
        }
        Notification &n = item.n;
        n.appName      = o[QStringLiteral("app")].toString();
        n.desktopEntry = o[QStringLiteral("desktopEntry")].toString();
        n.appIcon      = o[QStringLiteral("icon")].toString();
        n.summary      = o[QStringLiteral("summary")].toString();
        n.body         = o[QStringLiteral("body")].toString();
        n.category     = o[QStringLiteral("category")].toString();
        n.stackTag     = o[QStringLiteral("stackTag")].toString();
        n.urgency      = static_cast<Urgency>(o[QStringLiteral("urgency")].toInt());
        n.timeoutMs    = o[QStringLiteral("timeout")].toInt(-1);
        const QJsonArray acts = o[QStringLiteral("actions")].toArray();
        for (const QJsonValue &a : acts) {
            n.actions << a.toString();
        }
        /* The id is deliberately not restored. Ids are unique within one run
           of the daemon; reusing one from a previous run would collide with a
           live notification. insert() assigns a fresh one. */
        m_items.append(item);
    }
}

void SnoozeStore::save() const
{
    QJsonArray arr;
    for (const Item &item : m_items) {
        QJsonObject o;
        o[QStringLiteral("wakeAt")]       = item.wakeAt.toString(Qt::ISODate);
        o[QStringLiteral("app")]          = item.n.appName;
        o[QStringLiteral("desktopEntry")] = item.n.desktopEntry;
        o[QStringLiteral("icon")]         = item.n.appIcon;
        o[QStringLiteral("summary")]      = item.n.summary;
        o[QStringLiteral("body")]         = item.n.body;
        o[QStringLiteral("category")]     = item.n.category;
        o[QStringLiteral("stackTag")]     = item.n.stackTag;
        o[QStringLiteral("urgency")]      = int(item.n.urgency);
        o[QStringLiteral("timeout")]      = item.n.timeoutMs;
        o[QStringLiteral("actions")]      = QJsonArray::fromStringList(item.n.actions);
        /* Inline image data is not persisted, for the same reason history does
           not: it is raw pixels and would bloat the file. A snoozed
           notification that carried one falls back to its app icon. */
        arr.append(o);
    }
    QFile f(storePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
}

void SnoozeStore::rearm()
{
    m_timer.stop();
    if (m_items.isEmpty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto soonest = std::min_element(m_items.cbegin(), m_items.cend(),
                                    [](const Item &a, const Item &b) {
                                        return a.wakeAt < b.wakeAt;
                                    });
    const qint64 ms = now.msecsTo(soonest->wakeAt);
    /* A single timer for the earliest item rather than one per notification:
       the list is short, and this way a suspend that overshoots several wake
       times is handled by one fire() that sweeps all of them. */
    m_timer.start(int(qBound<qint64>(0, ms, 60LL * 60 * 1000)));
}

void SnoozeStore::fire()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QVector<Notification> due;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).wakeAt <= now) {
            due.prepend(m_items.at(i).n);
            m_items.removeAt(i);
        }
    }
    if (!due.isEmpty()) {
        save();
        for (const Notification &n : std::as_const(due)) {
            Q_EMIT woke(n);
        }
        Q_EMIT changed();
    }
    /* Always re-arm. The timer is capped at an hour, so a wake further out
       than that needs the next tick to get closer to it. */
    rearm();
}

int SnoozeStore::wakeAll()
{
    const int n = int(m_items.size());
    if (n == 0) {
        return 0;
    }
    QVector<Item> items;
    items.swap(m_items);
    save();
    for (const Item &item : std::as_const(items)) {
        Q_EMIT woke(item.n);
    }
    Q_EMIT changed();
    rearm();
    return n;
}
