/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Test harness plumbing.

    Every class worth testing here reaches the disk: HistoryModel persists to
    GenericDataLocation, SnoozeStore beside it, and the rules engine reads
    glassosdrc through KSharedConfig. Left alone they would read and *write*
    the history of whoever ran the suite, which is both a wrong answer and a
    rude one. So each test binary redirects the XDG directories into a
    temporary tree of its own before anything can look at them, and removes it
    afterwards.

    Before, not during: KSharedConfig caches the config object by name on
    first open, so a redirect applied once the test has started would be read
    from the real location anyway.
*/
#pragma once

#include <QDir>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTest>

namespace GlassTest
{
/* Returns the root so main() can delete it again. */
inline QString useTempXdgDirs()
{
    const QString root = QDir::tempPath()
        + QStringLiteral("/glassosd-test-%1").arg(QCoreApplication::applicationPid());
    QDir(root).removeRecursively();
    for (const char *sub : {"config", "data", "cache", "state", "runtime"}) {
        QDir().mkpath(root + QLatin1Char('/') + QLatin1String(sub));
    }
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(root + QStringLiteral("/config")));
    qputenv("XDG_DATA_HOME", QFile::encodeName(root + QStringLiteral("/data")));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(root + QStringLiteral("/cache")));
    qputenv("XDG_STATE_HOME", QFile::encodeName(root + QStringLiteral("/state")));
    /* Config dirs a distribution ships would otherwise still be read, and a
       packaged glassosdrc under /etc would change what the rules tests see. */
    qputenv("XDG_CONFIG_DIRS", QFile::encodeName(root + QStringLiteral("/config")));
    qputenv("XDG_DATA_DIRS", QFile::encodeName(root + QStringLiteral("/data")));

    /* Qt warns loudly without one, and a build root has none. 0700 because
       that is what the specification requires of it. */
    const QString runtime = root + QStringLiteral("/runtime");
    QFile::setPermissions(runtime, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime));

    /* No display in CI, and none needed: nothing here creates a window. */
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    return root;
}
} // namespace GlassTest

/* QTEST_MAIN's job, with the redirect wedged in ahead of the application. */
#define GLASSOSD_TEST_MAIN(TestClass)                                          \
    int main(int argc, char *argv[])                                           \
    {                                                                          \
        const QString testRoot = GlassTest::useTempXdgDirs();                  \
        QGuiApplication app(argc, argv);                                       \
        app.setOrganizationName(QStringLiteral("glassosd-test"));              \
        app.setApplicationName(QStringLiteral("glassosd-test"));               \
        TestClass tc;                                                          \
        const int rc = QTest::qExec(&tc, argc, argv);                          \
        QDir(testRoot).removeRecursively();                                    \
        return rc;                                                             \
    }
