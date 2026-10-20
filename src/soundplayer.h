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
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

/* Turns a run of rapid events into one sound, once the run is over.

   A held volume key repeats every 40ms or so, and a three-finger swipe sends
   a step every few pixels of travel, while the volume-change sound in a
   typical theme lasts 300ms — Ocean's does. Sounding on every step starts
   each copy before the last has finished, and what comes out is a stutter of
   overlapping, clipped clicks rather than feedback.

   The one sound that carries information is the last, heard at the level you
   settled on; the OSD bar already shows every step on the way. This used to
   sound at the start of a run as well, so that a press was acknowledged at
   once, but that made each swipe two sounds and a few swipes in a row a
   chatter of them.

   Pure and clock-free — the caller passes the time in — so it can be tested
   without waiting on real time. */
class BurstGate
{
public:
    /* Long enough that a slow stretch of a swipe stays one run (300ms
       between steps, measured); short enough that the sound does not lag
       behind your fingers. 500 was noticeably late. */
    explicit BurstGate(int quietMs = 350) : m_quietMs(quietMs) {}

    /* A step at nowMs. Starts a run, or extends the current one. */
    void event(qint64 nowMs);
    /* Call once quietMs has passed since the last event. True once per run,
       when it has ended: the moment to sound. */
    bool finish(qint64 nowMs);
    /* How long until the current run counts as ended; 0 when it already has
       or none is active. The caller's timer may fire early — Qt's default
       coarse timers are allowed 5% slack — and must wait out the remainder
       rather than concluding nothing is owed. */
    qint64 remainingMs(qint64 nowMs) const;

    int quietMs() const { return m_quietMs; }

private:
    int m_quietMs;
    qint64 m_last = 0;
    bool m_active = false;
};

class SoundPlayer : public QObject
{
    Q_OBJECT
public:
    explicit SoundPlayer(QObject *parent = nullptr);

    /* name is a sound-theme name such as "message-new-instant", or an
       absolute or ~/ path. Returns the player's pid, 0 when nothing played. */
    qint64 play(const QString &name);

    void setNotificationsEnabled(bool on) { m_notifications = on; }
    void setOsdEnabled(bool on) { m_osd = on; }
    /* The volume-change sound: a theme name or a path. Empty means the
       theme's audio-volume-change. */
    void setOsdSound(const QString &nameOrPath) { m_osdSound = nameOrPath; }
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
    void playVolumeChange();
    /* Cut off the previous volume sound before the next one starts, so two
       adjustments close together can never overlap. */
    void stopPreviousVolumeChange();

    bool m_notifications = true;
    bool m_osd = false;
    QDateTime m_lastPlayed;
    int m_minGapMs = 400;

    QString m_osdSound;
    BurstGate m_volumeGate;
    QTimer m_volumeTrailing;
    QElapsedTimer m_clock;
    qint64 m_volumePid = 0;
};
