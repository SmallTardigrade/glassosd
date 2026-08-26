/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Notification and OSD sounds, by freedesktop sound-theme name.

    swaync's answer to sounds is "write a shell script that calls play". The
    names are already standardised — the XDG sound naming spec defines
    message-new-instant, dialog-warning, audio-volume-change and the rest, and
    themes ship them at /usr/share/sounds/<theme>/stereo/<name>.oga — so a
    daemon can simply ask for the right name and let the user's chosen theme
    answer.

    Played through canberra-gtk-play rather than by linking libcanberra: there
    is no -devel package on a default Fedora, so linking it would add a build
    dependency to gain nothing. The CLI resolves the theme, honours the user's
    Sounds/Theme setting and falls back through theme inheritance on its own.
    paplay and pw-play stand in if it is missing, playing the freedesktop file
    directly — no theming, but not silence either.
*/
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

class SoundPlayer : public QObject
{
    Q_OBJECT
public:
    explicit SoundPlayer(QObject *parent = nullptr);

    /* name is a sound-theme name such as "message-new-instant", not a path. */
    void play(const QString &name);

    void setNotificationsEnabled(bool on) { m_notifications = on; }
    void setOsdEnabled(bool on) { m_osd = on; }
    bool notificationsEnabled() const { return m_notifications; }
    bool osdEnabled() const { return m_osd; }

    /* Rate limited together: a wake-from-suspend burst arriving as twelve
       notifications should be one sound, not twelve. */
    void playNotification(const QString &name);
    void playOsd(const QString &name);

    /* Which sound a notification should make, before any rule override:
       category first because it says what the thing *is*, urgency second
       because it only says how much it matters. Empty means stay silent. */
    static QString nameFor(const QString &category, int urgency);

private:
    bool haveBackend() const;

    bool m_notifications = true;
    bool m_osd = false;
    QDateTime m_lastPlayed;
    int m_minGapMs = 400;
};
