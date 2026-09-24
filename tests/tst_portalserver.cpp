/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    The portal backend's translation layer.

    Everything here is the mapping from the portal's vardict onto the
    Notification the rest of glassosd carries. It is worth testing closely
    because it is the only place two specifications meet, and because the
    failures are quiet ones: a dropped field looks exactly like an application
    that never sent it.

    Nothing here opens a bus. start() is not called; addNotification() and
    removeNotification() are the same entry points the adaptor uses, so the
    translation is exercised without D-Bus being involved at all.
*/
#include "historymodel.h"
#include "notificationmodel.h"
#include "notificationserver.h"
#include "portalserver.h"
#include "testenv.h"

#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>

#include <sys/mman.h>
#include <unistd.h>

namespace
{
/* Values in an a{sv} arrive wrapped in a variant. Wrapping them here means
   the tests exercise the same unwrapping the bus would. */
QVariant v(const QVariant &value)
{
    return QVariant::fromValue(QDBusVariant(value));
}

QVariantMap button(const QString &action, const QString &label, const QString &purpose = {})
{
    QVariantMap b{{QStringLiteral("action"), v(action)}};
    if (!label.isNull()) {
        b.insert(QStringLiteral("label"), v(label));
    }
    if (!purpose.isEmpty()) {
        b.insert(QStringLiteral("purpose"), v(purpose));
    }
    return b;
}
} // namespace

class TestPortalServer : public QObject
{
    Q_OBJECT

private:
    /* One notification through the portal, returning the row the model ended
       up showing. The pieces are built fresh per test so nothing leaks
       between them. */
    struct Fixture {
        NotificationModel model;
        HistoryModel history;
        NotificationServer server{&model, &history};
        PortalServer portal{&server, &model};

        Fixture()
        {
            model.setCoalesce(0, 0);
            model.setTimeouts(0, 0, 0);
        }

        void add(const QString &appId, const QString &id, const QVariantMap &props)
        {
            portal.addNotification(appId, id, props);
        }

        QVariant role(int row, int r) const { return model.index(row).data(r); }
        int rows() const { return model.rowCount(); }

