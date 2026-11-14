/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    What a KDE file operation looks like once it reaches the notification list.

    A job reports itself dozens of times a second. The cases that matter are
    the ones where doing the obvious thing is wrong: a copy that finishes in a
    blink must not flash a card, a card the user dismissed must stay gone while
    the job carries on, and a job that fails must say so even if it failed
    instantly.
*/
#include "historymodel.h"
#include "jobbridge.h"
#include "notificationmodel.h"
#include "notificationserver.h"
#include "testenv.h"

#include <QSignalSpy>

class TestJobBridge : public QObject
{
    Q_OBJECT

private:
    struct Fixture {
        NotificationModel model;
        HistoryModel history;
        NotificationServer server{&model, &history};
        JobBridge bridge{&server};

        Fixture()
        {
            model.setCoalesce(0, 0);
            model.setTimeouts(0, 0, 0);
            bridge.setShowAfterMs(0);
        }

        int rowOf(uint id) const
        {
            for (int r = 0; r < model.rowCount(); ++r) {
                if (model.index(r).data(NotificationModel::IdRole).toUInt() == id) {
                    return r;
                }
            }
            return -1;
        }
        QVariant role(uint id, int r) const { return model.index(rowOf(id)).data(r); }
    };

    static QVariantMap running(int percent)
    {
        return {
            {QStringLiteral("title"), QStringLiteral("Copying")},
            {QStringLiteral("percent"), percent},
            {QStringLiteral("descriptionValue1"), QStringLiteral("/home/u/big & small.iso")},
            {QStringLiteral("destUrl"), QStringLiteral("file:///home/u/Documents")},
            {QStringLiteral("processedBytes"), qulonglong(50) << 20},
            {QStringLiteral("totalBytes"), qulonglong(200) << 20},
            {QStringLiteral("speed"), qulonglong(30) << 20},
        };
    }

private Q_SLOTS:
    void runningJobShowsOneCardThatIsReplacedInPlace()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, running(25));
        f.bridge.flush();

        const uint id = f.bridge.cardFor(job);
        QVERIFY(id != 0);
        QCOMPARE(f.model.rowCount(), 1);
        QCOMPARE(f.role(id, NotificationModel::ProgressRole).toInt(), 25);
        QCOMPARE(f.role(id, NotificationModel::SummaryRole).toString(), QStringLiteral("Copying to Documents"));
        const QString body = f.role(id, NotificationModel::BodyRole).toString();
        QVERIFY2(body.contains(QStringLiteral("big &amp; small.iso")), qPrintable(body));
        QVERIFY2(body.contains(QStringLiteral("25%")), qPrintable(body));

        f.bridge.jobUpdated(job, {{QStringLiteral("percent"), 60}});
        f.bridge.flush();
        QCOMPARE(f.bridge.cardFor(job), id);   // same card
        QCOMPARE(f.model.rowCount(), 1);       // not a second one
        QCOMPARE(f.role(id, NotificationModel::ProgressRole).toInt(), 60);
    }

    void finishedJobSaysSo()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, running(80));
        f.bridge.flush();
        const uint id = f.bridge.cardFor(job);

        f.bridge.jobEnded(job, 0, QString());
        QCOMPARE(f.bridge.jobCount(), 0);
        QCOMPARE(f.role(id, NotificationModel::ProgressRole).toInt(), 100);
        QCOMPARE(f.role(id, NotificationModel::SummaryRole).toString(), QStringLiteral("Copied to Documents"));
    }

    /* Cancel, or Skip on an overwrite prompt, is an answer: no "failed" card. */
    void cancelledJobLeavesNoCard()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, running(30));
        f.bridge.flush();
        QCOMPARE(f.model.rowCount(), 1);
        f.bridge.jobEnded(job, 1, QString());
        QTest::qWait(50);
        QCOMPARE(f.model.rowCount(), 0);
    }

    void otherVerbsReadInThePast()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        QVariantMap p = running(50);
        p.insert(QStringLiteral("title"), QStringLiteral("Extracting"));
        f.bridge.jobStarted(job, QStringLiteral("org.kde.ark"));
        f.bridge.jobUpdated(job, p);
        f.bridge.flush();
        const uint id = f.bridge.cardFor(job);
        f.bridge.jobEnded(job, 0, QString());
        QCOMPARE(f.role(id, NotificationModel::SummaryRole).toString(), QStringLiteral("Extracted to Documents"));
    }

    /* A finished card reports the outcome. The file it happened to be on when
       it stopped is not news. */
    void finishedCardDropsTheLastFile()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        QVariantMap p = running(99);
        p.insert(QStringLiteral("totalFiles"), 40000);
        p.insert(QStringLiteral("elapsedTime"), 23919);
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, p);
        f.bridge.flush();
        const uint id = f.bridge.cardFor(job);

        f.bridge.jobEnded(job, 0, QString());
        const QString body = f.role(id, NotificationModel::BodyRole).toString();
        QVERIFY2(!body.contains(QStringLiteral("small.iso")), qPrintable(body));
        QVERIFY2(body.contains(QStringLiteral("40000")) || body.contains(QStringLiteral("40,000")), qPrintable(body));
        QVERIFY2(body.contains(QStringLiteral("24 s")), qPrintable(body));
        /* StyledText ignores a newline, so lines must be <br> or they run on. */
        QVERIFY2(body.contains(QStringLiteral("<br>")), qPrintable(body));
        QVERIFY2(!body.contains(QLatin1Char('\n')), qPrintable(body));
    }

    void timeLeftAppearsOnceThereIsSomethingToGoOn()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));

        /* 50 of 200 MiB at 30 MiB/s: 5 s left — but only 1 s in, so no guess. */
        QVariantMap early = running(25);
        early.insert(QStringLiteral("elapsedTime"), 1000);
        f.bridge.jobUpdated(job, early);
        f.bridge.flush();
        const uint id = f.bridge.cardFor(job);
        QVERIFY(!f.role(id, NotificationModel::BodyRole).toString().contains(QStringLiteral("left")));

        QVariantMap later = running(25);
        later.insert(QStringLiteral("elapsedTime"), 8000);
        f.bridge.jobUpdated(job, later);
        f.bridge.flush();
        const QString body = f.role(id, NotificationModel::BodyRole).toString();
        QVERIFY2(body.contains(QStringLiteral("left")), qPrintable(body));
    }

    /* A name with no spaces cannot wrap, so it ran out of the card. */
    void longNamesAreShortenedInTheMiddle()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        QVariantMap p = running(10);
        const QString name = QString(80, QLatin1Char('a')) + QStringLiteral(".tar.gz");
        p.insert(QStringLiteral("descriptionValue1"), QStringLiteral("/home/u/") + name);
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, p);
        f.bridge.flush();

        const QString first = f.role(f.bridge.cardFor(job), NotificationModel::BodyRole).toString()
                                  .split(QStringLiteral("<br>")).first();
        QVERIFY2(first.size() <= 40, qPrintable(first));
        QVERIFY2(first.endsWith(QStringLiteral(".tar.gz")), qPrintable(first)); // the extension survives
    }

    /* On a fast disk a copy is over before anyone could read a card. */
    void quickJobNeverShowsACard()
    {
        Fixture f;
        f.bridge.setShowAfterMs(60'000);
        const QString job = QStringLiteral("/j/1");
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, running(50));
        f.bridge.flush();
        f.bridge.jobEnded(job, 0, QString());

        QCOMPARE(f.model.rowCount(), 0);
    }

    /* ...but a failure is worth saying however briefly the job ran. */
    void quickFailureStillShows()
    {
        Fixture f;
        f.bridge.setShowAfterMs(60'000);
        const QString job = QStringLiteral("/j/1");
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, running(5));
        f.bridge.jobEnded(job, 4, QStringLiteral("No space left on device"));

        QCOMPARE(f.model.rowCount(), 1);
        const QString body = f.model.index(0).data(NotificationModel::BodyRole).toString();
        QVERIFY2(body.contains(QStringLiteral("No space left")), qPrintable(body));
        QVERIFY(f.model.index(0).data(NotificationModel::SummaryRole).toString().contains(QStringLiteral("failed")));
    }

    void dismissedCardStaysDismissed()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, running(10));
        f.bridge.flush();
        const uint id = f.bridge.cardFor(job);

        f.model.dismiss(id);
        QTest::qWait(50); // let the exit animation finish and the close signal fire
        f.bridge.jobUpdated(job, running(70));
        f.bridge.flush();
        QCOMPARE(f.model.rowCount(), 0);          // not put back
        f.bridge.jobEnded(job, 0, QString());
        QCOMPARE(f.model.rowCount(), 0);          // and no "finished" either
    }

    /* Nothing in a card is markup the sender gets to choose. */
    void hostileNamesAreInert()
    {
        Fixture f;
        const QString job = QStringLiteral("/j/1");
        QVariantMap p = running(10);
        p.insert(QStringLiteral("descriptionValue1"), QStringLiteral("<img src='file:///etc/passwd'>.txt"));
        f.bridge.jobStarted(job, QStringLiteral("org.kde.dolphin"));
        f.bridge.jobUpdated(job, p);
        f.bridge.flush();

        const QString body = f.role(f.bridge.cardFor(job), NotificationModel::BodyRole).toString();
        QVERIFY2(!body.contains(QStringLiteral("<img")), qPrintable(body));
    }

    void jobsAreIndependent()
    {
        Fixture f;
        f.bridge.jobStarted(QStringLiteral("/j/1"), QStringLiteral("org.kde.dolphin"));
        f.bridge.jobStarted(QStringLiteral("/j/2"), QStringLiteral("org.kde.ark"));
        f.bridge.jobUpdated(QStringLiteral("/j/1"), running(10));
        f.bridge.jobUpdated(QStringLiteral("/j/2"), running(90));
        f.bridge.flush();

        QVERIFY(f.bridge.cardFor(QStringLiteral("/j/1")) != f.bridge.cardFor(QStringLiteral("/j/2")));
        QCOMPARE(f.role(f.bridge.cardFor(QStringLiteral("/j/1")), NotificationModel::ProgressRole).toInt(), 10);
        QCOMPARE(f.role(f.bridge.cardFor(QStringLiteral("/j/2")), NotificationModel::ProgressRole).toInt(), 90);
    }
};

GLASSOSD_TEST_MAIN(TestJobBridge)
#include "tst_jobbridge.moc"
