/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    How the monitor pairs a requestView() call with its reply, and what it
    refuses to remember.

    The reply carries the job's object path but no interface, so the only way
    to know which application a path belongs to is the (caller, serial) of the
    call it answers. Nothing here opens a bus; the libdbus filter calls these
    same entry points with what it decoded.
*/
#include "jobviewmonitor.h"
#include "testenv.h"

#include <QSignalSpy>

class TestJobViewMonitor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void replyStartsTheJobUnderItsApp()
    {
        JobViewMonitor m;
        QSignalSpy started(&m, &JobViewMonitor::jobStarted);

        m.onRequestView(QStringLiteral(":1.5"), 10, QStringLiteral("org.kde.dolphin"));
        QCOMPARE(started.count(), 0); // nothing until the reply names the path
        m.onRequestReply(QStringLiteral(":1.5"), 10, QStringLiteral("/jobs/JobView_1"));

        QCOMPARE(started.count(), 1);
        QCOMPARE(started.at(0).at(0).toString(), QStringLiteral("/jobs/JobView_1"));
        QCOMPARE(started.at(0).at(1).toString(), QStringLiteral("org.kde.dolphin"));
    }

    /* plasmashell answers hundreds of calls that have nothing to do with jobs,
       and the reply rule is the only one wide enough to catch ours. */
    void unrelatedRepliesAreIgnored()
    {
        JobViewMonitor m;
        QSignalSpy started(&m, &JobViewMonitor::jobStarted);

        m.onRequestView(QStringLiteral(":1.5"), 10, QStringLiteral("org.kde.dolphin"));
        m.onRequestReply(QStringLiteral(":1.5"), 11, QStringLiteral("/other"));  // wrong serial
        m.onRequestReply(QStringLiteral(":1.9"), 10, QStringLiteral("/other"));  // wrong caller
        QCOMPARE(started.count(), 0);
    }

    void twoCallersWithTheSameSerialStayApart()
    {
        JobViewMonitor m;
        QSignalSpy started(&m, &JobViewMonitor::jobStarted);

        m.onRequestView(QStringLiteral(":1.5"), 7, QStringLiteral("org.kde.dolphin"));
        m.onRequestView(QStringLiteral(":1.6"), 7, QStringLiteral("org.kde.ark"));
        m.onRequestReply(QStringLiteral(":1.6"), 7, QStringLiteral("/jobs/JobView_2"));
        m.onRequestReply(QStringLiteral(":1.5"), 7, QStringLiteral("/jobs/JobView_1"));

        QCOMPARE(started.count(), 2);
        QCOMPARE(started.at(0).at(1).toString(), QStringLiteral("org.kde.ark"));
        QCOMPARE(started.at(1).at(1).toString(), QStringLiteral("org.kde.dolphin"));
    }

    void updatesAndTerminateOnlyForKnownJobs()
    {
        JobViewMonitor m;
        QSignalSpy updated(&m, &JobViewMonitor::jobUpdated);
        QSignalSpy ended(&m, &JobViewMonitor::jobEnded);
        const QString path = QStringLiteral("/jobs/JobView_1");

        m.onUpdate(path, {{QStringLiteral("percent"), 5}});
        m.onTerminate(path, 0, QString());
        QCOMPARE(updated.count(), 0); // never announced
        QCOMPARE(ended.count(), 0);

        m.onRequestView(QStringLiteral(":1.5"), 1, QStringLiteral("org.kde.dolphin"));
        m.onRequestReply(QStringLiteral(":1.5"), 1, path);
        m.onUpdate(path, {{QStringLiteral("percent"), 40}});
        QCOMPARE(updated.count(), 1);
        QCOMPARE(updated.at(0).at(1).toMap().value(QStringLiteral("percent")).toInt(), 40);

        m.onTerminate(path, 3, QStringLiteral("disk full"));
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.at(0).at(1).toUInt(), 3u);
        QCOMPARE(ended.at(0).at(2).toString(), QStringLiteral("disk full"));

        m.onUpdate(path, {{QStringLiteral("percent"), 90}});
        m.onTerminate(path, 0, QString());
        QCOMPARE(updated.count(), 1); // closed: later calls are noise
        QCOMPARE(ended.count(), 1);
    }

    /* Any process in the session can call these methods, so the monitor must
       not grow with what it is sent. */
    void openJobsAreBounded()
    {
        JobViewMonitor m;
        QSignalSpy started(&m, &JobViewMonitor::jobStarted);

        for (int i = 0; i < JobViewMonitor::maxJobs + 20; ++i) {
            m.onRequestView(QStringLiteral(":1.5"), quint32(i), QStringLiteral("x"));
            m.onRequestReply(QStringLiteral(":1.5"), quint32(i), QStringLiteral("/j/%1").arg(i));
        }
        QCOMPARE(started.count(), JobViewMonitor::maxJobs);
    }

    void pendingCallsAreBounded()
    {
        JobViewMonitor m;
        QSignalSpy started(&m, &JobViewMonitor::jobStarted);

        for (int i = 0; i < JobViewMonitor::maxPending * 3; ++i) {
            m.onRequestView(QStringLiteral(":1.5"), quint32(i), QStringLiteral("x"));
        }
        /* The oldest were dropped to stay within the bound; the newest survive. */
        m.onRequestReply(QStringLiteral(":1.5"), quint32(JobViewMonitor::maxPending * 3 - 1),
                         QStringLiteral("/last"));
        QCOMPARE(started.count(), 1);
        m.onRequestReply(QStringLiteral(":1.5"), 0, QStringLiteral("/first"));
        QCOMPARE(started.count(), 1);
    }

    /* Apps with no KDE job (browsers, Steam) show progress only as a taskbar
       launcher entry. It becomes the same job sequence. */
    void launcherEntryBecomesAJob()
    {
        JobViewMonitor m;
        QSignalSpy started(&m, &JobViewMonitor::jobStarted);
        QSignalSpy updated(&m, &JobViewMonitor::jobUpdated);
        QSignalSpy ended(&m, &JobViewMonitor::jobEnded);
        const QString uri = QStringLiteral("application://firefox.desktop");

        m.onLauncherEntry(QStringLiteral(":1.9"), uri,
                          {{QStringLiteral("progress"), 0.25}, {QStringLiteral("progress-visible"), true}});
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.at(0).at(1).toString(), QStringLiteral("firefox"));
        QCOMPARE(updated.at(0).at(1).toMap().value(QStringLiteral("percent")).toInt(), 25);

        m.onLauncherEntry(QStringLiteral(":1.9"), uri,
                          {{QStringLiteral("progress"), 1.0}, {QStringLiteral("progress-visible"), true}});
        m.onLauncherEntry(QStringLiteral(":1.9"), uri, {{QStringLiteral("progress-visible"), false}});
        QCOMPARE(started.count(), 1);
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.at(0).at(1).toUInt(), 0u); // reached the end: finished
    }

    void launcherEntryClearedEarlyIsNotFinished()
    {
        JobViewMonitor m;
        QSignalSpy ended(&m, &JobViewMonitor::jobEnded);
        const QString uri = QStringLiteral("application://firefox.desktop");
        m.onLauncherEntry(QStringLiteral(":1.9"), uri,
                          {{QStringLiteral("progress"), 0.4}, {QStringLiteral("progress-visible"), true}});
        m.onLauncherEntry(QStringLiteral(":1.9"), uri, {{QStringLiteral("progress-visible"), false}});
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.at(0).at(1).toUInt(), 1u); // cancelled: no "finished" card
    }

    /* plasmashell re-emits every KIO job as a launcher entry; showing both
       would put two cards up for one copy. */
    void plasmasProxiedEntriesAreIgnored()
    {
        JobViewMonitor m;
        QSignalSpy started(&m, &JobViewMonitor::jobStarted);
        m.onLauncherEntry(QStringLiteral(":1.54"), QStringLiteral("application://org.kde.dolphin"),
                          {{QStringLiteral("progress"), 0.5}, {QStringLiteral("progress-visible"), true},
                           {QStringLiteral("proxied-for"), QStringLiteral("kuiserver")}});
        QCOMPARE(started.count(), 0);
    }

    void launcherEntriesAreBounded()
    {
        JobViewMonitor m;
        QSignalSpy started(&m, &JobViewMonitor::jobStarted);
        for (int i = 0; i < JobViewMonitor::maxJobs + 20; ++i) {
            m.onLauncherEntry(QStringLiteral(":1.%1").arg(i), QStringLiteral("application://x.desktop"),
                              {{QStringLiteral("progress"), 0.1}, {QStringLiteral("progress-visible"), true}});
        }
        QCOMPARE(started.count(), JobViewMonitor::maxJobs);
    }
};

GLASSOSD_TEST_MAIN(TestJobViewMonitor)
#include "tst_jobviewmonitor.moc"
