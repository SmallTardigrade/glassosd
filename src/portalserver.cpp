/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "portalserver.h"

#include "notificationmodel.h"
#include "notificationserver.h"

#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QStandardPaths>

using namespace NotificationWire;

namespace
{
constexpr QLatin1String kPath{"/org/freedesktop/portal/desktop"};
constexpr QLatin1String kService{"org.freedesktop.impl.portal.desktop.glassosd"};
/* The one button purpose we can honour: glassosd already renders an inline
   reply field, for the KDE x-kde-reply-* extension. The portal standardises
   the same affordance, so an application written for any desktop gets it. */
constexpr QLatin1String kReplyPurpose{"im.reply-with-text"};

/* Decode an encoded image — png, jpeg or svg, already validated by the portal
   — into the raw-pixel form the rest of glassosd carries images in. */
ImageData decodeImage(const QByteArray &bytes)
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

/* Read a sealed memfd handed over by the portal. Bounded because the fd comes
   from outside: the portal's own icon limit is well under this, and a backend
   that will read any length is a backend that can be made to exhaust memory. */
constexpr qint64 kMaxFdBytes = 8 * 1024 * 1024;

QByteArray readFd(const QVariant &value)
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
       A descriptor carries a position, and an application that wrote its
       sound into a memfd and passed it straight over hands us one sitting at
       EOF — reading from there returns nothing at all, silently. Icons
       happened to work because the portal builds those descriptors itself. */
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

/* A (sv) pair, the shape the portal uses for both icons and sounds.

   Accepted in either form a struct can take: still marshalled, as it arrives
   over the bus, or already converted to a two-element list. */
bool readPair(const QVariant &value, QString *kind, QVariant *inner)
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

/* Custom notification sounds arrive as a file descriptor, but the player runs
   detached and needs a path that outlives this call. Content-addressed, so an
   application that sends the same sound with every message writes it once. */
QString cacheSound(const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return {};
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/glassosd/portal-sounds");
    if (!QDir().mkpath(dir)) {
        return {};
    }
    const QString path = dir + QLatin1Char('/')
        + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(32));
    if (QFileInfo::exists(path)) {
        return path;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        f.remove();
        return {};
    }
    f.close();

    /* Bounded, because the contents are chosen by the applications sending
       notifications rather than by us: one that sends a different sound every
       time would otherwise fill the cache a few kilobytes at a time, for as
       long as the session lasts. Oldest first — a sound still in use is
       rewritten by the next notification that names it. */
    constexpr int kMaxCachedSounds = 64;
    QDir d(dir);
    QFileInfoList kept = d.entryInfoList(QDir::Files, QDir::Time);
    for (int i = kMaxCachedSounds; i < kept.size(); ++i) {
        QFile::remove(kept.at(i).absoluteFilePath());
    }
    return path;
}

Urgency urgencyFor(const QString &priority)
{
    /* Four priorities onto three urgencies. "high" lands on Normal rather
       than Critical because Critical means "stays until dismissed" here, and
       the spec reserves that weight for "urgent". */
    if (priority == QLatin1String("low")) {
        return Urgency::Low;
    }
    if (priority == QLatin1String("urgent")) {
        return Urgency::Critical;
    }
    return Urgency::Normal;
}
} // namespace

PortalServer::PortalServer(NotificationServer *server, NotificationModel *model, QObject *parent)
    : QObject(parent)
    , m_server(server)
    , m_model(model)
{
    /* The same model signals the freedesktop adaptor listens to, filtered to
       ids this side minted. A portal id was never handed to a Notify() caller
       and vice versa, so the two adaptors never answer for each other. */
    connect(m_model, &NotificationModel::activationToken, this, [this](uint id, const QString &token) {
        if (owns(id)) {
            m_tokens.insert(id, token);
        }
    });
    connect(m_model, &NotificationModel::actionInvoked, this, [this](uint id, const QString &key) {
        if (owns(id)) {
            invoke(id, key, QString());
        }
    });
    connect(m_model, &NotificationModel::replied, this, [this](uint id, const QString &text) {
        if (!owns(id)) {
            return;
        }
        const auto it = m_entries.constFind(id);
        if (it != m_entries.cend() && !it->replyAction.isEmpty()) {
            invoke(id, it->replyAction, text);
        }
    });
    connect(m_model, &NotificationModel::notificationClosed, this, [this](uint id, uint) {
        if (owns(id)) {
            forget(id);
        }
    });
}

bool PortalServer::start()
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
    qInfo("glassosd: serving org.freedesktop.impl.portal.Notification (version 2)");
    return true;
}

