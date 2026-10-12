/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "portalrelay.h"

#include "notificationserver.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

using namespace NotificationWire;
using namespace PortalWire;

namespace
{
constexpr QLatin1String kFdoService{"org.freedesktop.Notifications"};
constexpr QLatin1String kFdoPath{"/org/freedesktop/Notifications"};
constexpr QLatin1String kFdoIface{"org.freedesktop.Notifications"};
} // namespace

PortalRelay::PortalRelay(QObject *parent)
    : PortalBackend(parent)
{
    qDBusRegisterMetaType<ImageData>();

    /* Subscribed by well-known name, so the daemon behind it can change —
       Plasma restarting, or the user swapping daemons mid-session — without
       the relay losing track. */
    auto bus = QDBusConnection::sessionBus();
    bus.connect(kFdoService, kFdoPath, kFdoIface, QStringLiteral("ActionInvoked"),
                this, SLOT(onActionInvoked(uint, QString)));
    bus.connect(kFdoService, kFdoPath, kFdoIface, QStringLiteral("ActivationToken"),
                this, SLOT(onActivationToken(uint, QString)));
    bus.connect(kFdoService, kFdoPath, kFdoIface, QStringLiteral("NotificationClosed"),
                this, SLOT(onClosed(uint, uint)));
}

QString PortalRelay::keyFor(const QString &appId, const QString &portalId)
{
    /* A separator neither id can contain. */
    return appId + QChar(0x1f) + portalId;
}

PortalRelay::NotifyCall PortalRelay::translate(const QString &appId, const QVariantMap &props)
{
    const QVariantMap p = unwrapAll(props);
    NotifyCall c;

    /* The portal's identity, which is the whole point of the portal path:
       the daemon on the other end recovers the name and icon from the
       desktop file, exactly as it does for anything else that sets it. */
    if (!appId.isEmpty()) {
        c.hints.insert(QStringLiteral("desktop-entry"), appId);
    }

    c.summary = p.value(QStringLiteral("title")).toString();

    /* The freedesktop body is markup, so plain text has to be escaped to stay
       text. Newlines are left alone: every daemon renders them, and <br> is
       not in the spec's tag list. markup-body never reaches a version 1
       backend, but is handled in case a frontend ever forwards it. */
    const QString markup = p.value(QStringLiteral("markup-body")).toString();
    c.body = markup.isEmpty() ? p.value(QStringLiteral("body")).toString().toHtmlEscaped()
                              : sanitiseMarkup(markup);

    const QVariant iconValue = p.value(QStringLiteral("icon"));
    QString kind;
    QVariant inner;
    if (iconValue.typeId() == QMetaType::QString) {
        c.appIcon = iconValue.toString();
    } else if (readPair(iconValue, &kind, &inner)) {
        if (kind == QLatin1String("themed")) {
            c.appIcon = inner.toStringList().value(0);
        } else {
            /* Version 1 is sent bytes; a descriptor is handled too, so the
               relay does not depend on which the frontend chooses. */
            const ImageData img = decodeImage(kind == QLatin1String("bytes") ? inner.toByteArray()
                                                                             : readFd(inner));
            if (img.isValid()) {
                c.hints.insert(QStringLiteral("image-data"), QVariant::fromValue(img));
            }
        }
    }

    c.hints.insert(QStringLiteral("urgency"),
                   QVariant::fromValue(uchar(urgencyFor(p.value(QStringLiteral("priority")).toString()))));

    const QString category = p.value(QStringLiteral("category")).toString();
    if (!category.isEmpty()) {
        c.hints.insert(QStringLiteral("category"), category);
    }
    if (p.value(QStringLiteral("display-hint")).toStringList().contains(QLatin1String("transient"))) {
        c.hints.insert(QStringLiteral("transient"), true);
    }

    /* "default" is what activating the body means on the freedesktop
       interface; the sender's own name for it is kept for the way back. */
    const QString defaultAction = p.value(QStringLiteral("default-action")).toString();
    if (!defaultAction.isEmpty()) {
        c.actions << QStringLiteral("default") << QString();
        c.defaultActionName = defaultAction;
        c.targets.insert(QStringLiteral("default"), p.value(QStringLiteral("default-action-target")));
    }

    QList<QVariantMap> buttons;
    const QVariant buttonsValue = p.value(QStringLiteral("buttons"));
    if (buttonsValue.canConvert<QDBusArgument>()) {
        buttons = qdbus_cast<QList<QVariantMap>>(buttonsValue.value<QDBusArgument>());
    } else {
        const QVariantList list = buttonsValue.toList();
        for (const QVariant &v : list) {
            buttons.append(unwrap(v).toMap());
        }
    }
    for (const QVariantMap &raw : std::as_const(buttons)) {
        const QVariantMap button = unwrapAll(raw);
        const QString action = button.value(QStringLiteral("action")).toString();
        const QString label = button.value(QStringLiteral("label")).toString();
        /* A button with no label is one the spec says to drop, not to draw
           blank. */
        if (action.isEmpty() || label.isEmpty()) {
            continue;
        }
        c.actions << action << label;
        c.targets.insert(action, button.value(QStringLiteral("target")));
    }
    return c;
}

