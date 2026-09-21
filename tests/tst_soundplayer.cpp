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
    /* One press of the volume key: one sound, straight away, and nothing owed
       afterwards — the tap was already heard at its final level. */
    void singleTapSoundsOnceAndOwesNothing()
    {
        BurstGate g(250);
        QVERIFY(g.event(1000));
        QVERIFY(!g.finish(1250));
    }

    /* The case this exists for. A held key repeats every 40ms for a second:
       one sound as it starts, silence while it runs, one after it is let go.
       Sounding on every step was 26 overlapping copies of a 300ms sound. */
    void heldKeySoundsAtStartAndAfterRelease()
    {
        BurstGate g(250);
        int sounded = 0;
        qint64 t = 1000;
        for (int i = 0; i < 26; ++i, t += 40) {
            if (g.event(t))
                ++sounded;
        }
        QCOMPARE(sounded, 1);                    // only the first step
        const qint64 lastStep = t - 40;

        QVERIFY(!g.finish(lastStep + 100));      // not quiet long enough yet
        QVERIFY(g.finish(lastStep + 250));       // the level you ended on
        QVERIFY(!g.finish(lastStep + 600));      // owed exactly once
    }

    /* The live failure this suite missed, because the other tests pass exact
       times: a timer firing a little inside the window. finish() must say
       "not yet", and remainingMs() must say how long to wait, or the sound
       after release is silently lost. */
    void earlyTimerIsToldHowLongToWait()
    {
        BurstGate g(250);
        QVERIFY(g.event(1000));
        QVERIFY(!g.event(1040));                 // swallowed: a trailing sound is owed
        QVERIFY(!g.finish(1278));                // coarse timer, 12ms early
        QCOMPARE(g.remainingMs(1278), qint64(12));
        QCOMPARE(g.remainingMs(1290), qint64(0));
        QVERIFY(g.finish(1290));                 // still owed, not lost
    }

    void remainingIsZeroWithNoRun()
    {
        BurstGate g(250);
        QCOMPARE(g.remainingMs(5000), qint64(0));
        QVERIFY(g.event(5000));
        QVERIFY(!g.finish(5300));                // single tap: nothing owed
        QCOMPARE(g.remainingMs(5400), qint64(0));
    }

    /* Two deliberate presses with a pause between them are two separate
       adjustments, and each is heard. */
    void separatePressesEachSound()
    {
        BurstGate g(250);
        QVERIFY(g.event(1000));
        QVERIFY(!g.finish(1300));
        QVERIFY(g.event(1500));
    }

    /* A press that lands after the quiet window but before the trailing timer
       has run starts a new run rather than extending the old one — so it
       sounds immediately instead of being swallowed. */
    void pressAfterQuietWindowStartsANewRun()
    {
        BurstGate g(250);
        QVERIFY(g.event(1000));
        QVERIFY(!g.event(1040));                 // swallowed
        QVERIFY(g.event(1400));                  // 360ms later: a new run
    }

    /* Presses closer together than the window, even slow ones, stay one run:
       the window is measured from the latest press, not the first. */
    void runExtendsFromTheLatestPress()
    {
        BurstGate g(250);
        QVERIFY(g.event(1000));
        QVERIFY(!g.event(1200));
        QVERIFY(!g.event(1400));
        QVERIFY(!g.event(1600));                 // 600ms after the first, still one run
        QVERIFY(!g.finish(1700));
        QVERIFY(g.finish(1850));
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
