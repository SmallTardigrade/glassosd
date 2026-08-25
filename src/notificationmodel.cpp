/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "notificationmodel.h"

#include <QDateTime>

#include <KIdleTime>
#include <KWaylandExtras>

namespace
{
constexpr int kTickMs = 250;
}

NotificationModel::NotificationModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &NotificationModel::expireTick);
    m_tick.start();

    auto *kit = KIdleTime::instance();
    connect(kit, &KIdleTime::timeoutReached, this, [this](int id, int) {
        if (id != m_idleTimeoutId) {
            return;
        }
        m_userIdle = true;
        qWarning("glassosd: session went idle (threshold %d ms) - holding expiry", m_idleThresholdMs);
        KIdleTime::instance()->catchNextResumeEvent();
    });
    connect(kit, &KIdleTime::resumingFromIdle, this, [this] {
        if (!m_userIdle) {
            return;
        }
        m_userIdle = false;
        qWarning("glassosd: resumed from idle - restarting dwell timers");
        /* Restart every visible notification's dwell from the moment the user
           came back, so nothing vanishes before it has been seen. */
        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (Notification &n : m_displayed) {
            n.displayedAt = now;
        }
    });
}

int NotificationModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_displayed.size();
}

QVariant NotificationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_displayed.size()) {
        return {};
    }
    const Notification &n = m_displayed.at(index.row());
    switch (role) {
    case IdRole:         return n.id;
    case AppNameRole:    return n.appName;
    case SummaryRole:    return n.summary;
    case BodyRole:       return n.body;
    case UrgencyRole:    return static_cast<int>(n.urgency);
    case ActionsRole: {
        /* The wire format is a flat [key, label, key, label, ...] list. The
           "default" key is special: it is what activating the notification
           body means, and per the spec it must not be drawn as a button. */
        QVariantList out;
        for (int i = 0; i + 1 < n.actions.size(); i += 2) {
            const QString key = n.actions.at(i);
            if (key == QLatin1String("default")) {
                continue;
            }
            if (key == QLatin1String("inline-reply")) {
                continue;   // rendered as a text field, not a button
            }
            out.append(QVariantMap{{QStringLiteral("key"), key},
                                   {QStringLiteral("label"), n.actions.at(i + 1)}});
        }
        return out;
    }
    case HasDefaultActionRole: {
        for (int i = 0; i + 1 < n.actions.size(); i += 2) {
            if (n.actions.at(i) == QLatin1String("default")) {
                return true;
            }
        }
        return false;
    }
    case GroupCountRole: return n.groupCount;
    case GroupKeyRole:   return n.groupKey();
    case ProgressRole:         return n.progressValue;
    case InlineReplyRole:      return n.inlineReply;
    case ReplyPlaceholderRole: return n.replyPlaceholder.isEmpty()
                                      ? QStringLiteral("Reply…") : n.replyPlaceholder;
    case ReplySubmitTextRole:  return n.replySubmitText.isEmpty()
                                      ? QStringLiteral("Send") : n.replySubmitText;
    case WhenRole: {
        /* Apple-style relative time: "now" while fresh, then minutes. */
        const qint64 secs = n.received.secsTo(QDateTime::currentDateTimeUtc());
        if (secs < 60) {
            return QStringLiteral("now");
        }
        if (secs < 3600) {
            return QStringLiteral("%1m ago").arg(secs / 60);
        }
        return n.received.toLocalTime().toString(QStringLiteral("HH:mm"));
    }
    case IconSourceRole:
        /* image-data outranks a themed name, matching the spec's ordering and
           mako's behaviour: an inline image is what the app actually wants
           shown, whereas app_icon is often a generic fallback. */
        if (n.image.isValid()) {
            return QStringLiteral("image://notifyimage/%1").arg(n.id);
        }
        return n.appIcon.isEmpty() ? QString() : QStringLiteral("image://icon/") + n.appIcon;
    default:
        return {};
    }
}

QHash<int, QByteArray> NotificationModel::roleNames() const
{
    return {
        {IdRole, "notifId"},
        {AppNameRole, "appName"},
        {SummaryRole, "summary"},
        {BodyRole, "body"},
        {IconSourceRole, "iconSource"},
        {UrgencyRole, "urgency"},
        {ActionsRole, "actions"},
        {HasDefaultActionRole, "hasDefaultAction"},
        {GroupCountRole, "groupCount"},
        {GroupKeyRole, "groupKey"},
        {WhenRole, "when"},
        {ProgressRole, "progress"},
        {InlineReplyRole, "inlineReply"},
        {ReplyPlaceholderRole, "replyPlaceholder"},
        {ReplySubmitTextRole, "replySubmitText"},
    };
}