void PortalRelay::addNotification(const QString &appId, const QString &portalId, const QVariantMap &props)
{
    if (portalId.isEmpty()) {
        return;
    }
    const QString key = keyFor(appId, portalId);
    const NotifyCall c = translate(appId, props);
    /* Same app, same id: the daemon replaces in place rather than stacking. */
    const uint replaces = m_ids.value(key, 0);

    QDBusMessage call = QDBusMessage::createMethodCall(kFdoService, kFdoPath, kFdoIface,
                                                       QStringLiteral("Notify"));
    call.setArguments({c.appName, replaces, c.appIcon, c.summary, c.body, c.actions, c.hints, c.timeout});

    m_inFlight.insert(key);
    m_withdrawn.remove(key);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, key, appId, portalId, c, replaces](QDBusPendingCallWatcher *w) {
                const QDBusPendingReply<uint> reply = *w;
                w->deleteLater();
                m_inFlight.remove(key);
                if (reply.isError()) {
                    /* Once, not per notification: with nothing to relay to,
                       every Flatpak notification would say the same thing. */
                    if (!m_warned) {
                        m_warned = true;
                        qWarning("glassosd: could not relay a portal notification to "
                                 "org.freedesktop.Notifications: %s",
                                 qUtf8Printable(reply.error().message()));
                    }
                    return;
                }
                const uint id = reply.value();
                if (m_withdrawn.remove(key)) {
                    /* Withdrawn while the Notify was on its way. */
                    QDBusMessage close = QDBusMessage::createMethodCall(kFdoService, kFdoPath, kFdoIface,
                                                                        QStringLiteral("CloseNotification"));
                    close.setArguments({id});
                    QDBusConnection::sessionBus().call(close, QDBus::NoBlock);
                    return;
                }
                if (replaces != 0 && replaces != id) {
                    m_entries.remove(replaces);
                }
                m_entries.insert(id, Entry{appId, portalId, c.targets, c.defaultActionName});
                m_ids.insert(key, id);
            });
}

void PortalRelay::removeNotification(const QString &appId, const QString &portalId)
{
    const QString key = keyFor(appId, portalId);
    const auto it = m_ids.constFind(key);
    if (it == m_ids.cend()) {
        if (m_inFlight.contains(key)) {
            m_withdrawn.insert(key);
        }
        return;
    }
    const uint id = it.value();
    QDBusMessage close = QDBusMessage::createMethodCall(kFdoService, kFdoPath, kFdoIface,
                                                        QStringLiteral("CloseNotification"));
    close.setArguments({id});
    QDBusConnection::sessionBus().call(close, QDBus::NoBlock);
    forget(id);
}

void PortalRelay::onActivationToken(uint id, const QString &token)
{
    if (m_entries.contains(id)) {
        m_tokens.insert(id, token);
    }
}

void PortalRelay::onActionInvoked(uint id, const QString &key)
{
    const auto it = m_entries.constFind(id);
    if (it == m_entries.cend()) {
        return;   // a notification some other application sent
    }
    const Entry entry = it.value();
    const QString name = key == QLatin1String("default") ? entry.defaultActionName : key;
    if (name.isEmpty()) {
        return;
    }
    dispatchAction(entry.appId, entry.portalId, name, entry.targets.value(key), QString(), m_tokens.take(id));
}

void PortalRelay::onClosed(uint id, uint reason)
{
    Q_UNUSED(reason)
    forget(id);
}

void PortalRelay::forget(uint id)
{
    const auto it = m_entries.constFind(id);
    if (it != m_entries.cend()) {
        m_ids.remove(keyFor(it->appId, it->portalId));
        m_entries.erase(it);
    }
    m_tokens.remove(id);
}
