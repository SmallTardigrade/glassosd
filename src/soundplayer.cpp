/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "soundplayer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <csignal>
#include <sys/types.h>

bool BurstGate::event(qint64 nowMs)
{
    if (!m_active || nowMs - m_last >= m_quietMs) {
        m_active = true;
        m_swallowed = false;
        m_last = nowMs;
        return true;
    }
    m_last = nowMs;
    m_swallowed = true;
    return false;
}

bool BurstGate::finish(qint64 nowMs)
{
    /* Too early, or no run: leave any run active. The caller asks
       remainingMs() how long to wait before asking again. */
    if (!m_active || nowMs - m_last < m_quietMs) {
        return false;
    }
    m_active = false;
    const bool owed = m_swallowed;
    m_swallowed = false;
    return owed;
}

qint64 BurstGate::remainingMs(qint64 nowMs) const
{
    if (!m_active) {
        return 0;
    }
    return qMax<qint64>(0, m_quietMs - (nowMs - m_last));
}

SoundPlayer::SoundPlayer(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_volumeTrailing.setSingleShot(true);
    connect(&m_volumeTrailing, &QTimer::timeout, this, [this] {
        const qint64 now = m_clock.elapsed();
        /* Fired early: wait out the rest instead of dropping the sound. This
           was the bug — a held key made its first sound and never its last,
           because the timer landed a few milliseconds inside the window,
           finish() correctly said the run was not over, and nothing asked
           again. */
        if (const qint64 left = m_volumeGate.remainingMs(now); left > 0) {
            m_volumeTrailing.start(int(left));
            return;
        }
        if (m_volumeGate.finish(now)) {
            playVolumeChange();
        }
    });
}

bool SoundPlayer::haveBackend() const
{
    static const bool have =
        !QStandardPaths::findExecutable(QStringLiteral("canberra-gtk-play")).isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty();
    return have;
}

qint64 SoundPlayer::play(const QString &name)
{
    if (name.isEmpty() || !haveBackend()) {
        return 0;
    }

    /* A path rather than a name. Themes are the better answer — they follow
       whatever the user has chosen and inherit sensibly — but somebody with a
       single .ogg they like should not have to build a theme around it. */
    QString file;
    if (name.startsWith(QLatin1Char('/'))) {
        file = name;
    } else if (name.startsWith(QLatin1String("~/"))) {
        file = QDir::homePath() + name.mid(1);
    }

    const QString canberra =
        QStandardPaths::findExecutable(QStringLiteral("canberra-gtk-play"));
    if (!canberra.isEmpty()) {
        if (!file.isEmpty()) {
            if (!QFileInfo::exists(file)) {
                qWarning("glassosd: sound file not found: %s", qUtf8Printable(file));
                return 0;
            }
            qint64 pid = 0;
            QProcess::startDetached(canberra, {QStringLiteral("-f"), file}, QString(), &pid);
            return pid;
        }
        /* -i takes a theme name and resolves it through the user's theme and
           its inheritance chain, which is the whole reason to go through
           canberra rather than play a file. */
        qint64 pid = 0;
        QProcess::startDetached(canberra, {QStringLiteral("-i"), name}, QString(), &pid);
        return pid;
    }

    /* No canberra: play the file, or the freedesktop copy of the name. Loses
       theming — this will not honour a user's chosen sound theme — but a
       daemon that makes the standard sound is better than one that makes
       none. */
    if (file.isEmpty()) {
        file = QStringLiteral("/usr/share/sounds/freedesktop/stereo/%1.oga").arg(name);
    }
    if (!QFileInfo::exists(file)) {
        return 0;
    }
    for (const QString &tool : {QStringLiteral("paplay"), QStringLiteral("pw-play")}) {
        const QString exe = QStandardPaths::findExecutable(tool);
        if (!exe.isEmpty()) {
            qint64 pid = 0;
            QProcess::startDetached(exe, {file}, QString(), &pid);
            return pid;
        }
    }
    return 0;
}

void SoundPlayer::playNotification(const QString &name)
{
    if (!m_notifications) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_lastPlayed.isValid() && m_lastPlayed.msecsTo(now) < m_minGapMs) {
        return;
    }
    m_lastPlayed = now;
    play(name);
}

void SoundPlayer::playOsd(const QString &name)
{
    if (!m_osd) {
        return;
    }

    /* Volume goes through the burst gate: first and last of a run only. The
       timer is re-armed by every swallowed step, so it fires quietMs after the
       key is let go rather than quietMs after the run began. */
    if (name == QLatin1String("audio-volume-change")) {
        if (m_volumeGate.event(m_clock.elapsed())) {
            playVolumeChange();
        } else {
            m_volumeTrailing.start(m_volumeGate.quietMs());
        }
        return;
    }

    /* Deliberately outside the notification rate limit's reach in one
       direction only: holding a volume key is a stream of OSD events and
       should not be silenced by a notification that happened to arrive, but
       neither should it machine-gun. */
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_lastPlayed.isValid() && m_lastPlayed.msecsTo(now) < 60) {
        return;
    }
    m_lastPlayed = now;
    play(name);
}

void SoundPlayer::playVolumeChange()
{
    stopPreviousVolumeChange();
    m_volumePid = play(m_osdSound.isEmpty() ? QStringLiteral("audio-volume-change") : m_osdSound);
}

void SoundPlayer::stopPreviousVolumeChange()
{
    if (m_volumePid <= 0) {
        return;
    }
    const qint64 pid = m_volumePid;
    m_volumePid = 0;

    /* Checked before signalling. The player is detached and exits on its own
       after a third of a second, and a pid that has been freed can be handed
       to something else — so a bare kill() on a remembered number could, in
       principle, terminate an unrelated process of the user's. Only signal it
       if it is still one of our players. comm is truncated to 15 characters,
       hence the prefix match. */
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly)) {
        return;   // already gone
    }
    const QByteArray name = comm.readAll().trimmed();
    if (name.startsWith("canberra-gtk-p") || name == "paplay" || name == "pw-play") {
        ::kill(pid_t(pid), SIGTERM);
    }
}

QString SoundPlayer::nameFor(const QString &category, int urgency)
{
    /* Categories from the freedesktop notification spec. Only the ones with a
       matching sound in the XDG sound naming spec are mapped; anything else
       falls through to urgency, which is the honest answer rather than
       inventing a name no theme ships. */
    const QString c = category.toLower();
    if (c.startsWith(QLatin1String("im"))) {
        return QStringLiteral("message-new-instant");
    }
    if (c.startsWith(QLatin1String("email"))) {
        return QStringLiteral("message");
    }
    if (c == QLatin1String("device.added")) {
        return QStringLiteral("device-added");
    }
    if (c == QLatin1String("device.removed")) {
        return QStringLiteral("device-removed");
    }
    if (c == QLatin1String("network.connected")) {
        return QStringLiteral("network-connectivity-established");
    }
    if (c == QLatin1String("network.disconnected")) {
        return QStringLiteral("network-connectivity-lost");
    }
    if (c == QLatin1String("transfer.complete")) {
        return QStringLiteral("complete");
    }
    if (c == QLatin1String("transfer.error")) {
        return QStringLiteral("dialog-error");
    }

    switch (urgency) {
    case 2:   // critical
        return QStringLiteral("dialog-warning");
    case 1:   // normal
        return QStringLiteral("message");
    default:  // low — arrived, but not worth a noise
        return {};
    }
}
