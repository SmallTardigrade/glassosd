/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Notification history.

    Every test in here is a bug that actually happened. HistoryLength was read
    after the file had already been truncated, so anyone raising it lost the
    excess on the next start. Identical repeats were kept a row each, so one
    afternoon of PowerDevil produced 158 copies of the same line. A group of
    one row standing for 156 arrivals lost its header and read as a member of
    whatever was above it. None of these looked like a crash; they looked like
    the program working.
*/
#include "historymodel.h"
#include "testenv.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

class TestHistoryModel : public QObject
{
    Q_OBJECT

private:
    static QString storePath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/glassosd/history.json");
    }

    /* Both files are shared by every test in the process, so each one starts
       by saying what it wants them to contain. */
    static void writeStore(const QJsonArray &arr)
    {
        const QString path = storePath();
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(arr).toJson());
    }

    static void clearStore()
    {
        QFile::remove(storePath());
    }

    static void setHistoryLength(int n)
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("glassosdrc"));
        config->group(QStringLiteral("Notifications")).writeEntry("HistoryLength", n);
        config->sync();
        config->reparseConfiguration();
    }

    static QJsonObject entry(const QString &app, const QString &summary,
                             const QString &body = {}, int id = 0,
                             const QDateTime &at = QDateTime::currentDateTimeUtc())
    {
        return QJsonObject{
            {QStringLiteral("id"), int(id)},
            {QStringLiteral("app"), app},
            {QStringLiteral("summary"), summary},
            {QStringLiteral("body"), body},
            {QStringLiteral("urgency"), 1},
            {QStringLiteral("at"), at.toString(Qt::ISODate)},
        };
    }

    /* Ids come from a counter because the notification server hands out a
       fresh one for every arrival, and record() treats a repeated id as the
       same message updating itself. Two unrelated notifications sharing an id
       is not a case that occurs; two sharing their text very much is. */
    static Notification note(const QString &app, const QString &summary,
                             const QString &body = {}, uint id = 0)
    {
        static uint next = 1000;
        Notification n;
        n.id = id ? id : next++;
        n.appName = app;
        n.summary = summary;
        n.body = body;
        n.received = QDateTime::currentDateTimeUtc();
        return n;
    }

