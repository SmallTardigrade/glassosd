/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Watches the file-operation progress KDE applications report to Plasma.

    Dolphin, Ark and everything else built on KIO do not use the freedesktop
    notification interface for a copy or an extraction. They ask the Plasma
    shell for a "job view" over org.kde.JobViewServerV2.requestView(), get an
    object path back, and then call org.kde.JobViewV3.update() on it every few
    dozen milliseconds with the percentage, bytes, speed and the file being
    handled, and finally terminate(). plasmashell owns that name, so with
    glassosd serving notifications those jobs have nowhere visible to go.

    Taking the name over would also take Plasma's own job handling away, so
    this does the other thing: it listens. A monitor connection sees the calls
    without owning anything, and plasmashell carries on exactly as before.

    A monitor cannot send, and so cannot pause or cancel a job; what it can do
    is show the progress, which is what was missing.

    The requestView() reply is the only thing that names the object path, and
    it does not carry the interface, so it is matched back to its call by
    (caller, serial) rather than by a rule of its own.

    QtDBus has no BecomeMonitor or message-filter API, hence raw libdbus-1 on a
    private connection, pumped from the Qt event loop — the same arrangement as
    OsdMonitor.
*/
#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantMap>

typedef struct DBusConnection DBusConnection;
class QSocketNotifier;

class JobViewMonitor : public QObject
{
    Q_OBJECT
public:
    explicit JobViewMonitor(QObject *parent = nullptr);
    ~JobViewMonitor() override;

    /* Returns false if the bus refused BecomeMonitor; the caller then simply
       has no job progress, as before. */
    bool start();

    /* The decoded form of the four messages that matter. The libdbus filter
       calls these, and so do the tests, which have no bus to listen on. */
    void onRequestView(const QString &caller, quint32 serial, const QString &appId);
    void onRequestReply(const QString &caller, quint32 replySerial, const QString &path);
    void onUpdate(const QString &path, const QVariantMap &props);
    void onTerminate(const QString &path, quint32 code, const QString &message);
    /* A Unity launcher-entry Update(): the taskbar-progress signal that apps
       with no KDE job (browsers, Steam, installers) send. Turned into the same
       started/updated/ended sequence a KIO job produces. */
    void onLauncherEntry(const QString &sender, const QString &appUri, const QVariantMap &props);

    /* Bounds on what an arbitrary session client can make us remember. */
    static constexpr int maxPending = 64;
    static constexpr int maxJobs = 64;

Q_SIGNALS:
    void jobStarted(const QString &path, const QString &appId);
    void jobUpdated(const QString &path, const QVariantMap &props);
    void jobEnded(const QString &path, quint32 code, const QString &message);

private:
    void pump();

    DBusConnection *m_conn = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    /* Calls waiting for their reply: (caller's unique name, serial) -> app. */
    QHash<QPair<QString, quint32>, QString> m_pending;
    /* Paths we have announced and not yet seen terminate. */
    QHash<QString, bool> m_open;
    /* Launcher entries currently showing progress: path -> last fraction. */
    QHash<QString, double> m_launcher;
};