ImageData NotificationModel::imageFor(uint id) const
{
    for (const Notification &n : m_displayed) {
        if (n.id == id) {
            return n.image;
        }
    }
    for (const Notification &n : m_waiting) {
        if (n.id == id) {
            return n.image;
        }
    }
    return {};
}

int NotificationModel::defaultTimeoutFor(Urgency u) const
{
    /* Mirrors the timeouts already tuned in the existing dunstrc. */
    switch (u) {
    case Urgency::Low:      return 4000;
    case Urgency::Critical: return 0;   // never expires
    case Urgency::Normal:
    default:                return 6000;
    }
}

int NotificationModel::indexOfDisplayed(uint id) const
{
    for (int i = 0; i < m_displayed.size(); ++i) {
        if (m_displayed.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

bool NotificationModel::replaceById(const Notification &n)
{
    /* Deliberately emits no NotificationClosed. The id is unchanged, so this
       is an update of an existing notification rather than the closing of one
       — dunst's queues_notification_replace_id() does the same. */
    const int row = indexOfDisplayed(n.id);
    if (row >= 0) {
        Notification updated = n;
        updated.groupCount = m_displayed.at(row).groupCount;
        updated.displayedAt = QDateTime::currentDateTimeUtc();  // restart the dwell
        if (updated.appIcon.isEmpty() && !updated.image.isValid()) {
            updated.appIcon = m_displayed.at(row).appIcon;
        }
        m_displayed[row] = updated;
        Q_EMIT dataChanged(index(row), index(row));
        return true;
    }
    for (int i = 0; i < m_waiting.size(); ++i) {
        if (m_waiting.at(i).id == n.id) {
            Notification updated = n;
            updated.groupCount = m_waiting.at(i).groupCount;
            m_waiting[i] = updated;
            return true;
        }
    }
    return false;
}

bool NotificationModel::replaceByTag(const Notification &n)
{
    if (n.stackTag.isEmpty()) {
        return false;
    }
    /* Matched on tag *and* app, per dunst — otherwise two apps using the same
       generic tag would clobber each other. Displayed is searched first. */
    auto matches = [&n](const Notification &old) {
        return !old.stackTag.isEmpty() && old.stackTag == n.stackTag && old.appName == n.appName;
    };

    for (int i = 0; i < m_displayed.size(); ++i) {
        if (matches(m_displayed.at(i))) {
            const Notification old = m_displayed.at(i);
            Notification updated = n;
            updated.groupCount = old.groupCount;
            updated.displayedAt = QDateTime::currentDateTimeUtc();
            if (updated.appIcon.isEmpty() && !updated.image.isValid()) {
                updated.appIcon = old.appIcon;   // inherit rather than go blank
            }
            m_displayed[i] = updated;
            Q_EMIT dataChanged(index(i), index(i));
            /* A different id is going away, so this one does need announcing.
               Expired, not Dismissed — the user never acted on it. */
            Q_EMIT notificationClosed(old.id, CloseReason::Expired);
            return true;
        }
    }
    for (int i = 0; i < m_waiting.size(); ++i) {
        if (matches(m_waiting.at(i))) {
            const Notification old = m_waiting.at(i);
            Notification updated = n;
            updated.groupCount = old.groupCount;
            if (updated.appIcon.isEmpty() && !updated.image.isValid()) {
                updated.appIcon = old.appIcon;
            }
            m_waiting[i] = updated;
            Q_EMIT notificationClosed(old.id, CloseReason::Expired);
            return true;
        }
    }
    return false;
}

bool NotificationModel::coalesce(const Notification &n)
{
    /* Stack tags outrank coalescing: a tagged notification has explicitly
       asked to be replaced by its successor, not counted alongside it. */
    if (!n.stackTag.isEmpty() || m_coalesceThreshold <= 0) {
        return false;
    }

    const QString key = n.groupKey();
    const QDateTime now = QDateTime::currentDateTimeUtc();

    QList<QDateTime> &times = m_recentByGroup[key];
    times.removeIf([&](const QDateTime &t) {
        return t.msecsTo(now) > m_coalesceWindowMs;
    });
    times.append(now);

    if (times.size() < m_coalesceThreshold) {
        return false;
    }

    /* Absorb every existing untagged card from this group, not just one. The
       burst's first messages arrived before the threshold was crossed and are
       already on screen as separate cards; without this they would linger
       beside the merged one and the count would understate the total. */
    int absorbed = 0;
    for (int i = m_displayed.size() - 1; i >= 0; --i) {
        const Notification &old = m_displayed.at(i);
        if (old.groupKey() != key || !old.stackTag.isEmpty()) {
            continue;
        }
        absorbed += old.groupCount;
        const uint oldId = old.id;
        beginRemoveRows({}, i, i);
        m_displayed.removeAt(i);
        endRemoveRows();
        Q_EMIT notificationClosed(oldId, CloseReason::Expired);
    }
    for (int i = m_waiting.size() - 1; i >= 0; --i) {
        const Notification &old = m_waiting.at(i);
        if (old.groupKey() != key || !old.stackTag.isEmpty()) {
            continue;
        }
        absorbed += old.groupCount;
        const uint oldId = old.id;
        m_waiting.removeAt(i);
        Q_EMIT notificationClosed(oldId, CloseReason::Expired);
    }

    Notification merged = n;
    merged.groupCount = absorbed + 1;
    /* Front of the queue so a continuing burst keeps one live card rather
       than re-popping behind whatever else is on screen. */
    m_waiting.prepend(merged);
    return true;
}

uint NotificationModel::insert(Notification n)
{
    n.received = QDateTime::currentDateTimeUtc();
    if (n.timeoutMs < 0) {
        n.timeoutMs = defaultTimeoutFor(n.urgency);
    }

    if (n.id != 0 && replaceById(n)) {
        update();
        return n.id;
    }
    if (replaceByTag(n)) {
        update();
        return n.id;
    }
    if (coalesce(n)) {
        update();
        return n.id;
    }

    /* A rule asked for no popup. The sender is still told the notification
       ended, so it does not sit waiting on a close signal that never comes. */
    if (n.skipDisplay) {
        Q_EMIT notificationClosed(n.id, CloseReason::Expired);
        return n.id;
    }

    /* Do Not Disturb suppresses the popup but must never lose the message;
       history still receives it via the closed signal path. */
    if (m_dnd) {
        Q_EMIT notificationClosed(n.id, CloseReason::Expired);
        return n.id;
    }

    m_waiting.append(n);
    update();
    return n.id;
}

bool NotificationModel::closeId(uint id, uint reason)
{
    const int row = indexOfDisplayed(id);
    if (row >= 0) {
        beginRemoveRows({}, row, row);
        m_displayed.removeAt(row);
        endRemoveRows();
        m_hovered.remove(id);
        Q_EMIT notificationClosed(id, reason);
        update();
        return true;
    }
    for (int i = 0; i < m_waiting.size(); ++i) {
        if (m_waiting.at(i).id == id) {
            m_waiting.removeAt(i);
            Q_EMIT notificationClosed(id, reason);
            update();
            return true;
        }
    }
    return false;
}

int NotificationModel::dismissAll()
{
    /* Ids are collected first: closeId() mutates both containers, so
       iterating them directly would invalidate the loop underneath itself. */
    QList<uint> ids;
    ids.reserve(m_displayed.size() + m_waiting.size());
    for (const Notification &n : std::as_const(m_displayed)) {
        ids << n.id;
    }
    for (const Notification &n : std::as_const(m_waiting)) {
        ids << n.id;
    }
    for (uint id : std::as_const(ids)) {
        closeId(id, CloseReason::Dismissed);
    }
    return int(ids.size());
}

void NotificationModel::setHoverPause(bool on)
{
    if (m_hoverPause == on) {
        return;
    }
    m_hoverPause = on;
    if (!on) {
        /* Anything currently held by the pointer resumes counting from now,
           not from whenever it appeared — otherwise turning the setting off
           makes every hovered popup vanish at once. */
        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (uint id : std::as_const(m_hovered)) {
            const int row = indexOfDisplayed(id);
            if (row >= 0) {
                m_displayed[row].displayedAt = now;
            }
        }
        m_hovered.clear();
    }
}

void NotificationModel::setHovered(uint id, bool hovered)
{
    if (!m_hoverPause) {
        return;
    }
    /* Logged once per transition: this is the only way to confirm from
       outside that pointer events are reaching the card at all, which
       depends on the layer surface's input region being right. */
    qDebug("glassosd: notification %u hover=%s", id, hovered ? "in" : "out");
    if (hovered) {
        m_hovered.insert(id);
    } else {
        /* Restart the dwell from now rather than resuming mid-count, so a
           notification you just finished reading does not vanish instantly. */
        if (m_hovered.remove(id)) {
            const int row = indexOfDisplayed(id);
            if (row >= 0) {
                m_displayed[row].displayedAt = QDateTime::currentDateTimeUtc();
            }
        }
    }
}

void NotificationModel::dismiss(uint id)
{
    closeId(id, CloseReason::Dismissed);
}

void NotificationModel::sendReply(uint id, const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    Q_EMIT replied(id, text);
    closeId(id, CloseReason::Dismissed);
}

void NotificationModel::activate(QQuickWindow *window, uint id, const QString &key)
{
    const int row = indexOfDisplayed(id);
    const QString appId = row >= 0 ? m_displayed.at(row).desktopEntry : QString();

    if (!window) {
        invokeAction(id, key);
        return;
    }

    /* Ask the compositor for a token tied to this click, then hand it to the
       sender just before the action itself so it can raise its window. */
    auto future = KWaylandExtras::xdgActivationToken(window, 0, appId);
    future.then(this, [this, id, key](const QString &token) {
        if (!token.isEmpty()) {
            Q_EMIT activationToken(id, token);
        }
        invokeAction(id, key);
    });
}

void NotificationModel::invokeAction(uint id, const QString &key)
{
    Q_EMIT actionInvoked(id, key);
    closeId(id, CloseReason::Dismissed);
}

int NotificationModel::effectiveLimit() const
{
    if (m_limit == 0) {
        return std::numeric_limits<int>::max();
    }
    /* Reserve a slot for the "+N more" row, but only when there is genuinely
       an overflow to indicate — dunst additionally requires limit > 1, else a
       limit of 1 would leave no room for any notification at all. */
    if (m_indicateHidden && m_limit > 1 && m_displayed.size() + m_waiting.size() > m_limit) {
        return m_limit - 1;
    }
    return m_limit;
}

void NotificationModel::update()
{
    const int limit = effectiveLimit();

    while (m_displayed.size() < limit && !m_waiting.isEmpty()) {
        Notification n = m_waiting.takeFirst();
        n.displayedAt = QDateTime::currentDateTimeUtc();
        beginInsertRows({}, m_displayed.size(), m_displayed.size());
        m_displayed.append(n);
        endInsertRows();
    }

    /* Shrinking the limit (e.g. once the indicator slot is needed) pushes the
       newest back to the front of the queue rather than dropping it. */
    while (m_displayed.size() > limit) {
        const int row = m_displayed.size() - 1;
        beginRemoveRows({}, row, row);
        const Notification n = m_displayed.takeLast();
        endRemoveRows();
        m_waiting.prepend(n);
    }

    /* Compare against the last value actually emitted, not against the size
       on entry: insert() appends to m_waiting *before* calling update(), so an
       entry snapshot already contains the new arrival. When nothing then moves
       between the lists the two match, no signal fires, and the "+N more"
       label silently goes stale — it read "+2" while four were really queued. */
    if (m_waiting.size() != m_lastHiddenCount) {
        m_lastHiddenCount = m_waiting.size();
        Q_EMIT hiddenCountChanged();
    }
}

void NotificationModel::expireTick()
{
    /* Drop coalescing history for groups that have gone quiet. The per-group
       list is pruned on use, but the map entry itself was never removed, so
       every app that ever sent a notification left a key behind for the life
       of the session. */
    if (m_coalesceWindowMs > 0) {
        const QDateTime cutoff = QDateTime::currentDateTimeUtc();
        for (auto it = m_recentByGroup.begin(); it != m_recentByGroup.end();) {
            it.value().removeIf([&](const QDateTime &t) {
                return t.msecsTo(cutoff) > m_coalesceWindowMs;
            });
            it = it.value().isEmpty() ? m_recentByGroup.erase(it) : std::next(it);
        }
    }

    /* Hold everything while the user is away. The point is "nobody is here to
       read these", which is a property of the session, not of any one
       message. */
    if (m_userIdle) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QList<uint> expired;
    for (const Notification &n : std::as_const(m_displayed)) {
        if (n.timeoutMs <= 0 || !n.displayedAt.isValid()) {
            continue;   // 0 means never expire, per the spec
        }
        if (m_hovered.contains(n.id)) {
            continue;   // being read
        }
        if (n.displayedAt.msecsTo(now) >= n.timeoutMs) {
            expired << n.id;
        }
    }
    for (uint id : std::as_const(expired)) {
        closeId(id, CloseReason::Expired);
    }
}

void NotificationModel::setIdleThresholdMs(int ms)
{
    /* The Wayland idle protocol (ext_idle_notification_v1) is
       notification-based: there is no "how long has the session been idle"
       query, so KIdleTime::idleTime() simply returns 0 here. An earlier
       attempt compared against it and therefore never fired. The supported
       shape is a registered timeout plus the resume signal.

       The signals are connected once, in the constructor — Qt::UniqueConnection
       cannot be used with a lambda (it asserts at runtime), so re-connecting
       here on every settings reload would stack duplicate handlers. */
    auto *kit = KIdleTime::instance();
    if (m_idleTimeoutId >= 0) {
        kit->removeIdleTimeout(m_idleTimeoutId);
        m_idleTimeoutId = -1;
    }
    m_userIdle = false;
    m_idleThresholdMs = ms;
    if (ms > 0) {
        m_idleTimeoutId = kit->addIdleTimeout(ms);
    }
}

void NotificationModel::setDoNotDisturb(bool dnd)
{
    if (m_dnd == dnd) {
        return;
    }
    m_dnd = dnd;
    Q_EMIT doNotDisturbChanged();
}
