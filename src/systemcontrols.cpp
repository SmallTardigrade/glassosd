/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "systemcontrols.h"

#include <QDebug>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

SystemControls::SystemControls(QObject *parent)
    : QObject(parent)
{
    detect();

    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &SystemControls::refresh);
    refresh();
}

void SystemControls::detect()
{
    /* wpctl first: on a PipeWire system pactl is a compatibility shim, and
       asking the shim for a value the native tool owns is a needless hop. */
    for (const QString &t : {QStringLiteral("wpctl"), QStringLiteral("pactl")}) {
        if (!QStandardPaths::findExecutable(t).isEmpty()) {
            m_volumeTool = t;
            break;
        }
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("brightnessctl")).isEmpty()) {
        m_brightnessTool = QStringLiteral("brightnessctl");
    }

    if (m_volumeTool.isEmpty()) {
        qInfo("glassosd: no wpctl or pactl — the volume widget will stay hidden");
    }
    if (m_brightnessTool.isEmpty()) {
        qInfo("glassosd: no brightnessctl — the brightness widget will stay hidden");
    }
}

QString SystemControls::run(const QString &cmd) const
{
    QProcess p;
    p.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), cmd});
    /* Short: these are local queries. A hung audio server must not freeze the
       UI thread, and a stale reading for one tick is harmless. */
    if (!p.waitForFinished(400)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void SystemControls::setPolling(bool on)
{
    if (on) {
        refresh();
        m_poll.start();
    } else {
        m_poll.stop();
    }
}

void SystemControls::refresh()
{
    const qreal oldVol = m_volume;
    const qreal oldBri = m_brightness;
    const bool oldMuted = m_muted;
    const bool oldVolAvail = m_volumeAvailable;
    const bool oldBriAvail = m_brightnessAvailable;

    if (m_volumeTool == QLatin1String("wpctl")) {
        // "Volume: 0.35" or "Volume: 0.35 [MUTED]"
        const QString out = run(QStringLiteral("wpctl get-volume @DEFAULT_AUDIO_SINK@"));
        static const QRegularExpression re(QStringLiteral("Volume:\\s*([0-9.]+)"));
        const auto m = re.match(out);
        if (m.hasMatch()) {
            m_volume = qBound(0.0, m.captured(1).toDouble(), 1.0);
            m_muted = out.contains(QLatin1String("MUTED"));
            m_volumeAvailable = true;
        } else {
            m_volumeAvailable = false;
        }
    } else if (m_volumeTool == QLatin1String("pactl")) {
        const QString out = run(QStringLiteral("pactl get-sink-volume @DEFAULT_SINK@"));
        static const QRegularExpression re(QStringLiteral("(\\d+)%"));
        const auto m = re.match(out);
        if (m.hasMatch()) {
            m_volume = qBound(0.0, m.captured(1).toDouble() / 100.0, 1.0);
            m_muted = run(QStringLiteral("pactl get-sink-mute @DEFAULT_SINK@"))
                          .contains(QLatin1String("yes"));
            m_volumeAvailable = true;
        } else {
            m_volumeAvailable = false;
        }
    }

    if (!m_brightnessTool.isEmpty()) {
        const QString cur = run(QStringLiteral("brightnessctl -m g"));
        const QString max = run(QStringLiteral("brightnessctl -m m"));
        bool okc = false, okm = false;
        const double c = cur.toDouble(&okc);
        const double mx = max.toDouble(&okm);
        if (okc && okm && mx > 0) {
            m_brightness = qBound(0.0, c / mx, 1.0);
            m_brightnessAvailable = true;
        } else {
            m_brightnessAvailable = false;
        }
    }

    if (!qFuzzyCompare(oldVol + 1, m_volume + 1) || !qFuzzyCompare(oldBri + 1, m_brightness + 1)
        || oldMuted != m_muted || oldVolAvail != m_volumeAvailable
        || oldBriAvail != m_brightnessAvailable) {
        Q_EMIT changed();
    }
}

void SystemControls::setVolume(qreal v)
{
    if (m_volumeTool.isEmpty()) {
        return;
    }
    const int pct = qBound(0, int(v * 100 + 0.5), 100);
    if (m_volumeTool == QLatin1String("wpctl")) {
        run(QStringLiteral("wpctl set-volume @DEFAULT_AUDIO_SINK@ %1%").arg(pct));
    } else {
        run(QStringLiteral("pactl set-sink-volume @DEFAULT_SINK@ %1%").arg(pct));
    }
    m_volume = pct / 100.0;
    Q_EMIT changed();
}

void SystemControls::toggleMute()
{
    if (m_volumeTool.isEmpty()) {
        return;
    }
    if (m_volumeTool == QLatin1String("wpctl")) {
        run(QStringLiteral("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"));
    } else {
        run(QStringLiteral("pactl set-sink-mute @DEFAULT_SINK@ toggle"));
    }
    refresh();
}

void SystemControls::setBrightness(qreal v)
{
    if (m_brightnessTool.isEmpty()) {
        return;
    }
    /* Never let a slider drag take the screen to literal zero — on most
       panels that is indistinguishable from the machine having died. */
    const int pct = qBound(1, int(v * 100 + 0.5), 100);
    run(QStringLiteral("brightnessctl -q set %1%").arg(pct));
    m_brightness = pct / 100.0;
    Q_EMIT changed();
}
