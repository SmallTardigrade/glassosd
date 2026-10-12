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
using namespace PortalWire;

namespace
{
/* The one button purpose we can honour: glassosd already renders an inline
   reply field, for the KDE x-kde-reply-* extension. The portal standardises
   the same affordance, so an application written for any desktop gets it. */
constexpr QLatin1String kReplyPurpose{"im.reply-with-text"};

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

} // namespace

PortalServer::PortalServer(NotificationServer *server, NotificationModel *model, QObject *parent)
    : PortalBackend(parent)
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

QVariantMap PortalServer::advertisedOptions()
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
    dispatchAction(entry.appId, entry.portalId, name, entry.targets.value(action), response, token);
}

void PortalServer::forget(uint id)
{
    m_entries.remove(id);
    m_tokens.remove(id);
}
