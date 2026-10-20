/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Which sound a notification asks for.

    Small, but it is a table, and a table is exactly the kind of thing that
    acquires a wrong row during an unrelated edit. The contract that matters:
    category outranks urgency, an unknown category falls through rather than
    inventing a name no sound theme ships, and low urgency is silent.
*/
#include "soundplayer.h"
#include "testenv.h"

class TestSoundPlayer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nameFor_data()
    {
        QTest::addColumn<QString>("category");
        QTest::addColumn<int>("urgency");
        QTest::addColumn<QString>("expected");

        /* Category first, because it says what the thing *is*. Each of these
           names is defined by the XDG sound naming spec; a name outside it
           would resolve to silence in every theme. */
        QTest::newRow("im") << "im" << 1 << "message-new-instant";
        QTest::newRow("im.received") << "im.received" << 1 << "message-new-instant";
        QTest::newRow("email") << "email" << 1 << "message";
        QTest::newRow("email.arrived") << "email.arrived" << 1 << "message";
        QTest::newRow("device.added") << "device.added" << 1 << "device-added";
        QTest::newRow("device.removed") << "device.removed" << 1 << "device-removed";
        QTest::newRow("net.up") << "network.connected" << 1
                                << "network-connectivity-established";
        QTest::newRow("net.down") << "network.disconnected" << 1
                                  << "network-connectivity-lost";
        QTest::newRow("transfer.ok") << "transfer.complete" << 1 << "complete";
        QTest::newRow("transfer.err") << "transfer.error" << 1 << "dialog-error";

        /* Categories are matched case-insensitively; senders are inconsistent. */
        QTest::newRow("uppercase") << "IM.Received" << 1 << "message-new-instant";

        /* A category outranks urgency even when urgency would have been
           louder — the sound should say what happened, not how loudly. */
        QTest::newRow("category beats urgency")
            << "transfer.complete" << 2 << "complete";

        /* No category, or one with no standard sound: fall through. */
        QTest::newRow("critical") << "" << 2 << "dialog-warning";
        QTest::newRow("normal") << "" << 1 << "message";
        QTest::newRow("unknown category, normal") << "x-vendor.thing" << 1 << "message";

        /* Low urgency arrived, but is not worth a noise. */
        QTest::newRow("low") << "" << 0 << "";
        QTest::newRow("unknown category, low") << "x-vendor.thing" << 0 << "";
    }

    void nameFor()
    {
        QFETCH(QString, category);
        QFETCH(int, urgency);
        QFETCH(QString, expected);
        QCOMPARE(SoundPlayer::nameFor(category, urgency), expected);
    }

    // ---- BurstGate: the volume sound ------------------------------------
    /* One sound per adjustment, once it is over, at the level you ended on.
       It used to sound at the start of a run as well, which made every
       three-finger volume swipe two sounds; a few swipes in a row were a
       chatter of them, about ten in four seconds, measured. */

    /* A single press: nothing at the press, one sound once it is quiet. */
    void singleTapSoundsOnceAfterTheWindow()
    {
        BurstGate g(500);
        g.event(1000);
        QVERIFY(!g.finish(1200));                // not quiet long enough yet
        QVERIFY(g.finish(1500));
        QVERIFY(!g.finish(2000));                // owed exactly once
    }

    /* A held key repeats every 40ms for a second: one sound, after release.
       Sounding on every step was 26 overlapping copies of a 300ms sound. */
    void heldKeySoundsOnceAfterRelease()
    {
        BurstGate g(500);
        qint64 t = 1000;
        for (int i = 0; i < 26; ++i, t += 40) {
            g.event(t);
            QVERIFY(!g.finish(t));               // never while the key is held
        }
        const qint64 lastStep = t - 40;
        QVERIFY(!g.finish(lastStep + 300));
        QVERIFY(g.finish(lastStep + 500));
        QVERIFY(!g.finish(lastStep + 1200));
    }

    /* A swipe sends volume steps as the fingers move, and a slow stretch of
       one can leave 300ms between steps. That is still one adjustment. */
    void slowSwipeIsOneSound()
    {
        BurstGate g(500);
        int sounded = 0;
        for (qint64 t : {1000, 1100, 1400, 1700, 1800}) {
            if (g.finish(t)) {
                ++sounded;
            }
            g.event(t);
        }
        QVERIFY(g.finish(2300));
        QCOMPARE(sounded, 0);
    }

    /* The default window, against the same slow swipe: shortening it to make
       the sound arrive sooner must not split a swipe into several. */
    void defaultWindowKeepsASlowSwipeWhole()
    {
        BurstGate g;
        QVERIFY(g.quietMs() > 300);
        int sounded = 0;
        for (qint64 t : {1000, 1100, 1400, 1700, 1800}) {
            if (g.finish(t)) {
                ++sounded;
            }
            g.event(t);
        }
        QCOMPARE(sounded, 0);
        QVERIFY(g.finish(1800 + g.quietMs()));
        QVERIFY(g.quietMs() <= 400);             // and not late
    }

    /* The live failure an earlier version of this suite missed, because the
       other tests pass exact times: a timer firing a little inside the
       window. finish() must say "not yet", and remainingMs() must say how
       long to wait, or the sound is silently lost. */
    void earlyTimerIsToldHowLongToWait()
    {
        BurstGate g(500);
        g.event(1000);
        g.event(1040);
        QVERIFY(!g.finish(1515));                // coarse timer, 25ms early
        QCOMPARE(g.remainingMs(1515), qint64(25));
        QCOMPARE(g.remainingMs(1540), qint64(0));
        QVERIFY(g.finish(1540));                 // still owed, not lost
    }

    void remainingIsZeroWithNoRun()
    {
        BurstGate g(500);
        QCOMPARE(g.remainingMs(5000), qint64(0));
        g.event(5000);
        QVERIFY(g.finish(5600));
        QCOMPARE(g.remainingMs(5700), qint64(0));
    }

    /* Two adjustments with a pause between them are each heard. */
    void separateAdjustmentsEachSound()
    {
        BurstGate g(500);
        g.event(1000);
        QVERIFY(g.finish(1500));
        g.event(2000);
        QVERIFY(!g.finish(2100));
        QVERIFY(g.finish(2500));
    }

    /* The window is measured from the latest step, not the first. */
    void runExtendsFromTheLatestStep()
    {
        BurstGate g(500);
        for (qint64 t : {1000, 1300, 1600, 1900}) {
            g.event(t);
        }
        QVERIFY(!g.finish(2300));                // 1300ms after the first, still one run
        QVERIFY(g.finish(2400));
    }

    /* Notification sounds default on and OSD sounds default off: the OSD is
       already a response to a key the user just pressed, and beeping at every
       volume step is the behaviour people turn off first. */
    void defaults()
    {
        SoundPlayer p;
        QVERIFY(p.notificationsEnabled());
        QVERIFY(!p.osdEnabled());
    }
};

GLASSOSD_TEST_MAIN(TestSoundPlayer)
#include "tst_soundplayer.moc"
