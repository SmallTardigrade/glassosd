/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    The popup queue.

    This is where the notifications a person actually sees are decided, and
    where the worst bugs have been — the ones that look like correct behaviour
    until you count. A tagged notification whose repeat reset the dwell clock
    produced a card that could never expire. A queue that compared its hidden
    count against a stale snapshot left "+2 more" on screen while four were
    waiting. Neither crashed; both were only findable by watching.

    A few tests here wait on real time, because expiry is real time and the
    tick is 250ms. They are the slow ones and they are worth it: the immortal
    card is the single worst bug this project has had.
*/
#include "notificationmodel.h"
#include "testenv.h"

#include <QSignalSpy>

namespace
{
Notification note(const QString &app, const QString &summary = {},
                  const QString &body = {}, uint id = 0)
{
    Notification n;
    n.id = id;
    n.appName = app;
    n.summary = summary;
    n.body = body;
    return n;
}
} // namespace

class TestNotificationModel : public QObject
{
    Q_OBJECT

private:
    /* Every test wants a model that is not trying to be clever: no burst
       grouping unless it asks for it, and no expiry unless it asks for it. */
    static void plain(NotificationModel &m)
    {
        m.setCoalesce(0, 0);
        m.setTimeouts(0, 0, 0);
    }

    static QString summaryAt(const NotificationModel &m, int row)
    {
        return m.index(row).data(NotificationModel::SummaryRole).toString();
    }

private Q_SLOTS:
    void insertedNotificationIsShown()
    {
        NotificationModel m;
        plain(m);
        m.insert(note(QStringLiteral("Signal"), QStringLiteral("hello"), {}, 1));

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(summaryAt(m, 0), QStringLiteral("hello"));
        QCOMPARE(m.hiddenCount(), 0);
        QVERIFY(m.isLive(1));
    }

    /* dunst's notification_limit, plus its rule that the indicator only takes
       a slot when there is genuinely an overflow to indicate. */
    void limitQueuesTheRestBehindAnIndicator()
    {
        NotificationModel m;
        plain(m);
        m.setLimit(4);
        m.setIndicateHidden(true);

        for (uint i = 1; i <= 4; ++i) {
            m.insert(note(QStringLiteral("App"), QStringLiteral("n%1").arg(i), {}, i));
        }
        QCOMPARE(m.rowCount(), 4);       // no overflow yet, so no reserved slot
        QCOMPARE(m.hiddenCount(), 0);

        m.insert(note(QStringLiteral("App"), QStringLiteral("n5"), {}, 5));
        QCOMPARE(m.rowCount(), 3);       // one slot given back to "+N more"
        QCOMPARE(m.hiddenCount(), 2);
    }

    void indicatorSlotCanBeTurnedOff()
    {
        NotificationModel m;
        plain(m);
        m.setLimit(4);
        m.setIndicateHidden(false);

        for (uint i = 1; i <= 5; ++i) {
            m.insert(note(QStringLiteral("App"), QStringLiteral("n%1").arg(i), {}, i));
        }
        QCOMPARE(m.rowCount(), 4);
        QCOMPARE(m.hiddenCount(), 1);
    }

    void limitZeroMeansUnlimited()
    {
        NotificationModel m;
        plain(m);
        m.setLimit(0);
        for (uint i = 1; i <= 10; ++i) {
            m.insert(note(QStringLiteral("App"), QStringLiteral("n%1").arg(i), {}, i));
        }
        QCOMPARE(m.rowCount(), 10);
        QCOMPARE(m.hiddenCount(), 0);
    }

    /* A negative limit is an easy typo in a config file. Unclamped it would
       make the effective limit negative, so nothing would ever be promoted:
       every notification queued forever, no popup, and no error anywhere. */
    void negativeLimitDoesNotSwallowEverything()
    {
        NotificationModel m;
        plain(m);
        m.setLimit(-5);
        for (uint i = 1; i <= 3; ++i) {
            m.insert(note(QStringLiteral("App"), QStringLiteral("n%1").arg(i), {}, i));
        }
        QCOMPARE(m.rowCount(), 3);
    }