QVariantMap PortalServer::supportedOptions()
{
    /* Read by applications through the frontend to decide what to send. Only
       claim what is actually rendered differently: a category we treat like
       any other buys the sender nothing. */
    return {
        {QStringLiteral("category"), QStringList{QStringLiteral("im.received")}},
        {QStringLiteral("button-purpose"), QStringList{QString(kReplyPurpose)}},
    };
}

void PortalServer::reserveIds(uint highest)
{
    if (owns(highest) && highest >= m_nextId) {
        m_nextId = highest + 1;
    }
}

uint PortalServer::mint(const QString &appId, const QString &portalId, bool *replacing)
{
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->appId == appId && it->portalId == portalId) {
            *replacing = true;
            return it.key();
        }
    }
    *replacing = false;
    /* Never wrap back into the freedesktop range. */
    if (m_nextId == 0 || !owns(m_nextId)) {
        m_nextId = idMask + 1;
    }
    return m_nextId++;
}

void PortalServer::addNotification(const QString &appId,
                                   const QString &portalId,
                                   const QVariantMap &props)
{
    if (portalId.isEmpty()) {
        return;
    }

    /* Every value in a D-Bus vardict arrives wrapped in a variant. Unwrapping
       once here is what lets the rest of this read like a plain map. */
    QVariantMap p;
    for (auto it = props.cbegin(); it != props.cend(); ++it) {
        p.insert(it.key(), unwrap(it.value()));
    }

    bool replacing = false;
    uint id = mint(appId, portalId, &replacing);

    /* "show-as-new" asks for the old card to go away and a new one to arrive,
       rather than the text changing underneath the reader. Closing first is
       what makes the difference visible. */
    const QStringList hints = p.value(QStringLiteral("display-hint")).toStringList();
    const bool showAsNew = hints.contains(QLatin1String("show-as-new"));
    if (replacing && showAsNew) {
        m_server->handleClose(id);
        forget(id);
        replacing = false;
        id = mint(appId, portalId, &replacing);
    }

    Notification n;
    n.id = id;
    /* Derived by the portal from the sandbox, not claimed by the sender.
       This is the key rules match on, and on this path it is trustworthy. */
    n.desktopEntry = appId;
    n.summary = sanitiseMarkup(p.value(QStringLiteral("title")).toString());

    /* markup-body carries the same tag subset the freedesktop body does.
       Plain body does not, so it is escaped: a message containing "<3" is
       text, not a broken tag. */
    const QString markup = p.value(QStringLiteral("markup-body")).toString();
    if (!markup.isEmpty()) {
        n.body = sanitiseMarkup(markup);
    } else {
        n.body = p.value(QStringLiteral("body")).toString().toHtmlEscaped();
        n.body.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    }

    n.urgency = urgencyFor(p.value(QStringLiteral("priority")).toString());
    n.category = p.value(QStringLiteral("category")).toString();

    Entry entry;
    entry.appId = appId;
    entry.portalId = portalId;

    /* Icon. Version 2 sends a themed name or a file descriptor; the bytes
       form is deprecated but still accepted, and a bare string is the
       historical spelling of a single themed name. */
    const QVariant iconValue = p.value(QStringLiteral("icon"));
    QString iconKind;
    QVariant iconInner;
    if (iconValue.typeId() == QMetaType::QString) {
        n.appIcon = iconValue.toString();
    } else if (readPair(iconValue, &iconKind, &iconInner)) {
        const QString &kind = iconKind;
        const QVariant &inner = iconInner;
        if (kind == QLatin1String("themed")) {
            const QStringList names = inner.toStringList();
            n.appIcon = names.value(0);
            for (const QString &name : names) {
                if (QIcon::hasThemeIcon(name)) {
                    n.appIcon = name;
                    break;
                }
            }
        } else if (kind == QLatin1String("bytes")) {
            n.image = decodeImage(inner.toByteArray());
        } else if (kind == QLatin1String("file-descriptor")) {
            n.image = decodeImage(readFd(inner));
        }
    }

    /* Sound. "default" leaves the choice to category and urgency, exactly as
       a notification that named none would. */
    const QVariant soundValue = p.value(QStringLiteral("sound"));
    if (soundValue.typeId() == QMetaType::QString) {
        if (soundValue.toString() == QLatin1String("silent")) {
            n.sound = QStringLiteral("none");
        }
    } else {
        QString soundKind;
        QVariant soundInner;
        if (readPair(soundValue, &soundKind, &soundInner)
            && soundKind == QLatin1String("file-descriptor")) {
            n.sound = cacheSound(readFd(soundInner));
        }
    }

    /* Display hints. "persistent" is honoured as "does not expire", which is
       the part of it that maps onto anything here — glassosd has no notion of
       a notification the user is forbidden to dismiss, and inventing one
       would be worse than not having it. */
    if (hints.contains(QLatin1String("transient"))) {
        n.transientHint = true;
    }
    if (hints.contains(QLatin1String("tray"))) {
        n.skipDisplay = true;
    }
    if (hints.contains(QLatin1String("persistent"))) {
        n.timeoutMs = 0;
    }
    /* Hiding the whole notification is the stronger request, so it wins if a
       sender somehow asks for both. */
    if (hints.contains(QLatin1String("hide-on-lockscreen"))) {
        n.lockPrivacy = Notification::LockPrivacy::Hide;
    } else if (hints.contains(QLatin1String("hide-content-on-lockscreen"))) {
        n.lockPrivacy = Notification::LockPrivacy::HideContent;
    }

    /* Actions. The freedesktop wire format is a flat [key, label, ...] list
       and the rest of glassosd reads that, so the portal's richer buttons are
       flattened into it and the extra parts kept here. */
    const QVariant defaultAction = p.value(QStringLiteral("default-action"));
    if (!defaultAction.toString().isEmpty()) {
        /* "default" is what activating the body means everywhere else in
           glassosd, so the sender's name for it is remembered rather than
           exposed. */
        n.actions << QStringLiteral("default") << QString();
        entry.targets.insert(QStringLiteral("default"),
                             p.value(QStringLiteral("default-action-target")));
        entry.targets.insert(QStringLiteral("default-action-name"), defaultAction);
    }

    /* Read from either shape an aa{sv} can take: still marshalled, as it
       arrives over the bus, or already converted to a list of maps. */
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
    {
        for (const QVariantMap &raw : buttons) {
            QVariantMap button;
            for (auto bit = raw.cbegin(); bit != raw.cend(); ++bit) {
                button.insert(bit.key(), unwrap(bit.value()));
            }
            const QString action = button.value(QStringLiteral("action")).toString();
            if (action.isEmpty()) {
                continue;
            }
            entry.targets.insert(action, button.value(QStringLiteral("target")));

            if (button.value(QStringLiteral("purpose")).toString() == kReplyPurpose) {
                /* Rendered as the reply field rather than a button, which is
                   what the purpose is asking for. A label is optional here
                   precisely because the server is expected to know better. */
                entry.replyAction = action;
                n.inlineReply = true;
                n.replySubmitText = button.value(QStringLiteral("label")).toString();
                continue;
            }
            const QString label = button.value(QStringLiteral("label")).toString();
            if (label.isEmpty()) {
                /* A button with a purpose we do not understand and no label
                   is one the spec says to drop, not to render blank. */
                continue;
            }
            n.actions << action << label;
        }
    }

    m_entries.insert(id, entry);
    m_server->deliver(n);
}

