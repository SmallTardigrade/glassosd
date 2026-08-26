/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "notificationserver.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QDir>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>

namespace
{
/* Clients disagree about the variant type of "urgency": the spec says byte,
   but uint32 and int32 both appear in the wild. mako handles all three and so
   must we, or notifications from those clients get the wrong urgency. */
Urgency readUrgency(const QVariant &v, bool *ok)
{
    *ok = true;
    bool converted = false;
    const int raw = v.toInt(&converted);
    if (!converted) {
        *ok = false;
        return Urgency::Normal;
    }
    switch (raw) {
    case 0:  return Urgency::Low;
    case 2:  return Urgency::Critical;
    default: return Urgency::Normal;
    }
}

/* The spec allows only <b> <i> <u> <a> <img> in body markup. Senders in the
   wild go well beyond that — DrKonqi wraps its body in <html><tt>...</tt>,
   neither of which Qt's StyledText understands, so they render as literal
   angle brackets in the middle of the message. Strip anything outside the
   permitted set rather than showing it raw. */
QString sanitiseMarkup(const QString &in)
{
    static const QRegularExpression tag(QStringLiteral("<\\s*/?\\s*([a-zA-Z0-9]+)[^>]*>"));

    QString out;
    out.reserve(in.size());
    int last = 0;
    auto it = tag.globalMatch(in);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += in.mid(last, m.capturedStart() - last);
        const QString name = m.captured(1).toLower();
        if (name == QLatin1String("b") || name == QLatin1String("i")
            || name == QLatin1String("u") || name == QLatin1String("a")
            || name == QLatin1String("img") || name == QLatin1String("br")) {
            out += m.captured(0);
        }
        /* Anything else is dropped, keeping its inner text. */
        last = m.capturedEnd();
    }
    out += in.mid(last);
    return out.trimmed();
}

QVariant unwrap(const QVariant &v)
{
    if (v.canConvert<QDBusVariant>()) {
        return v.value<QDBusVariant>().variant();
    }
    return v;
}
} // namespace

NotificationServer::NotificationServer(NotificationModel *model, HistoryModel *history, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_history(history)
{
    qDBusRegisterMetaType<ImageData>();

    connect(m_model, &NotificationModel::notificationClosed, this, &NotificationServer::closed);
    connect(m_model, &NotificationModel::actionInvoked, this, &NotificationServer::actionInvoked);
    connect(m_model, &NotificationModel::replied, this, &NotificationServer::replied);
    connect(m_model, &NotificationModel::activationToken, this, &NotificationServer::activationToken);
}

bool NotificationServer::start()
{
    new NotificationsAdaptor(this);

    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), this)) {
        qWarning("glassosd: could not register the notification object");
        return false;
    }
    if (!bus.registerService(QStringLiteral("org.freedesktop.Notifications"))) {
        /* Almost always means another daemon holds it. We do not fight for it:
           silently stealing the name mid-session would drop messages. */
        qWarning("glassosd: org.freedesktop.Notifications is already owned; "
                 "notifications stay with the existing daemon");
        bus.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));
        return false;
    }
    qWarning("glassosd: serving org.freedesktop.Notifications");
    return true;
}

namespace
{
/* Resolve a desktop-entry id to the name and icon in its .desktop file.

   xdg-desktop-portal forwards a sandboxed app's notification with an empty
   app_name and app_icon, so everything arriving that way — which is every
   Flatpak — would otherwise be an anonymous card. The desktop-entry hint is
   usually still present and is enough to recover both. */
struct DesktopInfo {
    QString name;
    QString icon;
};

DesktopInfo lookupDesktopEntry(const QString &entry)
{
    if (entry.isEmpty()) {
        return {};
    }
    /* Cached: a burst from one app would otherwise stat the whole XDG data
       path once per message. */
    static QHash<QString, DesktopInfo> cache;
    const auto cached = cache.constFind(entry);
    if (cached != cache.constEnd()) {
        return *cached;
    }

    QString id = entry;
    if (id.endsWith(QLatin1String(".desktop"))) {
        id.chop(8);
    }

    QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                          id + QStringLiteral(".desktop"));
    if (path.isEmpty()) {
        /* Flatpaks install under their full reverse-DNS id but some apps send
           only the last component, so fall back to a suffix match. */
        for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
            const QStringList hits =
                QDir(dir).entryList({QStringLiteral("*") + id + QStringLiteral(".desktop")}, QDir::Files);
            if (!hits.isEmpty()) {
                path = dir + QLatin1Char('/') + hits.first();
                break;
            }
        }
    }

    DesktopInfo info;
    if (!path.isEmpty()) {
        QSettings df(path, QSettings::IniFormat);
        df.beginGroup(QStringLiteral("Desktop Entry"));
        info.name = df.value(QStringLiteral("Name")).toString();
        info.icon = df.value(QStringLiteral("Icon")).toString();
    }
    cache.insert(entry, info);
    return info;
}
} // namespace

