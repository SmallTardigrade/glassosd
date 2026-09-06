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
