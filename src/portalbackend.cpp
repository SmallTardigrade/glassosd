/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "portalbackend.h"

#include "notificationserver.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>

using namespace NotificationWire;

namespace
{
constexpr QLatin1String kPath{"/org/freedesktop/portal/desktop"};
constexpr QLatin1String kService{"org.freedesktop.impl.portal.desktop.glassosd"};
constexpr QLatin1String kNotificationIface{"org.freedesktop.impl.portal.Notification"};

/* Read a sealed memfd handed over by the portal. Bounded because the fd comes
   from outside: the portal's own icon limit is well under this, and a backend
   that will read any length is a backend that can be made to exhaust memory. */
constexpr qint64 kMaxFdBytes = 8 * 1024 * 1024;

/* Whether a .portal file of this name is installed and declares the
   notification interface — the same test the portal applies before it will
   choose a backend named in portals.conf. */
bool portalInstalled(const QString &name)
{
    QStringList dirs{QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)};
    dirs += QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dir : std::as_const(dirs)) {
        const QString path = dir + QStringLiteral("/xdg-desktop-portal/portals/") + name
            + QStringLiteral(".portal");
        if (!QFileInfo::exists(path)) {
            continue;
        }
        QSettings f(path, QSettings::IniFormat);
        const QString ifaces = f.value(QStringLiteral("portal/Interfaces")).toStringList().join(QLatin1Char(';'));
        return ifaces.contains(kNotificationIface);
    }
    return false;
}

/* One file per directory, as the portal reads them: the first
   <desktop>-portals.conf that exists for any current desktop, otherwise
   portals.conf. A file only counts if it has a [preferred] group. */
QString configFileIn(const QString &dir, const QStringList &desktops)
{
    QStringList names;
    for (const QString &d : desktops) {
        names << d + QStringLiteral("-portals.conf");
    }
    names << QStringLiteral("portals.conf");
    for (const QString &n : std::as_const(names)) {
        const QString path = dir + QLatin1Char('/') + n;
        if (!QFileInfo::exists(path)) {
            continue;
        }
        QSettings f(path, QSettings::IniFormat);
        if (f.childGroups().contains(QStringLiteral("preferred"))) {
            return path;
        }
    }
    return {};
}
} // namespace

ImageData PortalWire::decodeImage(const QByteArray &bytes)
{
    QImage img;
    if (bytes.isEmpty() || !img.loadFromData(bytes)) {
        return {};
    }
    img = img.convertToFormat(QImage::Format_RGBA8888);

    ImageData out;
    out.width = img.width();
    out.height = img.height();
    out.rowstride = int(img.bytesPerLine());
    out.hasAlpha = true;
    out.bitsPerSample = 8;
    out.channels = 4;
    out.data = QByteArray(reinterpret_cast<const char *>(img.constBits()), qsizetype(img.sizeInBytes()));
    return out;
}

QByteArray PortalWire::readFd(const QVariant &value)
{
    if (!value.canConvert<QDBusUnixFileDescriptor>()) {
        return {};
    }
    const QDBusUnixFileDescriptor fd = value.value<QDBusUnixFileDescriptor>();
    if (!fd.isValid()) {
        return {};
    }
    QFile f;
    if (!f.open(fd.fileDescriptor(), QIODevice::ReadOnly, QFileDevice::DontCloseHandle)) {
        return {};
    }
    /* Read from the start, not from wherever the sender left the offset.
       A descriptor carries a position, and an application that writes its
       sound into a memfd and passes it straight over hands us one sitting at
       EOF — reading from there returns nothing, silently. Icons hid this:
       the portal builds those descriptors itself and they arrive rewound. */
    if (!f.seek(0)) {
        return {};
    }
    return f.read(kMaxFdBytes);
}

/* Accepted in either form a struct can take: still marshalled, as it arrives
   over the bus, or already converted to a two-element list. */
bool PortalWire::readPair(const QVariant &value, QString *kind, QVariant *inner)
{
    if (!value.canConvert<QDBusArgument>()) {
        const QVariantList pair = value.toList();
        if (pair.size() != 2 || pair.first().typeId() != QMetaType::QString) {
            return false;
        }
        *kind = pair.first().toString();
        *inner = unwrap(pair.last());
        return true;
    }
    const QDBusArgument arg = value.value<QDBusArgument>();
    if (arg.currentSignature() != QLatin1String("(sv)")) {
        return false;
    }
    QDBusVariant wrapped;
    arg.beginStructure();
    arg >> *kind >> wrapped;
    arg.endStructure();
    *inner = wrapped.variant();
    return true;
}