        /* ActionsRole is a list of {key, label} maps — the buttons actually
           drawn, with "default" and the reply field already excluded. */
        QStringList actionKeys(int row) const
        {
            QStringList out;
            const QVariantList list = role(row, NotificationModel::ActionsRole).toList();
            for (const QVariant &a : list) {
                out << a.toMap().value(QStringLiteral("key")).toString();
            }
            return out;
        }
        QStringList actionLabels(int row) const
        {
            QStringList out;
            const QVariantList list = role(row, NotificationModel::ActionsRole).toList();
            for (const QVariant &a : list) {
                out << a.toMap().value(QStringLiteral("label")).toString();
            }
            return out;
        }
    };

private Q_SLOTS:
    void titleAndBodyArrive()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Subject"))},
               {QStringLiteral("body"), v(QStringLiteral("Message"))}});

        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.role(0, NotificationModel::SummaryRole).toString(), QStringLiteral("Subject"));
        QCOMPARE(f.role(0, NotificationModel::BodyRole).toString(), QStringLiteral("Message"));
    }

    /* The portal's plain body is text, not markup. An application that says
       "<3" means "<3", and the freedesktop path's sanitiser would let a
       stray tag through as markup. */
    void plainBodyIsEscaped()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("body"), v(QStringLiteral("i <3 you & <b>this</b>"))}});

        const QString body = f.role(0, NotificationModel::BodyRole).toString();
        QVERIFY(body.contains(QLatin1String("&lt;3")));
        QVERIFY(body.contains(QLatin1String("&lt;b&gt;")));
    }

    void plainBodyKeepsLineBreaks()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("body"), v(QStringLiteral("one\ntwo"))}});

        QCOMPARE(f.role(0, NotificationModel::BodyRole).toString(),
                 QStringLiteral("one<br>two"));
    }

    /* markup-body carries the same subset the freedesktop body does, so it
       goes through the same sanitiser: <b> survives, <script> does not. */
    void markupBodyKeepsOnlyTheAllowedSubset()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("markup-body"),
                v(QStringLiteral("<b>bold</b><script>no</script>"))}});

        const QString body = f.role(0, NotificationModel::BodyRole).toString();
        QVERIFY(body.contains(QLatin1String("<b>bold</b>")));
        QVERIFY(!body.contains(QLatin1String("<script>")));
        QVERIFY(body.contains(QLatin1String("no")));
    }

    void markupBodyOutranksPlainBody()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("body"), v(QStringLiteral("plain"))},
               {QStringLiteral("markup-body"), v(QStringLiteral("<i>rich</i>"))}});

        QCOMPARE(f.role(0, NotificationModel::BodyRole).toString(), QStringLiteral("<i>rich</i>"));
    }

    /* Four priorities onto three urgencies. "high" deliberately lands on
       Normal: Critical means "stays until dismissed" here, and the spec
       reserves that weight for "urgent". */
    void priorityMapsOntoUrgency_data()
    {
        QTest::addColumn<QString>("priority");
        QTest::addColumn<int>("urgency");
        QTest::newRow("low") << QStringLiteral("low") << int(Urgency::Low);
        QTest::newRow("normal") << QStringLiteral("normal") << int(Urgency::Normal);
        QTest::newRow("high") << QStringLiteral("high") << int(Urgency::Normal);
        QTest::newRow("urgent") << QStringLiteral("urgent") << int(Urgency::Critical);
        QTest::newRow("absent") << QString() << int(Urgency::Normal);
        QTest::newRow("nonsense") << QStringLiteral("banana") << int(Urgency::Normal);
    }

    void priorityMapsOntoUrgency()
    {
        QFETCH(QString, priority);
        QFETCH(int, urgency);

        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("priority"), v(priority)}});

        QCOMPARE(f.role(0, NotificationModel::UrgencyRole).toInt(), urgency);
    }

    /* The whole point of the portal path: app_id is derived from the sandbox
       rather than claimed, and it lands on the key rules match against. */
    void appIdBecomesTheDesktopEntry()
    {
        Fixture f;
        f.add(QStringLiteral("com.spotify.Client"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))}});

        /* GroupKeyRole is the desktop entry when there is one — the same
           value rules match against. */
        QCOMPARE(f.role(0, NotificationModel::GroupKeyRole).toString(),
                 QStringLiteral("com.spotify.Client"));
    }

    /* "For historical reasons, it is also possible to send a simple string
       for themed icons with a single icon name." */
    void iconMayBeABareString()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("icon"), v(QStringLiteral("mail-message-new"))}});

        QVERIFY(f.role(0, NotificationModel::IconSourceRole).toString()
                    .contains(QLatin1String("mail-message-new")));
    }

    void buttonsBecomeActions()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("buttons"),
                v(QVariantList{button(QStringLiteral("archive"), QStringLiteral("Archive"))})}});

        QCOMPARE(f.actionKeys(0), QStringList({QStringLiteral("archive")}));
        QCOMPARE(f.actionLabels(0), QStringList({QStringLiteral("Archive")}));
    }

    /* "Buttons without a label are ignored by the server when it doesn't
       understand the purpose" — dropped, not rendered blank. */
    void buttonWithoutLabelOrPurposeIsDropped()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("buttons"),
                v(QVariantList{button(QStringLiteral("mystery"), QString()),
                               button(QStringLiteral("ok"), QStringLiteral("OK"))})}});

        QCOMPARE(f.actionKeys(0), QStringList({QStringLiteral("ok")}));
    }

    /* im.reply-with-text asks for a reply field, not a button. glassosd
       already renders one for the KDE extension; this is the standard
       spelling of the same thing. */
    void replyPurposeBecomesTheInlineReplyField()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("buttons"),
                v(QVariantList{button(QStringLiteral("send"), QStringLiteral("Send"),
                                      QStringLiteral("im.reply-with-text"))})}});

        QVERIFY(f.role(0, NotificationModel::InlineReplyRole).toBool());
        /* And it is not also drawn as a button. */
        QVERIFY(f.actionKeys(0).isEmpty());
    }

    /* The sender names its default action whatever it likes; everything else
       in glassosd calls activating the body "default". */
    void defaultActionIsExposedAsDefault()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("default-action"), v(QStringLiteral("open-thread"))}});

        /* Activating the body is offered... */
        QVERIFY(f.role(0, NotificationModel::HasDefaultActionRole).toBool());
        /* ...and the sender's own name for it is never drawn as a button. */
        QVERIFY(!f.actionKeys(0).contains(QStringLiteral("open-thread")));
        QVERIFY(f.actionKeys(0).isEmpty());
    }

    void trayHintKeepsItOutOfThePopupStack()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("display-hint"), v(QStringList{QStringLiteral("tray")})}});

        QCOMPARE(f.rows(), 0);
        QCOMPARE(f.history.rowCount(), 1);
    }

    /* The mirror of tray: shown, but explicitly not kept. */
    void transientHintKeepsItOutOfHistory()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("display-hint"), v(QStringList{QStringLiteral("transient")})}});

        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.history.rowCount(), 0);
    }

    /* "Don't show the notification on the lockscreen." It is still recorded,
       which is where it will be found after unlocking. */
    void hideOnLockscreenSuppressesThePopupWhileLocked()
    {
        Fixture f;
        f.model.setScreenLocked(true);
        f.add(QStringLiteral("org.example.Bank"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Payment received"))},
               {QStringLiteral("display-hint"), v(QStringList{QStringLiteral("hide-on-lockscreen")})}});

        QCOMPARE(f.rows(), 0);
        QCOMPARE(f.history.rowCount(), 1);
    }

    void hideOnLockscreenShowsNormallyWhenUnlocked()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.Bank"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Payment received"))},
               {QStringLiteral("display-hint"), v(QStringList{QStringLiteral("hide-on-lockscreen")})}});

        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.role(0, NotificationModel::SummaryRole).toString(),
                 QStringLiteral("Payment received"));
    }

    /* Locking with one already on screen has to remove it there and then.
       Withholding it only from that moment on would leave the text sitting
       in front of whoever walked up. */
    void lockingRemovesAnAlreadyVisibleHiddenNotification()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.Bank"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Payment received"))},
               {QStringLiteral("display-hint"), v(QStringList{QStringLiteral("hide-on-lockscreen")})}});
        QCOMPARE(f.rows(), 1);

        f.model.setScreenLocked(true);
        QCOMPARE(f.rows(), 0);
    }

    void lockingLeavesOrdinaryNotificationsAlone()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Still here"))}});

        f.model.setScreenLocked(true);
        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.role(0, NotificationModel::SummaryRole).toString(),
                 QStringLiteral("Still here"));
    }

    /* "All content of the notification will be hidden on the lockscreen." */
    void hideContentWithholdsTheTextButKeepsTheCard()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.Bank"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Balance low"))},
               {QStringLiteral("body"), v(QStringLiteral("You have 3 pounds left"))},
               {QStringLiteral("display-hint"),
                v(QStringList{QStringLiteral("hide-content-on-lockscreen")})}});

        f.model.setScreenLocked(true);
        QCOMPARE(f.rows(), 1);
        QVERIFY(f.role(0, NotificationModel::SummaryRole).toString() != QStringLiteral("Balance low"));
        QVERIFY(f.role(0, NotificationModel::BodyRole).toString().isEmpty());
    }

    /* Withheld, not destroyed: unlocking shows it without the sender having
       to send it again. */
    void unlockingRevealsWithheldContent()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.Bank"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Balance low"))},
               {QStringLiteral("body"), v(QStringLiteral("You have 3 pounds left"))},
               {QStringLiteral("display-hint"),
                v(QStringList{QStringLiteral("hide-content-on-lockscreen")})}});
        f.model.setScreenLocked(true);
        f.model.setScreenLocked(false);

        QCOMPARE(f.role(0, NotificationModel::SummaryRole).toString(), QStringLiteral("Balance low"));
        QCOMPARE(f.role(0, NotificationModel::BodyRole).toString(),
                 QStringLiteral("You have 3 pounds left"));
    }

    /* Hiding entirely is the stronger request, so it wins if a sender sets
       both — which the spec calls a programmer error rather than forbidding. */
    void bothLockscreenHintsResolveToHiding()
    {
        Fixture f;
        f.model.setScreenLocked(true);
        f.add(QStringLiteral("org.example.Bank"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("display-hint"),
                v(QStringList{QStringLiteral("hide-content-on-lockscreen"),
                              QStringLiteral("hide-on-lockscreen")})}});

        QCOMPARE(f.rows(), 0);
    }

    /* The configured default covers the overwhelming majority of senders,
       which say nothing at all. */
    void theDefaultAppliesToNotificationsThatSayNothing()
    {
        Fixture f;
        f.model.setLockPrivacyDefault(Notification::LockPrivacy::HideContent);
        f.model.setScreenLocked(true);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Secret"))}});

        QCOMPARE(f.rows(), 1);
        QVERIFY(f.role(0, NotificationModel::SummaryRole).toString() != QStringLiteral("Secret"));
    }

    /* ...and a sender that did say something is not overruled by it. */
    void aSendersOwnChoiceBeatsTheDefault()
    {
        Fixture f;
        f.model.setLockPrivacyDefault(Notification::LockPrivacy::Hide);
        f.model.setScreenLocked(true);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Train cancelled"))},
               {QStringLiteral("display-hint"), v(QStringList{QStringLiteral("show-as-new")})}});

        /* show-as-new says nothing about lock screens, so this sender is
           still "Unset" and the default hides it. */
        QCOMPARE(f.rows(), 0);

        Fixture g;
        g.model.setLockPrivacyDefault(Notification::LockPrivacy::Hide);
        g.model.setScreenLocked(true);
        /* Nothing in the portal says "show me anyway", so the one case that
           can override a hiding default is a notification the sender marked
           hide-content: it is shown, with its text withheld. */
        g.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("Train cancelled"))},
               {QStringLiteral("display-hint"),
                v(QStringList{QStringLiteral("hide-content-on-lockscreen")})}});
        QCOMPARE(g.rows(), 1);
    }

    void idsNeverCollideWithTheFreedesktopRange()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))}});

        const uint id = f.role(0, NotificationModel::IdRole).toUInt();
        QVERIFY(PortalServer::owns(id));
        QVERIFY(id >= PortalServer::idMask);
    }

    void reservingIdsSkipsPastPersistedOnes()
    {
        Fixture f;
        const uint used = PortalServer::idMask + 500;
        f.portal.reserveIds(used);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))}});

        QVERIFY(f.role(0, NotificationModel::IdRole).toUInt() > used);
    }

    /* A freedesktop id must never move the portal sequence, or the two would
       be handed the same numbers. */
    void reservingIgnoresFreedesktopIds()
    {
        Fixture f;
        f.portal.reserveIds(42);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))}});

        QVERIFY(PortalServer::owns(f.role(0, NotificationModel::IdRole).toUInt()));
    }

    /* "If the application reuses the same ID without withdrawing, the
       notification is updated with the new one." */
    void sameAppAndIdUpdatesInPlace()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("msg-1"),
              {{QStringLiteral("title"), v(QStringLiteral("First"))}});
        const uint first = f.role(0, NotificationModel::IdRole).toUInt();

        f.add(QStringLiteral("org.example.App"), QStringLiteral("msg-1"),
              {{QStringLiteral("title"), v(QStringLiteral("Second"))}});

        QCOMPARE(f.rows(), 1);
        QCOMPARE(f.role(0, NotificationModel::SummaryRole).toString(), QStringLiteral("Second"));
        QCOMPARE(f.role(0, NotificationModel::IdRole).toUInt(), first);
    }

    /* The id is only unique within an application. Two apps both calling
       their notification "1" are two notifications. */
    void sameIdFromDifferentAppsAreDistinct()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.One"), QStringLiteral("1"),
              {{QStringLiteral("title"), v(QStringLiteral("One"))}});
        f.add(QStringLiteral("org.example.Two"), QStringLiteral("1"),
              {{QStringLiteral("title"), v(QStringLiteral("Two"))}});

        QCOMPARE(f.rows(), 2);
    }

    void removeWithdrawsIt()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))}});
        QCOMPARE(f.rows(), 1);

        f.portal.removeNotification(QStringLiteral("org.example.App"), QStringLiteral("a"));
        QCOMPARE(f.rows(), 0);
    }

    void removingSomethingElsesNotificationDoesNothing()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))}});

        f.portal.removeNotification(QStringLiteral("org.example.Other"), QStringLiteral("a"));
        QCOMPARE(f.rows(), 1);
    }

    void emptyIdIsRefused()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QString(),
              {{QStringLiteral("title"), v(QStringLiteral("x"))}});
        QCOMPARE(f.rows(), 0);
    }

    /* A non-exported action goes back to the application as a signal, with
       the platform data the interface requires in the parameter list. */
    void plainActionIsSignalled()
    {
        Fixture f;
        QSignalSpy spy(&f.portal, &PortalServer::actionInvoked);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("buttons"),
                v(QVariantList{button(QStringLiteral("archive"), QStringLiteral("Archive"))})}});

        f.model.invokeAction(f.role(0, NotificationModel::IdRole).toUInt(),
                             QStringLiteral("archive"));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("org.example.App"));
        QCOMPARE(spy.first().at(1).toString(), QStringLiteral("a"));
        QCOMPARE(spy.first().at(2).toString(), QStringLiteral("archive"));
        /* target was not set, so platform-data is the only parameter. */
        QCOMPARE(spy.first().at(3).toList().size(), 1);
    }

    /* An "app."-prefixed action is activated over D-Bus instead, which is
       what lets a notification outlive the process that sent it. It must not
       also come back as a signal, or the application would see it twice. */
    void exportedActionIsNotSignalled()
    {
        Fixture f;
        QSignalSpy spy(&f.portal, &PortalServer::actionInvoked);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("buttons"),
                v(QVariantList{button(QStringLiteral("app.archive"), QStringLiteral("Archive"))})}});

        f.model.invokeAction(f.role(0, NotificationModel::IdRole).toUInt(),
                             QStringLiteral("app.archive"));

        QCOMPARE(spy.count(), 0);
    }

    /* Activating the body invokes the name the sender gave, not the internal
       "default" key it was exposed under. */
    void defaultActionIsSignalledUnderTheSendersName()
    {
        Fixture f;
        QSignalSpy spy(&f.portal, &PortalServer::actionInvoked);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("default-action"), v(QStringLiteral("open-thread"))}});

        f.model.invokeAction(f.role(0, NotificationModel::IdRole).toUInt(),
                             QStringLiteral("default"));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(2).toString(), QStringLiteral("open-thread"));
    }

    /* The typed reply is delivered as the response, on the action the
       purpose was attached to. */
    void replyIsDeliveredOnTheReplyAction()
    {
        Fixture f;
        QSignalSpy spy(&f.portal, &PortalServer::actionInvoked);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("buttons"),
                v(QVariantList{button(QStringLiteral("send"), QStringLiteral("Send"),
                                      QStringLiteral("im.reply-with-text"))})}});

        f.model.sendReply(f.role(0, NotificationModel::IdRole).toUInt(),
                      QStringLiteral("on my way"));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(2).toString(), QStringLiteral("send"));
        const QVariantList params = spy.first().at(3).toList();
        QCOMPARE(params.last().toString(), QStringLiteral("on my way"));
    }

    /* Nothing on the freedesktop side should reach the portal adaptor. */
    void freedesktopNotificationsAreNotTheportalsBusiness()
    {
        Fixture f;
        QSignalSpy spy(&f.portal, &PortalServer::actionInvoked);

        const uint id = f.server.handleNotify(QStringLiteral("App"), 0, QString(),
                                              QStringLiteral("Summary"), QString(),
                                              {QStringLiteral("go"), QStringLiteral("Go")},
                                              {}, -1);
        f.model.invokeAction(id, QStringLiteral("go"));

        QVERIFY(!PortalServer::owns(id));
        QCOMPARE(spy.count(), 0);
    }

    /* The regression this exists for: a descriptor carries a file position,
       and an application that writes its sound into a memfd and hands the
       descriptor straight over leaves it at EOF. Reading from there returns
       nothing and the sound is silently lost. Icons never caught it — the
       portal builds those descriptors itself and they arrive rewound. */
    void soundDescriptorIsReadFromTheStart()
    {
        /* wav/pcm, because the portal only forwards formats it recognises
           and the backend should not be the thing that guesses. */
        const QByteArray wav = QByteArrayLiteral("RIFF\x24\x08\x00\x00WAVEfmt ")
            + QByteArray(64, 'x');

        const int fd = memfd_create("glassosd-test-sound", MFD_ALLOW_SEALING);
        QVERIFY(fd >= 0);
        QCOMPARE(write(fd, wav.constData(), wav.size()), qint64(wav.size()));
        /* Deliberately not rewound — this is the state a sender leaves it in. */

        Fixture f;
        QSignalSpy spy(&f.model, &NotificationModel::soundWanted);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("sound"),
                v(QVariantList{QStringLiteral("file-descriptor"),
                               QVariant::fromValue(QDBusUnixFileDescriptor(fd))})}});
        close(fd);

        QCOMPARE(spy.count(), 1);
        const QString path = spy.first().at(0).toString();
        QVERIFY2(!path.isEmpty(), "no sound file was cached");
        /* Absolute, because SoundPlayer treats anything else as a theme
           name — which is how this failed before: an unreadable descriptor
           left the sound empty and the category quietly chose a stock one,
           so the notification still made a noise, just the wrong one. */
        QVERIFY2(path.startsWith(QLatin1Char('/')),
                 "the sender's own sound was silently replaced by a theme name");

        QFile cached(path);
        QVERIFY(cached.open(QIODevice::ReadOnly));
        QCOMPARE(cached.readAll(), wav);
    }

    /* Content-addressed, so an application that attaches the same sound to
       every message writes it once rather than per notification. */
    void identicalSoundsShareOneCachedFile()
    {
        const QByteArray wav = QByteArrayLiteral("RIFFwave-ish") + QByteArray(32, 'y');
        Fixture f;
        QSignalSpy spy(&f.model, &NotificationModel::soundWanted);

        for (int i = 0; i < 2; ++i) {
            const int fd = memfd_create("glassosd-test-sound", MFD_ALLOW_SEALING);
            QVERIFY(fd >= 0);
            QCOMPARE(write(fd, wav.constData(), wav.size()), qint64(wav.size()));
            f.add(QStringLiteral("org.example.App"), QStringLiteral("n%1").arg(i),
                  {{QStringLiteral("title"), v(QStringLiteral("x"))},
                   {QStringLiteral("sound"),
                    v(QVariantList{QStringLiteral("file-descriptor"),
                                   QVariant::fromValue(QDBusUnixFileDescriptor(fd))})}});
            close(fd);
        }

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), spy.at(1).at(0).toString());
    }

    /* A themed icon array is a preference list, so the first name is used
       when nothing better is known about the rest. */
    void themedIconArrayIsAccepted()
    {
        Fixture f;
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("icon"),
                v(QVariantList{QStringLiteral("themed"),
                               QVariant(QStringList{QStringLiteral("mail-message-new"),
                                                    QStringLiteral("mail-unread")})})}});

        QVERIFY(!f.role(0, NotificationModel::IconSourceRole).toString().isEmpty());
    }

    void silentSoundPlaysNothing()
    {
        Fixture f;
        QSignalSpy spy(&f.model, &NotificationModel::soundWanted);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("category"), v(QStringLiteral("im.received"))},
               {QStringLiteral("sound"), v(QStringLiteral("silent"))}});

        /* Silent even though the category would otherwise have chosen one. */
        QCOMPARE(spy.count(), 0);
    }

    /* "default" means the server decides, which is what a notification
       naming no sound at all already means — not a sound called "default". */
    void defaultSoundLeavesTheChoiceToUs()
    {
        Fixture f;
        QSignalSpy spy(&f.model, &NotificationModel::soundWanted);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("category"), v(QStringLiteral("im.received"))},
               {QStringLiteral("sound"), v(QStringLiteral("default"))}});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("message-new-instant"));
    }

    /* The category has to survive the translation, because everything
       downstream — rules, and the sound chosen here — reads it. */
    void categoryIsCarriedThrough()
    {
        Fixture f;
        QSignalSpy spy(&f.model, &NotificationModel::soundWanted);
        f.add(QStringLiteral("org.example.App"), QStringLiteral("a"),
              {{QStringLiteral("title"), v(QStringLiteral("x"))},
               {QStringLiteral("category"), v(QStringLiteral("im.received"))}});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("message-new-instant"));
    }

    /* Declaring version 2 is what stops the frontend stripping sound,
       category, markup and button purposes before we ever see them. */
    void advertisesTheOptionsItActuallyHonours()
    {
        const QVariantMap options = PortalServer::supportedOptions();
        QCOMPARE(options.value(QStringLiteral("category")).toStringList(),
                 QStringList({QStringLiteral("im.received")}));
        QCOMPARE(options.value(QStringLiteral("button-purpose")).toStringList(),
                 QStringList({QStringLiteral("im.reply-with-text")}));
    }
};

GLASSOSD_TEST_MAIN(TestPortalServer)
#include "tst_portalserver.moc"
