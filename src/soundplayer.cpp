/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "soundplayer.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

SoundPlayer::SoundPlayer(QObject *parent)
    : QObject(parent)
{
}

bool SoundPlayer::haveBackend() const
{
    static const bool have =
        !QStandardPaths::findExecutable(QStringLiteral("canberra-gtk-play")).isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty();
    return have;
}

void SoundPlayer::play(const QString &name)
{
    if (name.isEmpty() || !haveBackend()) {
        return;
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
                return;
            }
            QProcess::startDetached(canberra, {QStringLiteral("-f"), file});
            return;
        }
        /* -i takes a theme name and resolves it through the user's theme and
           its inheritance chain, which is the whole reason to go through
           canberra rather than play a file. */
        QProcess::startDetached(canberra, {QStringLiteral("-i"), name});
        return;
    }

    /* No canberra: play the file, or the freedesktop copy of the name. Loses
       theming — this will not honour a user's chosen sound theme — but a
       daemon that makes the standard sound is better than one that makes
       none. */
    if (file.isEmpty()) {
        file = QStringLiteral("/usr/share/sounds/freedesktop/stereo/%1.oga").arg(name);
    }
    if (!QFileInfo::exists(file)) {
        return;
    }
    for (const QString &tool : {QStringLiteral("paplay"), QStringLiteral("pw-play")}) {
        const QString exe = QStandardPaths::findExecutable(tool);
        if (!exe.isEmpty()) {
            QProcess::startDetached(exe, {file});
            return;
        }
    }
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
