/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    Live volume and brightness for the centre's slider widgets.

    Deliberately shells out to wpctl/pactl/brightnessctl rather than linking
    libpulse and libudev. Three reasons: those tools are what every wlroots
    user already has bound to their media keys, the values are only read while
    the centre is open (so polling cost is irrelevant), and it keeps the
    daemon's dependency list short enough to package everywhere.

    Values are 0..1 throughout. Unavailable backends report -1 so the widget
    can hide itself rather than showing a permanently-zero slider.
*/
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

class QProcess;

class SystemControls : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(qreal volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(bool volumeAvailable READ volumeAvailable NOTIFY changed)

    Q_PROPERTY(qreal brightness READ brightness NOTIFY changed)
    Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY changed)

public:
    explicit SystemControls(QObject *parent = nullptr);

    qreal volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool volumeAvailable() const { return m_volumeAvailable; }

    qreal brightness() const { return m_brightness; }
    bool brightnessAvailable() const { return m_brightnessAvailable; }

    Q_INVOKABLE void setVolume(qreal v);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setBrightness(qreal v);

    /* Called when the centre opens and stopped when it closes: there is no
       point polling wpctl every second for a panel nobody is looking at. */
    Q_INVOKABLE void setPolling(bool on);
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void changed();

private:
    QString run(const QString &cmd) const;
    void detect();
    void startSubscription();

    /* `pactl subscribe` streams a line whenever a sink or source changes, so
       the slider can follow a volume key instantly instead of waiting for the
       next poll tick. Polling stays as a fallback for the brightness path and
       for systems without pactl, but at a lazy interval — a poll fast enough
       to feel instant would mean spawning subprocesses several times a
       second for as long as the centre is open. */
    QProcess *m_subscription = nullptr;

    QTimer m_poll;
    QTimer m_debounce;
    QString m_volumeTool;      // "wpctl", "pactl" or empty
    QString m_brightnessTool;  // "brightnessctl" or empty
    qreal m_volume = 0;
    qreal m_brightness = 0;
    bool m_muted = false;
    bool m_volumeAvailable = false;
    bool m_brightnessAvailable = false;
};