void NotificationServer::reserveIds(uint highest)
{
    if (highest >= m_nextId) {
        m_nextId = highest + 1;
    }
}

uint NotificationServer::handleNotify(const QString &appName,
                                      uint replacesId,
                                      const QString &appIcon,
                                      const QString &summary,
                                      const QString &body,
                                      const QStringList &actions,
                                      const QVariantMap &hints,
                                      int expireTimeout)
{
    Notification n;
    n.appName = appName;
    n.appIcon = appIcon;
    n.summary = sanitiseMarkup(summary);
    n.body = sanitiseMarkup(body);
    n.actions = actions;
    n.timeoutMs = expireTimeout;   // -1 server decides, 0 never; resolved in the model
    n.id = replacesId != 0 ? replacesId : m_nextId++;

    for (auto it = hints.constBegin(); it != hints.constEnd(); ++it) {
        const QString &key = it.key();
        const QVariant value = unwrap(it.value());

        if (key == QLatin1String("urgency")) {
            bool ok = false;
            const Urgency u = readUrgency(value, &ok);
            if (ok) {
                n.urgency = u;
            }
        } else if (key == QLatin1String("x-kde-reply-placeholder-text")) {
            n.replyPlaceholder = value.toString();
        } else if (key == QLatin1String("x-kde-reply-submit-button-text")) {
            n.replySubmitText = value.toString();
        } else if (key == QLatin1String("value")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok) {
                n.progressValue = qBound(0, v, 100);
            }
        } else if (key == QLatin1String("transient")) {
            n.transientHint = value.toBool();
        } else if (key == QLatin1String("category")) {
            n.category = value.toString();
        } else if (key == QLatin1String("desktop-entry")) {
            n.desktopEntry = value.toString();
        } else if (key == QLatin1String("x-dunst-stack-tag")
                   || key == QLatin1String("x-canonical-private-synchronous")) {
            /* Two names for the same de-facto convention. Honouring both means
               the existing dunstrc rules and any app that sets it directly
               both keep working. */
            n.stackTag = value.toString();
        } else if (key == QLatin1String("image-data") || key == QLatin1String("image_data")
                   || key == QLatin1String("icon_data")) {
            ImageData img;
            if (value.canConvert<QDBusArgument>()) {
                value.value<QDBusArgument>() >> img;
                if (img.isValid() && img.data.size() >= img.expectedLength()) {
                    n.image = img;
                }
            }
        } else if (key == QLatin1String("image-path") || key == QLatin1String("image_path")) {
            /* Outranks app_icon but not inline image data. */
            const QString path = value.toString();
            if (!path.isEmpty() && n.appIcon.isEmpty()) {
                n.appIcon = path;
            }
        }
    }

    /* The reply affordance is requested by the app as an action key, so it is
       driven entirely by the sender rather than assumed for particular apps. */
    n.inlineReply = n.actions.contains(QLatin1String("inline-reply"));

    /* Everything arriving through xdg-desktop-portal has an empty app_name
       and app_icon — the portal does not forward the sandboxed app's
       identity. Recover both from the desktop-entry hint, or a Flatpak's
       notification renders as an anonymous card with no name and no icon. */
    if (n.appName.isEmpty() || n.appIcon.isEmpty()) {
        const DesktopInfo info = lookupDesktopEntry(n.desktopEntry);
        if (n.appName.isEmpty() && !info.name.isEmpty()) {
            n.appName = info.name;
        }
        if (n.appIcon.isEmpty() && !info.icon.isEmpty()) {
            n.appIcon = info.icon;
        }
    }

    /* Rules run after the hints are parsed (so they can match on category and
       desktop-entry) but before insertion, because assigning a stack tag has
       to happen before the queue decides what this replaces. */
    m_rules.apply(n);

    /* The spec has no opinion here, but an empty message is not worth a
       popup; dunst drops these too. */
    if (n.summary.isEmpty() && n.body.isEmpty()) {
        return n.id;
    }

    /* Recorded before the queue runs, so every member of a coalesced burst is
       kept individually even though only one card is shown. */
    n.received = QDateTime::currentDateTimeUtc();
    /* Transient notifications are explicitly not meant to persist. */
    if (!n.transientHint) {
        m_history->record(n);
    }

    return m_model->insert(n);
}

void NotificationServer::handleClose(uint id)
{
    m_model->closeId(id, CloseReason::Closed);
}

// ---------------------------------------------------------------- adaptor

NotificationsAdaptor::NotificationsAdaptor(NotificationServer *server)
    : QDBusAbstractAdaptor(server)
    , m_server(server)
{
    setAutoRelaySignals(false);
    connect(server, &NotificationServer::closed, this, &NotificationsAdaptor::NotificationClosed);
    connect(server, &NotificationServer::actionInvoked, this, &NotificationsAdaptor::ActionInvoked);
    connect(server, &NotificationServer::replied, this, &NotificationsAdaptor::NotificationReplied);
    connect(server, &NotificationServer::activationToken, this, &NotificationsAdaptor::ActivationToken);
}

