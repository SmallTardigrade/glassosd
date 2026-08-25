/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "mpriscontroller.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusReply>
#include <QDBusVariant>
#include <QStringList>

namespace
{
constexpr const char *kPath = "/org/mpris/MediaPlayer2";
constexpr const char *kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr const char *kRootIface = "org.mpris.MediaPlayer2";
constexpr const char *kPrefix = "org.mpris.MediaPlayer2.";

/* QDBusInterface::property() cannot read a{sv} without the type being
   registered, and fails with "QDBusRawType ... must be registered". Going
   through Properties.Get and demarshalling by hand avoids the registration
   dance entirely. */
QVariant readProperty(const QString &service, const QString &iface, const QString &name)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, QLatin1String(kPath),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    msg << iface << name;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    QVariant v = reply.arguments().first();
    if (v.canConvert<QDBusVariant>()) {
        v = v.value<QDBusVariant>().variant();
    }
    return v;
}

QVariantMap readMetadata(const QString &service)
{
    const QVariant v = readProperty(service, QLatin1String(kPlayerIface), QStringLiteral("Metadata"));
    if (v.canConvert<QDBusArgument>()) {
        QVariantMap out;
        const QDBusArgument arg = v.value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            QString key;
            QDBusVariant value;
            arg.beginMapEntry();
            arg >> key >> value;
            arg.endMapEntry();
            out.insert(key, value.variant());
        }
        arg.endMap();
        return out;
    }
    return v.toMap();
}
} // namespace

MprisController::MprisController(QObject *parent)
    : QObject(parent)
{
    /* Watch for players coming and going rather than polling. */
    m_watcher.setConnection(QDBusConnection::sessionBus());
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_watcher.addWatchedService(QStringLiteral("org.mpris.MediaPlayer2*"));
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
        findPlayer();
    });

    /* Property changes arrive as a standard PropertiesChanged signal. */
    QDBusConnection::sessionBus().connect(QString(), QLatin1String(kPath),
                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QStringLiteral("PropertiesChanged"),
                                          this, SLOT(refresh()));
    findPlayer();
}

void MprisController::findPlayer()
{
    const QDBusReply<QStringList> reply =
        QDBusConnection::sessionBus().interface()->registeredServiceNames();
    QString chosen;
    if (reply.isValid()) {
        for (const QString &name : reply.value()) {
            if (!name.startsWith(QLatin1String(kPrefix))) {
                continue;
            }
            chosen = name;
            /* Prefer a player that is actually playing over one merely open. */
            if (readProperty(name, QLatin1String(kPlayerIface),
                             QStringLiteral("PlaybackStatus")).toString()
                == QLatin1String("Playing")) {
                break;
            }
        }
    }
    m_service = chosen;
    refresh();
}

void MprisController::refresh()
{
    if (m_service.isEmpty()) {
        m_title.clear();
        m_artist.clear();
        m_artUrl.clear();
        m_identity.clear();
        m_playing = false;
        Q_EMIT changed();
        return;
    }

    m_playing = readProperty(m_service, QLatin1String(kPlayerIface),
                             QStringLiteral("PlaybackStatus")).toString()
        == QLatin1String("Playing");

    const QVariantMap meta = readMetadata(m_service);
    m_title = meta.value(QStringLiteral("xesam:title")).toString();
    /* xesam:artist is a list; join it rather than showing only the first. */
    m_artist = meta.value(QStringLiteral("xesam:artist")).toStringList().join(QStringLiteral(", "));
    m_artUrl = meta.value(QStringLiteral("mpris:artUrl")).toString();

    m_identity = readProperty(m_service, QLatin1String(kRootIface),
                              QStringLiteral("Identity")).toString();

    Q_EMIT changed();
}

void MprisController::call(const QString &method)
{
    if (m_service.isEmpty()) {
        return;
    }
    QDBusInterface player(m_service, QLatin1String(kPath), QLatin1String(kPlayerIface),
                          QDBusConnection::sessionBus());
    player.asyncCall(method);
}

void MprisController::playPause() { call(QStringLiteral("PlayPause")); }
void MprisController::next()      { call(QStringLiteral("Next")); }
void MprisController::previous()  { call(QStringLiteral("Previous")); }
