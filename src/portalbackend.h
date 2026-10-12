/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    What both kinds of portal backend share: the D-Bus name and object, the
    adaptor, and getting a clicked action back to the application.

    There are two kinds because glassosd can be asked to answer the portal
    when it is not the session's notification daemon. portals.conf names a
    backend and xdg-desktop-portal starts it by D-Bus activation, whatever
    else is true — so a configuration left behind after someone goes back to
    Plasma's notifications still starts glassosd. If glassosd then declined
    the name, the portal would wait for an answer that never came: 25 seconds
    of stalled startup, measured, and an outright failure when it happens
    twice inside systemd's 45-second limit. That is the bug a packager hit.

    So glassosd always answers. When it is the notification daemon it handles
    portal notifications itself (PortalServer, version 2). When it is not, it
    relays them to whoever is (PortalRelay, version 1), which also keeps every
    notification in the one daemon the user chose rather than splitting them.
*/
#pragma once

#include "notification.h"

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QObject>
#include <QString>
#include <QVariantMap>

/* Wire handling shared by the two backends. */
namespace PortalWire
{
/* An encoded png, jpeg or svg — already validated by the portal — as the
   raw pixels glassosd and the freedesktop image-data hint both carry. */
ImageData decodeImage(const QByteArray &bytes);
/* The contents of a sealed memfd handed over by the portal, from offset 0. */
QByteArray readFd(const QVariant &value);
/* A (sv) pair, the shape the portal uses for icons and sounds. */
bool readPair(const QVariant &value, QString *kind, QVariant *inner);
/* Four portal priorities onto three urgencies. */
Urgency urgencyFor(const QString &priority);
/* The body as freedesktop markup: markup-body sanitised, or plain body
   escaped so a message containing "<3" stays text. */
QString bodyFrom(const QVariantMap &unwrapped);
/* Every value of an a{sv}, unwrapped from its variant. */
QVariantMap unwrapAll(const QVariantMap &props);

/* Whether the user's portals.conf sends notifications to glassosd. Read the
   way xdg-desktop-portal reads it: desktop-specific files before the generic
   one, the user's before the system's, and the first file that names a
   notification backend at all decides. */
bool routedHere();
} // namespace PortalWire

class PortalBackend : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    using QObject::QObject;

    /* Whether the call being handled may be acted on. The backend interface
       is xdg-desktop-portal's alone: the portal checks the sandbox and hands
       over an app id it has verified. Anything else on the bus could call
       this object directly and name any app id it liked, so a D-Bus call is
       only taken from the current owner of org.freedesktop.portal.Desktop,
       and anyone else is answered with AccessDenied. A call that did not
       come over D-Bus is the process calling itself, and is taken. */
    bool admitCaller();

    /* Claims org.freedesktop.impl.portal.desktop.glassosd and exports the
       adaptor. Returns false if the name is already owned. */
    bool start();

    virtual void addNotification(const QString &appId, const QString &portalId, const QVariantMap &props) = 0;
    virtual void removeNotification(const QString &appId, const QString &portalId) = 0;

    /* Read by the portal once, while it builds its proxy. Version 1 makes the
       frontend strip everything the freedesktop interface cannot carry, which
       is exactly right for a relay. */
    virtual uint version() const = 0;
    virtual QVariantMap supportedOptions() const = 0;

Q_SIGNALS:
    /* Only for actions the application did not export; see dispatchAction(). */
    void actionInvoked(const QString &appId,
                       const QString &portalId,
                       const QString &action,
                       const QVariantList &parameter);

protected:
    /* A click, delivered back to the application that sent the notification.
       An "app."-prefixed action is one the application exported, and the
       portal frontend does not route those — the backend activates the
       application itself, which is what lets a notification outlive the
       process that sent it. Anything else goes back as ActionInvoked. */
    void dispatchAction(const QString &appId,
                        const QString &portalId,
                        const QString &name,
                        const QVariant &target,
                        const QString &response,
                        const QString &activationToken);

    /* For the startup log line: what this backend does with what it gets. */
    virtual const char *describe() const = 0;
};

class PortalNotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Notification")
    Q_PROPERTY(uint version READ version CONSTANT)
    Q_PROPERTY(QVariantMap SupportedOptions READ supportedOptions CONSTANT)
public:
    explicit PortalNotificationsAdaptor(PortalBackend *backend);

    uint version() const { return m_backend->version(); }
    QVariantMap supportedOptions() const { return m_backend->supportedOptions(); }

public Q_SLOTS:
    void AddNotification(const QString &app_id, const QString &id, const QVariantMap &notification);
    void RemoveNotification(const QString &app_id, const QString &id);

Q_SIGNALS:
    void ActionInvoked(const QString &app_id,
                       const QString &id,
                       const QString &action,
                       const QVariantList &parameter);

private:
    PortalBackend *m_backend;
};