uint NotificationsAdaptor::Notify(const QString &app_name,
                                  uint replaces_id,
                                  const QString &app_icon,
                                  const QString &summary,
                                  const QString &body,
                                  const QStringList &actions,
                                  const QVariantMap &hints,
                                  int expire_timeout)
{
    return m_server->handleNotify(app_name, replaces_id, app_icon, summary, body,
                                  actions, hints, expire_timeout);
}

void NotificationsAdaptor::CloseNotification(uint id)
{
    m_server->handleClose(id);
}

uint NotificationsAdaptor::Inhibit(const QString &desktop_entry,
                                   const QString &reason,
                                   const QVariantMap &hints)
{
    /* hints is accepted and ignored: upstream defines none that change what
       inhibition means, and refusing the argument would break callers. */
    Q_UNUSED(hints)
    return m_server->inhibit(desktop_entry, reason);
}

void NotificationsAdaptor::UnInhibit(uint cookie)
{
    m_server->unInhibit(cookie);
}

bool NotificationsAdaptor::inhibited() const
{
    return m_server->inhibited();
}

uint NotificationServer::inhibit(const QString &desktopEntry, const QString &reason)
{
    const QString service = calledFromDBus() ? message().service() : QString();

    if (!m_inhibitWatcher) {
        m_inhibitWatcher = new QDBusServiceWatcher(this);
        m_inhibitWatcher->setConnection(QDBusConnection::sessionBus());
        m_inhibitWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(m_inhibitWatcher, &QDBusServiceWatcher::serviceUnregistered,
                this, [this](const QString &gone) {
                    /* Whoever asked for quiet has left the bus. Releasing here
                       is the difference between a screen recorder crashing
                       mid-share and notifications staying silenced until the
                       next login. */
                    bool removed = false;
                    for (auto it = m_inhibits.begin(); it != m_inhibits.end();) {
                        if (it.value().service == gone) {
                            it = m_inhibits.erase(it);
                            removed = true;
                        } else {
                            ++it;
                        }
                    }
                    if (removed) {
                        m_inhibitWatcher->removeWatchedService(gone);
                        refreshInhibited();
                    }
                });
    }
    if (!service.isEmpty()) {
        m_inhibitWatcher->addWatchedService(service);
    }

    const uint cookie = m_nextInhibit++;
    m_inhibits.insert(cookie, Inhibition{service, desktopEntry, reason});
    qInfo("glassosd: notifications inhibited by %s (%s)",
          qUtf8Printable(desktopEntry.isEmpty() ? service : desktopEntry),
          qUtf8Printable(reason));
    refreshInhibited();
    return cookie;
}

void NotificationServer::unInhibit(uint cookie)
{
    const auto it = m_inhibits.constFind(cookie);
    if (it == m_inhibits.cend()) {
        return;
    }
    const QString service = it.value().service;
    m_inhibits.erase(it);
    /* Only stop watching once nothing else of theirs is left: one application
       may hold several inhibitions at once. */
    if (m_inhibitWatcher && !service.isEmpty()) {
        bool stillHeld = false;
        for (const Inhibition &other : std::as_const(m_inhibits)) {
            if (other.service == service) {
                stillHeld = true;
                break;
            }
        }
        if (!stillHeld) {
            m_inhibitWatcher->removeWatchedService(service);
        }
    }
    refreshInhibited();
}

void NotificationServer::refreshInhibited()
{
    if (m_model) {
        m_model->setInhibited(inhibited());
    }
    /* The property is declared EmitsChangedSignal, so anything watching for
       quiet — a status widget, another daemon — hears about it without
       polling. Emitted by hand because the adaptor's property is read-only
       and Qt only auto-emits for ones it can write. */
    QDBusMessage msg = QDBusMessage::createSignal(
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    msg << QStringLiteral("org.freedesktop.Notifications")
        << QVariantMap{{QStringLiteral("Inhibited"), inhibited()}}
        << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

QStringList NotificationsAdaptor::GetCapabilities()
{
    return {
        QStringLiteral("body"),
        QStringLiteral("body-markup"),
        QStringLiteral("body-hyperlinks"),
        QStringLiteral("actions"),
        QStringLiteral("action-icons"),
        QStringLiteral("icon-static"),
        QStringLiteral("persistence"),
        /* We render the "value" hint as a progress bar. */
        QStringLiteral("x-kde-display-appname"),
        /* Advertised so senders know replace-by-tag is available, exactly as
           dunst and mako do. */
        QStringLiteral("x-dunst-stack-tag"),
        QStringLiteral("x-canonical-private-synchronous"),
        /* KDE extension: lets senders offer a reply field rather than a button. */
        QStringLiteral("inline-reply"),
    };
}

QString NotificationsAdaptor::GetServerInformation(QString &vendor, QString &version, QString &spec_version)
{
    vendor = QStringLiteral("glassosd");
    version = QStringLiteral("0.1");
    spec_version = QStringLiteral("1.2");
    return QStringLiteral("glassosd");
}
