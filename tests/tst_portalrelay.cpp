/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    The portal relay: what glassosd does with portal notifications when it is
    not the session's notification daemon.

    It exists because of a real failure. A packager stopped using glassosd and
    went back to Plasma's notifications, but portals.conf still named
    glassosd. The portal D-Bus-activated it at every start, glassosd declined
    the portal name because another daemon owned the notifications, and the
    portal sat waiting for an answer — 25 seconds per attempt, then systemd
    gave up on it entirely.

    The first half of this file tests the translation and needs no bus. The
    second half runs the relay against a fake notification daemon on a real
    bus, and only runs inside the private session bus ctest starts for it:
    against a desktop session's bus it would be trying to take the
    notification name from whatever daemon is showing the user's
    notifications.
*/
#include "portalrelay.h"
#include "testenv.h"

#include <QBuffer>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QImage>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>

/* A macro, not a helper function: QSKIP returns from the function it is
   written in, so a helper that skipped would return to a test that then
   carried on — onto the desktop's real bus. That happened once. */
#define NEED_PRIVATE_BUS()                                                                    \
    if (!onPrivateBus())                                                                      \
    QSKIP("needs the private session bus ctest provides (tst_portalrelay_bus)")

namespace
{
QVariant v(const QVariant &value)
{
    return QVariant::fromValue(QDBusVariant(value));
}

QVariantMap button(const QString &action, const QString &label)
{
    QVariantMap b{{QStringLiteral("action"), v(action)}};
    if (!label.isNull()) {
        b.insert(QStringLiteral("label"), v(label));
    }
    return b;
}

QByteArray pngBytes(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(Qt::magenta);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

bool onPrivateBus()
{
    return qEnvironmentVariableIsSet("GLASSOSD_PRIVATE_BUS")
        && QDBusConnection::sessionBus().isConnected();
}
} // namespace

/* Stands in for Plasma, dunst, or whatever else owns the name. Exported
   directly rather than through an adaptor, which is all a test needs. */
class FakeDaemon : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    struct Call {
        uint replaces;
        QString icon;
        QString summary;
        QString body;
        QStringList actions;
        QVariantMap hints;
    };
    QList<Call> calls;
    QList<uint> closed;
    uint next = 100;

public Q_SLOTS:
    uint Notify(const QString &, uint replaces_id, const QString &app_icon, const QString &summary,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int)
    {
        calls << Call{replaces_id, app_icon, summary, body, actions, hints};
        return replaces_id ? replaces_id : next++;
    }
    void CloseNotification(uint id) { closed << id; }

Q_SIGNALS:
    void ActionInvoked(uint id, const QString &action_key);
    void NotificationClosed(uint id, uint reason);
    void ActivationToken(uint id, const QString &activation_token);
};

