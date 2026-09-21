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

/* Turns a run of rapid events into at most two sounds: one as it starts, one
   after it stops.

   A held volume key repeats every 40ms or so, and the volume-change sound in
   a typical theme lasts 300 — Ocean's does. Sounding on every step starts each
   copy before the last has finished, and what comes out is a stutter of
   overlapping, clipped clicks rather than feedback. Rate-limiting alone only
   thins the stutter.

   The two sounds that carry information are the first, which says the key
   did something, and the last, which is heard at the level you settled on.
   Everything in between says nothing the OSD bar is not already showing.

   Pure and clock-free — the caller passes the time in — so it can be tested
   without waiting on real time. */
class BurstGate
{
public:
    explicit BurstGate(int quietMs = 250) : m_quietMs(quietMs) {}

    /* An event at nowMs. True when it should sound now: it is the first of a
       new run, because nothing arrived in the previous quietMs. */
    bool event(qint64 nowMs);
    /* Call once quietMs has passed since the last event. True when the run
       that just ended swallowed events, meaning the level you ended on has not
       been heard yet. A single tap already sounded, so it owes nothing. */
    bool finish(qint64 nowMs);
    /* How long until the current run counts as ended; 0 when it already has
       or none is active. The caller's timer may fire early — Qt's default
       coarse timers are allowed 5% slack, which on 250ms is enough to land
       inside the window — and must wait out the remainder rather than
       concluding nothing is owed. */
    qint64 remainingMs(qint64 nowMs) const;

    int quietMs() const { return m_quietMs; }

private:
    int m_quietMs;
    qint64 m_last = 0;
    bool m_active = false;
    bool m_swallowed = false;
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
    /* Cut off the previous volume sound before the next one starts, so the
       first and last of a run can never overlap. */
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