private Q_SLOTS:
    /* The bug: load() truncates to m_capacity as it reads, and the model is a
       QML singleton, so main.cpp could only call setCapacity() after the file
       had already been cut short. Raising HistoryLength therefore worked for
       the session and silently lost everything past 200 on the next start,
       while glassosdctl went on reporting the larger number. */
    void capacityIsReadBeforeLoading()
    {
        setHistoryLength(250);
        QJsonArray arr;
        for (int i = 0; i < 240; ++i) {
            arr.append(entry(QStringLiteral("App"), QStringLiteral("message %1").arg(i)));
        }
        writeStore(arr);

        HistoryModel h;
        QCOMPARE(h.total(), 240);   // 200 was the bug
    }

    void capacityTruncatesOnLoad()
    {
        setHistoryLength(5);
        QJsonArray arr;
        for (int i = 0; i < 20; ++i) {
            arr.append(entry(QStringLiteral("App"), QStringLiteral("message %1").arg(i)));
        }
        writeStore(arr);

        HistoryModel h;
        QCOMPARE(h.total(), 5);
    }

    /* An older file has the repeats one row each; nothing but a pass on load
       would ever fold them together, since only a new arrival triggers a
       merge. */
    void loadCollapsesDuplicatesWrittenByOlderBuilds()
    {
        setHistoryLength(200);
        QJsonArray arr;
        for (int i = 0; i < 6; ++i) {
            arr.append(entry(QStringLiteral("Battery Monitor"),
                             QStringLiteral("Device Battery Low (6% Remaining)")));
        }
        arr.append(entry(QStringLiteral("Battery Monitor"),
                         QStringLiteral("Device Battery Low (9% Remaining)")));
        writeStore(arr);

        HistoryModel h;
        QCOMPARE(h.total(), 2);   // one per distinct message
    }

    /* Ids are only unique within one run of the daemon, so the server has to
       start above whatever the file already used or a fresh notification
       overwrites an unrelated old entry. */
    void maxLoadedIdIsTheHighestInTheFile()
    {
        setHistoryLength(200);
        writeStore({entry(QStringLiteral("A"), QStringLiteral("one"), {}, 4),
                    entry(QStringLiteral("A"), QStringLiteral("two"), {}, 91),
                    entry(QStringLiteral("A"), QStringLiteral("three"), {}, 12)});

        HistoryModel h;
        QCOMPARE(h.maxLoadedId(), 91u);
    }

    void missingFileIsNotAnError()
    {
        clearStore();
        HistoryModel h;
        QCOMPARE(h.total(), 0);
        QCOMPARE(h.rowCount(), 0);
    }

    /* A byte-identical repeat becomes a count, not another row. */
    void recordMergesIdenticalRepeats()
    {
        clearStore();
        HistoryModel h;
        for (int i = 0; i < 3; ++i) {
            h.record(note(QStringLiteral("Battery Monitor"),
                          QStringLiteral("Device Battery Low (6% Remaining)")));
        }
        QCOMPARE(h.total(), 1);

        /* One arrival is one row with no header; three arrivals keep the
           header, so the row cannot be read as belonging to the group above
           it. That is the whole reason the count is occurrences, not rows. */
        QCOMPARE(h.rowCount(), 2);
        QVERIFY(h.index(0).data(HistoryModel::IsHeaderRole).toBool());
        QCOMPARE(h.index(0).data(HistoryModel::GroupCountRole).toInt(), 3);
        QCOMPARE(h.index(1).data(HistoryModel::RepeatCountRole).toInt(), 3);
    }

    /* A changed percentage is news, not a repeat. */
    void recordKeepsChangedContentSeparate()
    {
        clearStore();
        HistoryModel h;
        for (const QString &pct : {QStringLiteral("12"), QStringLiteral("9"),
                                   QStringLiteral("6")}) {
            h.record(note(QStringLiteral("Battery Monitor"),
                          QStringLiteral("Device Battery Low (%1% Remaining)").arg(pct)));
        }
        QCOMPARE(h.total(), 3);
    }

    /* The single most annoying shape of a full history panel: one app's
       entries pushing everything else off the screen. A group of one arrival
       has no header at all — with one entry the header says nothing the entry
       does not already say. */
    void singleArrivalGetsNoHeader()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("Signal"), QStringLiteral("hello")));
        QCOMPARE(h.rowCount(), 1);
        QVERIFY(!h.index(0).data(HistoryModel::IsHeaderRole).toBool());
    }

    /* The header count is arrivals, which is what a person counts — not the
       number of rows we happened to draw them in. */
    void headerCountsOccurrencesNotRows()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("App"), QStringLiteral("first")));
        h.record(note(QStringLiteral("App"), QStringLiteral("second")));
        h.record(note(QStringLiteral("App"), QStringLiteral("second")));
        h.record(note(QStringLiteral("App"), QStringLiteral("second")));

        QCOMPARE(h.total(), 2);   // two rows
        QVERIFY(h.index(0).data(HistoryModel::IsHeaderRole).toBool());
        QCOMPARE(h.index(0).data(HistoryModel::GroupCountRole).toInt(), 4);
    }

    void historyIgnoreIsNotRecorded()
    {
        clearStore();
        HistoryModel h;
        Notification n = note(QStringLiteral("Steam"), QStringLiteral("friend online"));
        n.historyIgnore = true;
        h.record(n);
        QCOMPARE(h.total(), 0);
        QCOMPARE(h.unread(), 0);
    }

    /* replaces_id is the same message updating itself — a file copy would
       otherwise leave a hundred entries behind. */
    void replacesIdUpdatesInPlace()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("Dolphin"), QStringLiteral("Copying"),
                      QStringLiteral("20%"), 7));
        h.record(note(QStringLiteral("Dolphin"), QStringLiteral("Copying"),
                      QStringLiteral("80%"), 7));
        QCOMPARE(h.total(), 1);
        QCOMPARE(h.index(0).data(HistoryModel::BodyRole).toString(), QStringLiteral("80%"));
    }

    /* The app name has to match as well as the id. History outlives daemon
       restarts and ids do not, so id alone let a fresh notification overwrite
       an unrelated old entry — silently, and in that entry's old position,
       which is what put the file out of time order in the first place. */
    void replacesIdRequiresTheSameApp()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("Dolphin"), QStringLiteral("Copying"),
                      QStringLiteral("20%"), 7));
        h.record(note(QStringLiteral("Signal"), QStringLiteral("hello"),
                      QStringLiteral("there"), 7));
        QCOMPARE(h.total(), 2);
    }

    void capacityTrimsTheOldest()
    {
        clearStore();
        HistoryModel h;
        h.setCapacity(2);
        h.record(note(QStringLiteral("App"), QStringLiteral("one")));
        h.record(note(QStringLiteral("App"), QStringLiteral("two")));
        h.record(note(QStringLiteral("App"), QStringLiteral("three")));

        QCOMPARE(h.total(), 2);
        h.setSearch(QStringLiteral("one"));
        QCOMPARE(h.matchCount(), 0);      // the oldest is the one that went
        h.setSearch(QStringLiteral("three"));
        QCOMPARE(h.matchCount(), 1);
    }

    /* A capacity of zero would be a config typo that emptied history on every
       arrival, so it is clamped rather than obeyed. */
    void capacityIsClamped()
    {
        clearStore();
        HistoryModel h;
        h.setCapacity(0);
        h.record(note(QStringLiteral("App"), QStringLiteral("one")));
        QCOMPARE(h.total(), 1);
    }

    void searchLooksAtAppSummaryAndBody()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("Thunderbird"), QStringLiteral("Invoice"),
                      QStringLiteral("due Friday")));
        h.record(note(QStringLiteral("Signal"), QStringLiteral("hello"),
                      QStringLiteral("about the invoice")));
        h.record(note(QStringLiteral("Steam"), QStringLiteral("Sale"),
                      QStringLiteral("50% off")));

        h.setSearch(QStringLiteral("invoice"));    // summary, body, insensitive
        QCOMPARE(h.matchCount(), 2);

        h.setSearch(QStringLiteral("steam"));      // app name
        QCOMPARE(h.matchCount(), 1);

        h.setSearch(QStringLiteral("nothing here"));
        QCOMPARE(h.matchCount(), 0);
        QCOMPARE(h.rowCount(), 0);

        h.setSearch(QString());
        QCOMPARE(h.matchCount(), 3);
    }

    /* Typing into search while drilled into one app must search everything,
       or it looks like it searched everything and quietly did not. */
    void searchClearsTheGroupFilter()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("Thunderbird"), QStringLiteral("Invoice")));
        h.record(note(QStringLiteral("Signal"), QStringLiteral("Invoice")));

        h.setGroupFilter(QStringLiteral("Signal"));
        QCOMPARE(h.matchCount(), 1);

        h.setSearch(QStringLiteral("invoice"));
        QCOMPARE(h.matchCount(), 2);
        QVERIFY(h.groupFilter().isEmpty());
    }

    /* unread answers "is there anything new", which total() cannot: total is
       the whole backlog and is almost never zero. */
    void unreadStopsCountingWhileTheCentreIsOpen()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("App"), QStringLiteral("one")));
        h.record(note(QStringLiteral("App"), QStringLiteral("two")));
        QCOMPARE(h.unread(), 2);

        h.setPanelOpen(true);
        QCOMPARE(h.unread(), 0);
        h.record(note(QStringLiteral("App"), QStringLiteral("three")));
        QCOMPARE(h.unread(), 0);          // you are looking at it

        h.setPanelOpen(false);
        h.record(note(QStringLiteral("App"), QStringLiteral("four")));
        QCOMPARE(h.unread(), 1);
    }

    /* Closing the centre has to drop the drill-in, or reopening shows one
       app's notifications while looking like it shows all of them. */
    void closingTheCentreResetsFilterAndSearch()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("Signal"), QStringLiteral("hello")));
        h.record(note(QStringLiteral("Steam"), QStringLiteral("sale")));

        h.setPanelOpen(true);
        h.setGroupFilter(QStringLiteral("Signal"));
        h.setPanelOpen(false);

        QVERIFY(h.groupFilter().isEmpty());
        QVERIFY(h.search().isEmpty());
        QCOMPARE(h.matchCount(), 2);
    }

    void clearAllEmptiesEverything()
    {
        clearStore();
        HistoryModel h;
        h.record(note(QStringLiteral("App"), QStringLiteral("one")));
        h.record(note(QStringLiteral("App"), QStringLiteral("two")));
        h.clearAll();
        QCOMPARE(h.total(), 0);
        QCOMPARE(h.rowCount(), 0);
    }

    /* Round trip: what save() writes, load() has to read back. The counts on
       deduplicated entries are the part that is easy to drop, because they
       live nowhere but that field. */
    void savedHistoryReloads()
    {
        clearStore();
        {
            HistoryModel h;
            h.record(note(QStringLiteral("Battery Monitor"), QStringLiteral("Low")));
            h.record(note(QStringLiteral("Battery Monitor"), QStringLiteral("Low")));
            h.record(note(QStringLiteral("Signal"), QStringLiteral("hello"),
                          QStringLiteral("there")));
            h.save();
        }

        HistoryModel reloaded;
        QCOMPARE(reloaded.total(), 2);
        reloaded.setSearch(QStringLiteral("Low"));
        QCOMPARE(reloaded.rowCount(), 2);   // header, then the entry
        QCOMPARE(reloaded.index(1).data(HistoryModel::RepeatCountRole).toInt(), 2);
    }
};

GLASSOSD_TEST_MAIN(TestHistoryModel)
#include "tst_historymodel.moc"
