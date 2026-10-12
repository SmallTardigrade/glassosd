/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    The snooze store keeps deferred notifications on disk so they survive a
    restart, which makes its file the only copy of each one. It had the same
    two flaws history had: an interrupted save left a truncated file, and a
    file that did not parse was read as empty and then overwritten.
*/
#include "snoozestore.h"
#include "testenv.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <sys/stat.h>

class TestSnoozeStore : public QObject
{
    Q_OBJECT

private:
    static QString storePath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/glassosd/snoozed.json");
    }

    static Notification note(const QString &summary)
    {
        Notification n;
        n.appName = QStringLiteral("App");
        n.summary = summary;
        return n;
    }

    static ino_t inode(const QString &path)
    {
        struct stat st{};
        return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_ino : 0;
    }

private Q_SLOTS:
    void init()
    {
        QFile::remove(storePath());
        QFile::remove(storePath() + QStringLiteral(".corrupt"));
    }

    void snoozedNotificationsSurviveARestart()
    {
        {
            SnoozeStore s;
            s.snooze(note(QStringLiteral("later")), 60);
        }
        SnoozeStore reloaded;
        QCOMPARE(reloaded.count(), 1);
    }

    /* See tst_historymodel: a new inode means the file was replaced whole. */
    void saveReplacesTheFileRatherThanRewritingIt()
    {
        SnoozeStore s;
        s.snooze(note(QStringLiteral("one")), 60);
        const ino_t first = inode(storePath());
        QVERIFY(first != 0);
        s.snooze(note(QStringLiteral("two")), 60);
        QVERIFY(inode(storePath()) != first);
    }

    void corruptFileIsSetAsideNotLost()
    {
        const QByteArray damaged = "[{\"wakeAt\":\"2030-01-01T00:00:00Z\",\"summary\":\"kept\"},{\"wa";
        {
            QDir().mkpath(QFileInfo(storePath()).path());
            QFile f(storePath());
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(damaged);
        }

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not be read")));
        SnoozeStore s;
        QCOMPARE(s.count(), 0);

        QFile kept(storePath() + QStringLiteral(".corrupt"));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), damaged);
    }
};

GLASSOSD_TEST_MAIN(TestSnoozeStore)
#include "tst_snoozestore.moc"