Urgency PortalWire::urgencyFor(const QString &priority)
{
    /* "high" lands on Normal rather than Critical because Critical means
       "stays until dismissed" here, and the spec reserves that weight for
       "urgent". */
    if (priority == QLatin1String("low")) {
        return Urgency::Low;
    }
    if (priority == QLatin1String("urgent")) {
        return Urgency::Critical;
    }
    return Urgency::Normal;
}

QVariantMap PortalWire::unwrapAll(const QVariantMap &props)
{
    QVariantMap out;
    for (auto it = props.cbegin(); it != props.cend(); ++it) {
        out.insert(it.key(), unwrap(it.value()));
    }
    return out;
}

bool PortalWire::routedHere()
{
    QStringList desktops;
    const QStringList current = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &d : current) {
        desktops << d.toLower();
    }

    /* The portal's own search order: the user's config, the system's config
       directories, /etc, then the data directories. */
    QStringList dirs;
    dirs << QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QStringList configDirs = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    dirs << configDirs.mid(1);
    dirs << QStringLiteral("/etc");
    dirs << QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);

    const QString iface = QStringLiteral("preferred/") + kNotificationIface;
    for (const QString &base : std::as_const(dirs)) {
        const QString path = configFileIn(base + QStringLiteral("/xdg-desktop-portal"), desktops);
        if (path.isEmpty()) {
            continue;
        }
        QSettings f(path, QSettings::IniFormat);
        /* QSettings reads "a;b" as a single string; the portal reads it as a
           list, and so must this. */
        QString value = f.value(iface).toStringList().join(QLatin1Char(';'));
        if (value.isEmpty()) {
            value = f.value(QStringLiteral("preferred/default")).toStringList().join(QLatin1Char(';'));
        }
        const QStringList names = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        if (names.contains(QLatin1String("none"))) {
            return false;
        }
        /* The first name that is actually installed wins; a file naming
           nothing installable falls through to the next directory. */
        for (const QString &raw : names) {
            const QString name = raw.trimmed();
            if (portalInstalled(name)) {
                return name == QLatin1String("glassosd");
            }
        }
    }
    return false;
}

bool PortalBackend::start()
{
    new PortalNotificationsAdaptor(this);

    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(kPath, this)) {
        qWarning("glassosd: could not register the portal notification object");
        return false;
    }
    if (!bus.registerService(kService)) {
        qWarning("glassosd: org.freedesktop.impl.portal.desktop.glassosd is already "
                 "owned; the portal backend stays with the existing owner");
        bus.unregisterObject(kPath);
        return false;
    }
    qInfo("glassosd: %s (org.freedesktop.impl.portal.Notification version %u)", describe(), version());
    return true;
}

void PortalBackend::dispatchAction(const QString &appId,
                                   const QString &portalId,
                                   const QString &name,
                                   const QVariant &target,
                                   const QString &response,
                                   const QString &activationToken)
{
    QVariantMap platformData;
    if (!activationToken.isEmpty()) {
        platformData.insert(QStringLiteral("activation-token"), activationToken);
    }

    constexpr QLatin1String appPrefix{"app."};
    if (name.startsWith(appPrefix)) {
        QString path = appId;
        path.replace(QLatin1Char('.'), QLatin1Char('/'));
        path.replace(QLatin1Char('-'), QLatin1Char('_'));
        path.prepend(QLatin1Char('/'));

        QVariantList params;
        if (target.isValid()) {
            params.append(target);
        }
        if (!response.isEmpty()) {
            params.append(response);
        }

        QDBusMessage message = QDBusMessage::createMethodCall(appId,
                                                              path,
                                                              QStringLiteral("org.freedesktop.Application"),
                                                              QStringLiteral("ActivateAction"));
        message.setArguments({name.mid(appPrefix.size()), params, platformData});
        QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
        return;
    }

    /* Order is fixed by the interface: target first if there was one, then
       platform data, then the user's response if the purpose produced one. */
    QVariantList params;
    if (target.isValid()) {
        params.append(target);
    }
    params.append(platformData);
    if (!response.isEmpty()) {
        params.append(response);
    }
    Q_EMIT actionInvoked(appId, portalId, name, params);
}

PortalNotificationsAdaptor::PortalNotificationsAdaptor(PortalBackend *backend)
    : QDBusAbstractAdaptor(backend)
    , m_backend(backend)
{
    setAutoRelaySignals(false);
    connect(backend, &PortalBackend::actionInvoked, this, &PortalNotificationsAdaptor::ActionInvoked);
}

void PortalNotificationsAdaptor::AddNotification(const QString &app_id,
                                                 const QString &id,
                                                 const QVariantMap &notification)
{
    m_backend->addNotification(app_id, id, notification);
}

void PortalNotificationsAdaptor::RemoveNotification(const QString &app_id, const QString &id)
{
    m_backend->removeNotification(app_id, id);
}