    /* The "+N more" label went stale because update() compared the queue
       against its size on entry — and insert() appends before calling it, so
       the snapshot already contained the new arrival. */
    void hiddenCountIsAnnouncedEveryTimeTheQueueGrows()
    {
        NotificationModel m;
        plain(m);
        m.setLimit(2);
        QSignalSpy spy(&m, &NotificationModel::hiddenCountChanged);

        for (uint i = 1; i <= 3; ++i) {
            m.insert(note(QStringLiteral("App"), QStringLiteral("n%1").arg(i), {}, i));
        }
        QCOMPARE(m.hiddenCount(), 2);
        const int before = spy.count();

        m.insert(note(QStringLiteral("App"), QStringLiteral("n4"), {}, 4));
        QCOMPARE(m.hiddenCount(), 3);
        QVERIFY(spy.count() > before);   // nothing moved between the lists
    }

    /* replaces_id is an update of an existing notification, not the closing
       of one, so no NotificationClosed goes out — dunst does the same. */
    void replaceByIdUpdatesInPlaceAndAnnouncesNothing()
    {
        NotificationModel m;
        plain(m);
        QSignalSpy closed(&m, &NotificationModel::notificationClosed);

        m.insert(note(QStringLiteral("Dolphin"), QStringLiteral("Copying"),
                      QStringLiteral("20%"), 7));
        m.insert(note(QStringLiteral("Dolphin"), QStringLiteral("Copying"),
                      QStringLiteral("80%"), 7));

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.index(0).data(NotificationModel::BodyRole).toString(),
                 QStringLiteral("80%"));
        QCOMPARE(closed.count(), 0);
    }

    /* An update that arrives without an icon keeps the one it had, rather
       than going blank halfway through a progress notification. */
    void replaceByIdInheritsTheIcon()
    {
        NotificationModel m;
        plain(m);
        Notification first = note(QStringLiteral("Dolphin"), QStringLiteral("Copying"),
                                  {}, 7);
        first.appIcon = QStringLiteral("folder");
        m.insert(first);
        m.insert(note(QStringLiteral("Dolphin"), QStringLiteral("Copying"),
                      QStringLiteral("80%"), 7));

        QCOMPARE(m.index(0).data(NotificationModel::IconSourceRole).toString(),
                 QStringLiteral("image://icon/folder"));
    }

    /* A stack tag replaces a *different* notification, so that one does have
       to be announced — Expired, because the user never acted on it. */
    void replaceByTagClosesTheOneItReplaced()
    {
        NotificationModel m;
        plain(m);
        QSignalSpy closed(&m, &NotificationModel::notificationClosed);

        Notification a = note(QStringLiteral("PowerDevil"), QStringLiteral("12%"), {}, 1);
        a.stackTag = QStringLiteral("battery");
        Notification b = note(QStringLiteral("PowerDevil"), QStringLiteral("9%"), {}, 2);
        b.stackTag = QStringLiteral("battery");

        m.insert(a);
        m.insert(b);

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(summaryAt(m, 0), QStringLiteral("9%"));
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(0).toUInt(), 1u);
        QCOMPARE(closed.at(0).at(1).toUInt(), CloseReason::Expired);
    }

    /* Matched on tag *and* app, or two apps picking the same generic tag
       would clobber each other. */
    void replaceByTagRequiresTheSameApp()
    {
        NotificationModel m;
        plain(m);
        Notification a = note(QStringLiteral("AppOne"), QStringLiteral("one"), {}, 1);
        a.stackTag = QStringLiteral("status");
        Notification b = note(QStringLiteral("AppTwo"), QStringLiteral("two"), {}, 2);
        b.stackTag = QStringLiteral("status");

        m.insert(a);
        m.insert(b);
        QCOMPARE(m.rowCount(), 2);
    }

    /* The immortal card.

       PowerDevil re-sends the identical "Device Battery Low (6% Remaining)"
       once or twice a second for as long as the device is low. The tag
       correctly collapsed all of them onto one card — and every copy also
       reset the dwell clock, so an 8s timer restarted about eight times per
       expiry and the card could never die. The rule was working; the card was
       simply immortal, which looks exactly like the spam it was meant to stop. */
    void identicalTagRepeatDoesNotRestartTheDwell()
    {
        NotificationModel m;
        m.setCoalesce(0, 0);
        m.setTimeouts(2000, 2000, 2000);

        Notification n = note(QStringLiteral("PowerDevil"),
                              QStringLiteral("Device Battery Low"),
                              QStringLiteral("6% Remaining"), 1);
        n.stackTag = QStringLiteral("battery");
        m.insert(n);
        QCOMPARE(m.rowCount(), 1);

        QTest::qWait(1200);
        n.id = 2;
        m.insert(n);                 // byte-identical, 1200ms in
        QCOMPARE(m.rowCount(), 1);

        /* It must die on the original clock: ~800ms from here plus a tick.
           With the clock reset it would live another full two seconds. The
           gap between those is wide on purpose — this runs on build machines
           under load, and a test that fails when the box is busy teaches
           people to ignore it. */
        QTRY_VERIFY_WITH_TIMEOUT(m.rowCount() == 0, 1500);
    }

    /* The other half of the same rule: 12% -> 9% is new information and does
       earn a fresh timer, which is the entire point of replacing by tag. */
    void changedTagContentDoesRestartTheDwell()
    {
        NotificationModel m;
        m.setCoalesce(0, 0);
        m.setTimeouts(2000, 2000, 2000);

        Notification n = note(QStringLiteral("PowerDevil"),
                              QStringLiteral("Device Battery Low"),
                              QStringLiteral("9% Remaining"), 1);
        n.stackTag = QStringLiteral("battery");
        m.insert(n);

        QTest::qWait(1200);
        n.id = 2;
        n.body = QStringLiteral("6% Remaining");
        m.insert(n);

        QTest::qWait(1000);          // 2200ms after the first, 1000 after the second
        QCOMPARE(m.rowCount(), 1);
    }

    /* set_stack_tag alone is not enough for a sender that repeats forever:
       replaceByTag only searches what is on screen or queued, so once the card
       expires the next copy matches nothing and opens a fresh popup. */
    void repeatWindowSuppressesAPopupAfterTheCardIsGone()
    {
        NotificationModel m;
        plain(m);
        QSignalSpy closed(&m, &NotificationModel::notificationClosed);

        Notification n = note(QStringLiteral("PowerDevil"),
                              QStringLiteral("Device Battery Low"),
                              QStringLiteral("6% Remaining"), 1);
        n.repeatWindowSec = 300;
        m.insert(n);
        QCOMPARE(m.rowCount(), 1);

        m.closeId(1, CloseReason::Expired);
        QCOMPARE(m.rowCount(), 0);

        n.id = 2;
        m.insert(n);
        QCOMPARE(m.rowCount(), 0);   // suppressed, not shown again

        /* The sender is still told, so it is not left waiting on a close
           signal that never comes. */
        QCOMPARE(closed.count(), 2);
        QCOMPARE(closed.at(1).at(0).toUInt(), 2u);
        QCOMPARE(closed.at(1).at(1).toUInt(), CloseReason::Expired);
    }

    /* Keyed on the content, so the same rule still lets a changed percentage
       through as a genuinely new piece of news. */
    void repeatWindowLetsChangedContentThrough()
    {
        NotificationModel m;
        plain(m);

        Notification n = note(QStringLiteral("PowerDevil"),
                              QStringLiteral("Device Battery Low"),
                              QStringLiteral("9% Remaining"), 1);
        n.repeatWindowSec = 300;
        m.insert(n);
        m.closeId(1, CloseReason::Expired);

        n.id = 2;
        n.body = QStringLiteral("6% Remaining");
        m.insert(n);
        QCOMPARE(m.rowCount(), 1);
    }

    void repeatWindowIsOffByDefault()
    {
        NotificationModel m;
        plain(m);
        Notification n = note(QStringLiteral("App"), QStringLiteral("same"), {}, 1);
        m.insert(n);
        m.closeId(1, CloseReason::Expired);
        n.id = 2;
        m.insert(n);
        QCOMPARE(m.rowCount(), 1);
    }

    /* Burst grouping — the capability neither dunst nor swaync has. Thirty
       notifications should become one card that says thirty. */
    void burstsCoalesceIntoOneCountedCard()
    {
        NotificationModel m;
        m.setTimeouts(0, 0, 0);
        m.setCoalesce(3, 20000);

        for (uint i = 1; i <= 3; ++i) {
            m.insert(note(QStringLiteral("Discord"), QStringLiteral("msg %1").arg(i), {}, i));
        }
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.index(0).data(NotificationModel::GroupCountRole).toInt(), 3);

        /* A continuing burst keeps absorbing rather than starting a second
           card behind the first. */
        m.insert(note(QStringLiteral("Discord"), QStringLiteral("msg 4"), {}, 4));
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.index(0).data(NotificationModel::GroupCountRole).toInt(), 4);
    }

    /* Below the threshold nothing is grouped: two messages are two messages. */
    void belowTheThresholdNothingIsGrouped()
    {
        NotificationModel m;
        m.setTimeouts(0, 0, 0);
        m.setCoalesce(3, 20000);
        m.insert(note(QStringLiteral("Discord"), QStringLiteral("one"), {}, 1));
        m.insert(note(QStringLiteral("Discord"), QStringLiteral("two"), {}, 2));
        QCOMPARE(m.rowCount(), 2);
    }

    void coalescingIsPerApp()
    {
        NotificationModel m;
        m.setTimeouts(0, 0, 0);
        m.setCoalesce(2, 20000);
        m.insert(note(QStringLiteral("Discord"), QStringLiteral("a"), {}, 1));
        m.insert(note(QStringLiteral("Signal"), QStringLiteral("b"), {}, 2));
        QCOMPARE(m.rowCount(), 2);   // one each, neither reached the threshold
    }

    /* Grouped by desktop entry where there is one: the same application can
       send under more than one display name. */
    void coalescingPrefersTheDesktopEntry()
    {
        NotificationModel m;
        m.setTimeouts(0, 0, 0);
        m.setCoalesce(2, 20000);

        Notification a = note(QStringLiteral("Firefox"), QStringLiteral("a"), {}, 1);
        a.desktopEntry = QStringLiteral("firefox");
        Notification b = note(QStringLiteral("Mozilla Firefox"), QStringLiteral("b"), {}, 2);
        b.desktopEntry = QStringLiteral("firefox");

        m.insert(a);
        m.insert(b);
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.index(0).data(NotificationModel::GroupCountRole).toInt(), 2);
    }

    /* A tagged notification has asked to be replaced by its successor, not
       counted alongside it, so tags outrank coalescing. */
    void stackTagsAreNotCoalesced()
    {
        NotificationModel m;
        m.setTimeouts(0, 0, 0);
        m.setCoalesce(2, 20000);

        Notification tagged = note(QStringLiteral("App"), QStringLiteral("a"), {}, 1);
        tagged.stackTag = QStringLiteral("t");
        m.insert(tagged);
        m.insert(note(QStringLiteral("App"), QStringLiteral("b"), {}, 2));

        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.index(0).data(NotificationModel::GroupCountRole).toInt(), 1);
    }

    void coalescingCanBeDisabled()
    {
        NotificationModel m;
        m.setTimeouts(0, 0, 0);
        m.setCoalesce(0, 20000);
        m.setLimit(0);   // the display limit is a separate question
        for (uint i = 1; i <= 5; ++i) {
            m.insert(note(QStringLiteral("Discord"), QStringLiteral("n%1").arg(i), {}, i));
        }
        QCOMPARE(m.rowCount(), 5);
    }

    /* A rule said "history only". The sender is still told it ended. */
    void skipDisplayShowsNothingButStillAnswersTheSender()
    {
        NotificationModel m;
        plain(m);
        QSignalSpy closed(&m, &NotificationModel::notificationClosed);

        Notification n = note(QStringLiteral("Steam"), QStringLiteral("friend online"), {}, 1);
        n.skipDisplay = true;
        m.insert(n);

        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(1).toUInt(), CloseReason::Expired);
    }

    void doNotDisturbSuppressesThePopup()
    {
        NotificationModel m;
        plain(m);
        m.setDoNotDisturb(true);
        m.insert(note(QStringLiteral("App"), QStringLiteral("hello"), {}, 1));
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.hiddenCount(), 0);   // suppressed, not queued for later
    }

    /* Three separate reasons for quiet, kept apart on purpose: turning Do Not
       Disturb off must not silently cancel a screen recording's inhibition. */
    void theThreeQuietSourcesAreIndependent()
    {
        NotificationModel m;
        plain(m);
        QVERIFY(!m.quiet());

        m.setDoNotDisturb(true);
        m.setInhibited(true);
        m.setBusyQuiet(true);
        QVERIFY(m.quiet());

        m.setDoNotDisturb(false);
        QVERIFY(m.quiet());          // the application is still asking
        m.setInhibited(false);
        QVERIFY(m.quiet());          // something is still holding the screen awake
        m.setBusyQuiet(false);
        QVERIFY(!m.quiet());
    }

    /* Sound goes with the popup: a notification nobody is shown should not
       announce itself. */
    void quietSilencesTheSoundToo()
    {
        NotificationModel m;
        plain(m);
        QSignalSpy sound(&m, &NotificationModel::soundWanted);

        m.insert(note(QStringLiteral("App"), QStringLiteral("one"), {}, 1));
        QCOMPARE(sound.count(), 1);
        QCOMPARE(sound.at(0).at(0).toString(), QStringLiteral("message"));

        m.setDoNotDisturb(true);
        m.insert(note(QStringLiteral("App"), QStringLiteral("two"), {}, 2));
        QCOMPARE(sound.count(), 1);
    }

    void ruleSoundOverridesTheDefaultAndNoneMeansSilence()
    {
        NotificationModel m;
        plain(m);
        QSignalSpy sound(&m, &NotificationModel::soundWanted);

        Notification chosen = note(QStringLiteral("App"), QStringLiteral("one"), {}, 1);
        chosen.sound = QStringLiteral("bell");
        m.insert(chosen);
        QCOMPARE(sound.count(), 1);
        QCOMPARE(sound.at(0).at(0).toString(), QStringLiteral("bell"));

        Notification silent = note(QStringLiteral("App"), QStringLiteral("two"), {}, 2);
        silent.sound = QStringLiteral("none");
        m.insert(silent);
        QCOMPARE(sound.count(), 1);

        /* Low urgency has no sound at all, so nothing is emitted rather than
           an empty name being handed to the player. */
        Notification low = note(QStringLiteral("App"), QStringLiteral("three"), {}, 3);
        low.urgency = Urgency::Low;
        m.insert(low);
        QCOMPARE(sound.count(), 1);
    }

    /* A held-back notification has still arrived, and the sound is what tells
       you so — hence before the queue, not after. */
    void queuedNotificationsStillMakeTheirSound()
    {
        NotificationModel m;
        plain(m);
        m.setLimit(1);
        QSignalSpy sound(&m, &NotificationModel::soundWanted);

        m.insert(note(QStringLiteral("App"), QStringLiteral("one"), {}, 1));
        m.insert(note(QStringLiteral("App"), QStringLiteral("two"), {}, 2));
        QVERIFY(m.hiddenCount() > 0);
        QCOMPARE(sound.count(), 2);
    }

    /* 0 means never expire, which the spec mandates for critical. */
    void criticalDoesNotExpireOnItsOwn()
    {
        NotificationModel m;
        m.setCoalesce(0, 0);
        m.setTimeouts(200, 200, 0);

        Notification n = note(QStringLiteral("App"), QStringLiteral("disk full"), {}, 1);
        n.urgency = Urgency::Critical;
        m.insert(n);

        Notification ordinary = note(QStringLiteral("App"), QStringLiteral("hello"), {}, 2);
        m.insert(ordinary);

        QTRY_COMPARE_WITH_TIMEOUT(m.rowCount(), 1, 1500);
        QCOMPARE(summaryAt(m, 0), QStringLiteral("disk full"));
    }

    /* An app that sent its own expire_timeout still wins, including over the
       critical default — the fallbacks only apply when it sent -1. */
    void senderTimeoutBeatsTheDefault()
    {
        NotificationModel m;
        m.setCoalesce(0, 0);
        m.setTimeouts(0, 0, 0);

        Notification n = note(QStringLiteral("App"), QStringLiteral("brief"), {}, 1);
        n.urgency = Urgency::Critical;
        n.timeoutMs = 200;
        m.insert(n);

        QTRY_COMPARE_WITH_TIMEOUT(m.rowCount(), 0, 1500);
    }

    /* Hovering pauses the dwell timer: a notification should not expire out
       from under someone who is visibly reading it. */
    void hoveringHoldsANotificationOpen()
    {
        NotificationModel m;
        m.setCoalesce(0, 0);
        m.setTimeouts(300, 300, 300);

        m.insert(note(QStringLiteral("App"), QStringLiteral("read me"), {}, 1));
        m.setHovered(1, true);

        QTest::qWait(700);
        QCOMPARE(m.rowCount(), 1);

        m.setHovered(1, false);
        QTRY_COMPARE_WITH_TIMEOUT(m.rowCount(), 0, 1500);
    }

    /* Turning the setting off must not make every hovered popup vanish at
       once — they resume counting from now, not from when they appeared. */
    void disablingHoverPauseRestartsRatherThanExpires()
    {
        NotificationModel m;
        m.setCoalesce(0, 0);
        m.setTimeouts(400, 400, 400);

        m.insert(note(QStringLiteral("App"), QStringLiteral("read me"), {}, 1));
        m.setHovered(1, true);
        QTest::qWait(700);

        m.setHoverPause(false);
        QCOMPARE(m.rowCount(), 1);   // not swept away the instant the flag flips
    }

    void dismissAllClearsScreenAndQueue()
    {
        NotificationModel m;
        plain(m);
        m.setLimit(2);
        QSignalSpy closed(&m, &NotificationModel::notificationClosed);

        for (uint i = 1; i <= 5; ++i) {
            m.insert(note(QStringLiteral("App"), QStringLiteral("n%1").arg(i), {}, i));
        }
        QCOMPARE(m.dismissAll(), 5);
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.hiddenCount(), 0);
        QCOMPARE(closed.count(), 5);
        QCOMPARE(closed.at(0).at(1).toUInt(), CloseReason::Dismissed);
    }

    /* Opening the centre is not acting on any particular notification, and
       telling every sender otherwise would be a lie about intent. */
    void hideForCentreClosesAsExpiredNotDismissed()
    {
        NotificationModel m;
        plain(m);
        QSignalSpy closed(&m, &NotificationModel::notificationClosed);

        m.insert(note(QStringLiteral("App"), QStringLiteral("one"), {}, 1));
        QCOMPARE(m.hideForCentre(), 1);
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(closed.at(0).at(1).toUInt(), CloseReason::Expired);
    }

    void invokingAnActionAnnouncesItThenCloses()
    {
        NotificationModel m;
        plain(m);
        QSignalSpy invoked(&m, &NotificationModel::actionInvoked);
        QSignalSpy closed(&m, &NotificationModel::notificationClosed);

        Notification n = note(QStringLiteral("PowerDevil"), QStringLiteral("Low battery"), {}, 1);
        n.actions = {QStringLiteral("save"), QStringLiteral("Switch to Power Save")};
        m.insert(n);
        m.invokeAction(1, QStringLiteral("save"));

        QCOMPARE(invoked.count(), 1);
        QCOMPARE(invoked.at(0).at(1).toString(), QStringLiteral("save"));
        QCOMPARE(closed.at(0).at(1).toUInt(), CloseReason::Dismissed);
        QVERIFY(!m.isLive(1));
    }

    /* The wire format is a flat [key, label, ...] list. "default" is what
       activating the body means and the spec says it must not be drawn as a
       button; inline-reply is rendered as a text field, not one either. */
    void actionsRoleHidesDefaultAndInlineReply()
    {
        NotificationModel m;
        plain(m);
        Notification n = note(QStringLiteral("Element"), QStringLiteral("message"), {}, 1);
        n.actions = {QStringLiteral("default"),      QStringLiteral("Open"),
                     QStringLiteral("inline-reply"), QStringLiteral("Reply"),
                     QStringLiteral("mark-read"),    QStringLiteral("Mark as read")};
        m.insert(n);

        const QVariantList actions =
            m.index(0).data(NotificationModel::ActionsRole).toList();
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.at(0).toMap().value(QStringLiteral("key")).toString(),
                 QStringLiteral("mark-read"));
        QVERIFY(m.index(0).data(NotificationModel::HasDefaultActionRole).toBool());
    }

    /* An odd-length actions list is malformed input from a sender, and must
       not be read past the end of. */
    void malformedActionListIsIgnoredNotRead()
    {
        NotificationModel m;
        plain(m);
        Notification n = note(QStringLiteral("App"), QStringLiteral("x"), {}, 1);
        n.actions = {QStringLiteral("only-a-key")};
        m.insert(n);
        QCOMPARE(m.index(0).data(NotificationModel::ActionsRole).toList().size(), 0);
    }

    /* A closing row is logically gone — it is still here only so the view can
       animate it away. Everything that looks a notification up has to treat it
       as absent, without each caller having to know the state exists. */
    void aClosingRowIsInvisibleToLookups()
    {
        NotificationModel m;
        plain(m);
        m.setExitMs(300);
        m.insert(note(QStringLiteral("App"), QStringLiteral("bye"), {}, 1));

        m.closeId(1, CloseReason::Dismissed);
        QCOMPARE(m.rowCount(), 1);                  // still drawn, mid-animation
        QVERIFY(m.index(0).data(NotificationModel::ClosingRole).toBool());
        QVERIFY(!m.isLive(1));

        QTRY_COMPARE_WITH_TIMEOUT(m.rowCount(), 0, 1500);
    }

    void snoozeHandsTheNotificationOnAndTakesItOffScreen()
    {
        NotificationModel m;
        plain(m);
        Notification received;
        int count = 0;
        connect(&m, &NotificationModel::snoozeRequested, this,
                [&](const Notification &n) { received = n; ++count; });

        m.insert(note(QStringLiteral("Signal"), QStringLiteral("later"), {}, 1));
        m.snooze(1);

        QCOMPARE(count, 1);
        QCOMPARE(received.summary, QStringLiteral("later"));
        QCOMPARE(m.rowCount(), 0);
    }

    void closingAnUnknownIdIsHarmless()
    {
        NotificationModel m;
        plain(m);
        QVERIFY(!m.closeId(999, CloseReason::Closed));
        QCOMPARE(m.dismissAll(), 0);
        QCOMPARE(m.hideForCentre(), 0);
    }
};

GLASSOSD_TEST_MAIN(TestNotificationModel)
#include "tst_notificationmodel.moc"