class TestPortalRelay : public QObject
{
    Q_OBJECT

private:
    /* The fake gets a connection of its own, so the relay talks to it across
       the bus the way it would to another process. */
    QDBusConnection m_daemonBus{QStringLiteral("unconnected")};
    FakeDaemon *m_fake = nullptr;


private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<ImageData>();
        if (!onPrivateBus()) {
            return;
        }
        m_daemonBus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-daemon"));
        QVERIFY(m_daemonBus.isConnected());
        m_fake = new FakeDaemon;
        QVERIFY(m_daemonBus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), m_fake,
                                           QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        QVERIFY(m_daemonBus.registerService(QStringLiteral("org.freedesktop.Notifications")));
    }

    void cleanupTestCase()
    {
        if (m_fake) {
            m_daemonBus.unregisterService(QStringLiteral("org.freedesktop.Notifications"));
            delete m_fake;
            QDBusConnection::disconnectFromBus(QStringLiteral("fake-daemon"));
        }
    }

    void init()
    {
        if (m_fake) {
            m_fake->calls.clear();
            m_fake->closed.clear();
            m_fake->next = 100;
        }
    }

    // ---- translation -------------------------------------------------------

    /* The portal's verified identity is the point of the portal path; the
       daemon on the far side recovers the name and icon from it. */
    void appIdTravelsAsTheDesktopEntry()
    {
        const auto c = PortalRelay::translate(QStringLiteral("com.spotify.Client"),
                                              {{QStringLiteral("title"), v(QStringLiteral("x"))}});
        QCOMPARE(c.hints.value(QStringLiteral("desktop-entry")).toString(), QStringLiteral("com.spotify.Client"));
    }

    /* The freedesktop body is markup, so plain text must be escaped to stay
       text — but newlines are kept as newlines, not turned into a tag the
       spec does not list. */
    void plainBodyIsEscapedAndKeepsNewlines()
    {
        const auto c = PortalRelay::translate(QStringLiteral("a"),
                                              {{QStringLiteral("title"), v(QStringLiteral("Subject"))},
                                               {QStringLiteral("body"), v(QStringLiteral("i <3 this\nsecond line"))}});
        QCOMPARE(c.summary, QStringLiteral("Subject"));
        QCOMPARE(c.body, QStringLiteral("i &lt;3 this\nsecond line"));
    }

    void priorityBecomesTheUrgencyByte_data()
    {
        QTest::addColumn<QString>("priority");
        QTest::addColumn<int>("urgency");
        QTest::newRow("low") << QStringLiteral("low") << 0;
        QTest::newRow("normal") << QStringLiteral("normal") << 1;
        QTest::newRow("high") << QStringLiteral("high") << 1;
        QTest::newRow("urgent") << QStringLiteral("urgent") << 2;
        QTest::newRow("absent") << QString() << 1;
    }

    void priorityBecomesTheUrgencyByte()
    {
        QFETCH(QString, priority);
        QFETCH(int, urgency);
        const auto c = PortalRelay::translate(QStringLiteral("a"),
                                              {{QStringLiteral("priority"), v(priority)}});
        const QVariant u = c.hints.value(QStringLiteral("urgency"));
        /* A byte, not an int: daemons that check the type, as the spec
           allows, would otherwise ignore it. */
        QCOMPARE(u.typeId(), int(QMetaType::UChar));
        QCOMPARE(int(u.value<uchar>()), urgency);
    }

    void themedIconBecomesTheAppIcon()
    {
        auto c = PortalRelay::translate(QStringLiteral("a"),
                                        {{QStringLiteral("icon"), v(QStringLiteral("mail-message-new"))}});
        QCOMPARE(c.appIcon, QStringLiteral("mail-message-new"));

        c = PortalRelay::translate(QStringLiteral("a"),
                                   {{QStringLiteral("icon"),
                                     v(QVariantList{QStringLiteral("themed"),
                                                    QVariant(QStringList{QStringLiteral("first"),
                                                                         QStringLiteral("second")})})}});
        QCOMPARE(c.appIcon, QStringLiteral("first"));
    }

    /* A version 1 backend is sent icon bytes; the freedesktop way to carry
       pixels is the image-data hint. */
    void iconBytesBecomeImageData()
    {
        const auto c = PortalRelay::translate(QStringLiteral("a"),
                                              {{QStringLiteral("icon"),
                                                v(QVariantList{QStringLiteral("bytes"), pngBytes(24, 16)})}});
        QVERIFY(c.appIcon.isEmpty());
        const ImageData img = c.hints.value(QStringLiteral("image-data")).value<ImageData>();
        QVERIFY(img.isValid());
        QCOMPARE(img.width, 24);
        QCOMPARE(img.height, 16);
        QCOMPARE(img.channels, 4);
    }

    /* The portal already limits icons, but the backend reads whatever it is
       handed. More than the descriptor limit is refused outright, even when
       the image inside would decode. */
    void oversizedIconBytesAreRefused()
    {
        QByteArray bytes = pngBytes(8, 8);
        bytes.append(QByteArray(9 * 1024 * 1024, '\0'));   // a png decoder ignores what follows IEND
        const auto c = PortalRelay::translate(QStringLiteral("a"),
                                              {{QStringLiteral("icon"),
                                                v(QVariantList{QStringLiteral("bytes"), bytes})}});
        QVERIFY(!c.hints.contains(QStringLiteral("image-data")));
    }

    /* A large icon is scaled to fit on the way in, not refused: the picture
       is fine, there is just more of it than an icon needs. */
    void largeIconsAreScaledDown()
    {
        const auto c = PortalRelay::translate(QStringLiteral("a"),
                                              {{QStringLiteral("icon"),
                                                v(QVariantList{QStringLiteral("bytes"), pngBytes(2000, 1000)})}});
        const ImageData img = c.hints.value(QStringLiteral("image-data")).value<ImageData>();
        QVERIFY(img.isValid());
        QCOMPARE(img.width, ImageData::maxStoredEdge);
        QCOMPARE(img.height, ImageData::maxStoredEdge / 2);
    }

    void defaultActionBecomesTheDefaultKey()
    {
        const auto c = PortalRelay::translate(QStringLiteral("a"),
                                              {{QStringLiteral("default-action"), v(QStringLiteral("open-thread"))}});
        QCOMPARE(c.actions, QStringList({QStringLiteral("default"), QString()}));
        QCOMPARE(c.defaultActionName, QStringLiteral("open-thread"));
    }

    void labelledButtonsBecomeActionsAndUnlabelledOnesAreDropped()
    {
        const auto c = PortalRelay::translate(
            QStringLiteral("a"),
            {{QStringLiteral("buttons"),
              v(QVariantList{button(QStringLiteral("archive"), QStringLiteral("Archive")),
                             button(QStringLiteral("mystery"), QString())})}});
        QCOMPARE(c.actions, QStringList({QStringLiteral("archive"), QStringLiteral("Archive")}));
    }

    // ---- over the bus ------------------------------------------------------

    /* What the portal reads once, at startup, to decide what to strip. */
    void declaresVersionOne()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        QVERIFY(relay.start());
        QDBusMessage get = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.impl.portal.desktop.glassosd"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        get.setArguments({QStringLiteral("org.freedesktop.impl.portal.Notification"), QStringLiteral("version")});
        const QDBusMessage reply = m_daemonBus.call(get, QDBus::BlockWithGui);
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().value(0).value<QDBusVariant>().variant().toUInt(), 1u);
        QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.freedesktop.impl.portal.desktop.glassosd"));
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/org/freedesktop/portal/desktop"));
    }

    void notificationReachesTheDaemon()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        relay.addNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"),
                              {{QStringLiteral("title"), v(QStringLiteral("Hello"))},
                               {QStringLiteral("body"), v(QStringLiteral("World"))}});
        QTRY_COMPARE(m_fake->calls.size(), 1);
        const auto &call = m_fake->calls.first();
        QCOMPARE(call.summary, QStringLiteral("Hello"));
        QCOMPARE(call.body, QStringLiteral("World"));
        QCOMPARE(call.replaces, 0u);
        QCOMPARE(call.hints.value(QStringLiteral("desktop-entry")).toString(), QStringLiteral("org.example.App"));
    }

    /* "If the application reuses the same ID without withdrawing, the
       notification is updated" — so the daemon must be told to replace. */
    void reusingAnIdReplacesInsteadOfStacking()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        relay.addNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"),
                              {{QStringLiteral("title"), v(QStringLiteral("First"))}});
        QTRY_COMPARE(m_fake->calls.size(), 1);
        /* Let the reply land, so the relay knows the daemon's id. */
        QTest::qWait(50);
        relay.addNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"),
                              {{QStringLiteral("title"), v(QStringLiteral("Second"))}});
        QTRY_COMPARE(m_fake->calls.size(), 2);
        QCOMPARE(m_fake->calls.at(1).replaces, 100u);
    }

    void clickComesBackToTheApplication()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        QSignalSpy spy(&relay, &PortalBackend::actionInvoked);
        relay.addNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"),
                              {{QStringLiteral("title"), v(QStringLiteral("x"))},
                               {QStringLiteral("buttons"),
                                v(QVariantList{button(QStringLiteral("archive"), QStringLiteral("Archive"))})}});
        QTRY_COMPARE(m_fake->calls.size(), 1);
        QTest::qWait(50);

        Q_EMIT m_fake->ActivationToken(100, QStringLiteral("tok"));
        Q_EMIT m_fake->ActionInvoked(100, QStringLiteral("archive"));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("org.example.App"));
        QCOMPARE(spy.first().at(1).toString(), QStringLiteral("n1"));
        QCOMPARE(spy.first().at(2).toString(), QStringLiteral("archive"));
        /* No target was set, so platform data is the only parameter, and it
           carries the token so the application can raise its window. */
        const QVariantList params = spy.first().at(3).toList();
        QCOMPARE(params.size(), 1);
        QCOMPARE(params.first().toMap().value(QStringLiteral("activation-token")).toString(),
                 QStringLiteral("tok"));
    }

    /* Activating the body comes back under the sender's own name for it. */
    void defaultClickComesBackUnderTheSendersName()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        QSignalSpy spy(&relay, &PortalBackend::actionInvoked);
        relay.addNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"),
                              {{QStringLiteral("default-action"), v(QStringLiteral("open-thread"))}});
        QTRY_COMPARE(m_fake->calls.size(), 1);
        QTest::qWait(50);
        Q_EMIT m_fake->ActionInvoked(100, QStringLiteral("default"));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(2).toString(), QStringLiteral("open-thread"));
    }

    /* Clicks on other applications' notifications are none of its business. */
    void clicksOnOtherNotificationsAreIgnored()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        QSignalSpy spy(&relay, &PortalBackend::actionInvoked);
        Q_EMIT m_fake->ActionInvoked(4242, QStringLiteral("whatever"));
        QTest::qWait(100);
        QCOMPARE(spy.count(), 0);
    }

    void withdrawingClosesItOnTheDaemon()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        relay.addNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"),
                              {{QStringLiteral("title"), v(QStringLiteral("x"))}});
        QTRY_COMPARE(m_fake->calls.size(), 1);
        QTest::qWait(50);
        relay.removeNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"));
        QTRY_COMPARE(m_fake->closed, QList<uint>({100u}));
    }

    /* Withdrawn before the daemon has even answered: closed as soon as it
       does, rather than left on screen because its id was not known yet. */
    void withdrawingWhileInFlightStillCloses()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        relay.addNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"),
                              {{QStringLiteral("title"), v(QStringLiteral("x"))}});
        relay.removeNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"));
        QTRY_COMPARE(m_fake->closed, QList<uint>({100u}));
    }

    /* The backend interface is meant for xdg-desktop-portal alone, which
       checks the sandbox and passes on an app id it has verified. Anything
       else on the bus could call it directly and name any app id it liked,
       so calls are only taken from whoever owns the portal's name. */
    void onlyThePortalMayAddNotifications()
    {
        NEED_PRIVATE_BUS();
        PortalRelay relay;
        QVERIFY(relay.start());
        auto cleanup = qScopeGuard([] {
            QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.freedesktop.impl.portal.desktop.glassosd"));
            QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/org/freedesktop/portal/desktop"));
        });

        auto add = [](const QDBusConnection &from, const QString &title) {
            QDBusMessage m = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.impl.portal.desktop.glassosd"),
                QStringLiteral("/org/freedesktop/portal/desktop"),
                QStringLiteral("org.freedesktop.impl.portal.Notification"), QStringLiteral("AddNotification"));
            m.setArguments({QStringLiteral("org.example.App"), title,
                            QVariant::fromValue(QVariantMap{{QStringLiteral("title"), v(title)}})});
            return from.call(m, QDBus::BlockWithGui);
        };

        /* Someone other than the portal. */
        const QDBusMessage refused = add(m_daemonBus, QStringLiteral("forged"));
        QCOMPARE(refused.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(refused.errorName(), QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"));

        /* The portal. */
        QDBusConnection portal = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-portal"));
        auto dropPortal = qScopeGuard([] { QDBusConnection::disconnectFromBus(QStringLiteral("fake-portal")); });
        QVERIFY(portal.registerService(QStringLiteral("org.freedesktop.portal.Desktop")));
        const QDBusMessage accepted = add(portal, QStringLiteral("genuine"));
        QCOMPARE(accepted.type(), QDBusMessage::ReplyMessage);

        QTRY_COMPARE(m_fake->calls.size(), 1);
        QCOMPARE(m_fake->calls.first().summary, QStringLiteral("genuine"));
    }

    /* Nothing owns the name at all. The relay must still answer the portal —
       that is the whole fix — and simply fail the one notification. */
    void noDaemonDoesNotBreakTheRelay()
    {
        NEED_PRIVATE_BUS();
        QVERIFY(m_daemonBus.unregisterService(QStringLiteral("org.freedesktop.Notifications")));
        PortalRelay relay;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("could not relay a portal notification")));
        relay.addNotification(QStringLiteral("org.example.App"), QStringLiteral("n1"),
                              {{QStringLiteral("title"), v(QStringLiteral("x"))}});
        QTest::qWait(300);
        QCOMPARE(m_fake->calls.size(), 0);
        QVERIFY(m_daemonBus.registerService(QStringLiteral("org.freedesktop.Notifications")));
    }
};

GLASSOSD_TEST_MAIN(TestPortalRelay)
#include "tst_portalrelay.moc"