void PortalServer::removeNotification(const QString &appId, const QString &portalId)
{
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->appId == appId && it->portalId == portalId) {
            const uint id = it.key();
            m_server->handleClose(id);
            forget(id);
            return;
        }
    }
}

void PortalServer::invoke(uint id, const QString &action, const QString &response)
{
    const auto it = m_entries.constFind(id);
    if (it == m_entries.cend()) {
        return;
    }
    const Entry entry = it.value();
    const QString token = m_tokens.take(id);

    /* The name the sender gave its default action, recovered now that the
       click has happened. */
    QString name = action;
    if (action == QLatin1String("default")) {
        name = entry.targets.value(QStringLiteral("default-action-name")).toString();
        if (name.isEmpty()) {
            return;
        }
    }
    const QVariant target = entry.targets.value(action);

    QVariantMap platformData;
    if (!token.isEmpty()) {
        platformData.insert(QStringLiteral("activation-token"), token);
    }

    /* An "app."-prefixed action is one the application exported, and the
       portal frontend does not route these — the backend is expected to
       activate the application itself, which is what lets a notification
       outlive the process that sent it. */
    constexpr QLatin1String appPrefix{"app."};
    if (name.startsWith(appPrefix)) {
        QString path = entry.appId;
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

        QDBusMessage message = QDBusMessage::createMethodCall(entry.appId,
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
    Q_EMIT actionInvoked(entry.appId, entry.portalId, name, params);
}

void PortalServer::forget(uint id)
{
    m_entries.remove(id);
    m_tokens.remove(id);
}

PortalNotificationsAdaptor::PortalNotificationsAdaptor(PortalServer *server)
    : QDBusAbstractAdaptor(server)
    , m_server(server)
{
    setAutoRelaySignals(false);
    connect(server, &PortalServer::actionInvoked, this, &PortalNotificationsAdaptor::ActionInvoked);
}

void PortalNotificationsAdaptor::AddNotification(const QString &app_id,
                                                 const QString &id,
                                                 const QVariantMap &notification)
{
    m_server->addNotification(app_id, id, notification);
}

void PortalNotificationsAdaptor::RemoveNotification(const QString &app_id, const QString &id)
{
    m_server->removeNotification(app_id, id);
}
