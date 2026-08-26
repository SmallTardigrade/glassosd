/*
    SPDX-FileCopyrightText: 2014 Martin Klapetek <mklapetek@kde.org>
    SPDX-FileCopyrightText: 2024 Jakob Petsovits <jpetso@petsovits.com>
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "osdmodel.h"

#include <KLocalizedString>

namespace
{
/* Ported verbatim from Osd::volumeChanged(), plasma-workspace v6.7.4. */
QString volumeIcon(int percent)
{
    if (percent <= 25) {
        return QStringLiteral("volume-low");
    } else if (percent <= 75) {
        return QStringLiteral("volume-medium");
    }
    return QStringLiteral("volume-high");
}

/* Ported from Osd::microphoneVolumeChanged(). */
QString micIcon(int)
{
    return QStringLiteral("mic-on");
}

int intArg(const QVariantList &a, int i, int fallback = 0)
{
    return (a.size() > i && a.at(i).canConvert<int>()) ? a.at(i).toInt() : fallback;
}
bool boolArg(const QVariantList &a, int i)
{
    return a.size() > i && a.at(i).toBool();
}
QString strArg(const QVariantList &a, int i)
{
    return a.size() > i ? a.at(i).toString() : QString();
}
} // namespace

OsdModel::OsdModel(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(1800); // matches the native OSD's dwell time
    connect(&m_timer, &QTimer::timeout, this, &OsdModel::hide);
}

void OsdModel::onOsdCall(const QString &member, const QVariantList &args)
{
    if (member == QLatin1String("volumeChanged")) {
        const int percent = intArg(args, 0);
        const int max = args.size() > 1 ? intArg(args, 1, 100) : 100;
        if (percent <= 0) {
            showText(QStringLiteral("volume-muted"),
                     i18nc("OSD informing that the system is muted, keep short", "Audio Muted"));
        } else {
            showProgress(volumeIcon(percent), percent, max);
        }
    } else if (member == QLatin1String("microphoneVolumeChanged")) {
        const int percent = intArg(args, 0);
        if (percent <= 0) {
            showText(QStringLiteral("mic-muted"),
                     i18nc("OSD informing that the microphone is muted, keep short", "Microphone Muted"));
        } else {
            showProgress(micIcon(percent), percent, 100);
        }
    } else if (member == QLatin1String("mediaPlayerVolumeChanged")) {
        const int percent = intArg(args, 0);
        const QString player = strArg(args, 1);
        const QString playerIcon = strArg(args, 2);
        if (percent == 0) {
            showText(QStringLiteral("media"), i18nc("OSD informing that some media app is muted", "%1 Muted", player));
        } else {
            showProgress(QStringLiteral("media"), percent, 100, player);
        }
    } else if (member == QLatin1String("brightnessChanged")) {
        showProgress(QStringLiteral("brightness"), intArg(args, 0), 100);
    } else if (member == QLatin1String("screenBrightnessChanged")) {
        /* (percent, displayId, displayLabel, priority, QRect) — we show the
           single-display case; multi-monitor is deferred to Phase 5. */
        showProgress(QStringLiteral("brightness"), intArg(args, 0), 100);
    } else if (member == QLatin1String("keyboardBrightnessChanged")) {
        showProgress(QStringLiteral("keyboard-brightness"), intArg(args, 0), 100);
    } else if (member == QLatin1String("kbdLayoutChanged")) {
        showText(QStringLiteral("keyboard-layout"), strArg(args, 0));
    } else if (member == QLatin1String("virtualDesktopChanged")) {
        showText(QString(), strArg(args, 0));
    } else if (member == QLatin1String("touchpadEnabledChanged")) {
        const bool on = boolArg(args, 0);
        showText(on ? QStringLiteral("input-touchpad-on") : QStringLiteral("input-touchpad-off"),
                 on ? i18nc("touchpad was enabled, keep short", "Touchpad On")
                    : i18nc("touchpad was disabled, keep short", "Touchpad Off"));
    } else if (member == QLatin1String("wifiEnabledChanged")) {
        const bool on = boolArg(args, 0);
        showText(on ? QStringLiteral("network-wireless-on") : QStringLiteral("network-wireless-off"),
                 on ? i18nc("wireless lan was enabled, keep short", "Wifi On")
                    : i18nc("wireless lan was disabled, keep short", "Wifi Off"));
    } else if (member == QLatin1String("bluetoothEnabledChanged")) {
        const bool on = boolArg(args, 0);
        showText(on ? QStringLiteral("preferences-system-bluetooth")
                    : QStringLiteral("preferences-system-bluetooth-inactive"),
                 on ? i18nc("Bluetooth was enabled, keep short", "Bluetooth On")
                    : i18nc("Bluetooth was disabled, keep short", "Bluetooth Off"));
    } else if (member == QLatin1String("wwanEnabledChanged")) {
        const bool on = boolArg(args, 0);
        showText(on ? QStringLiteral("network-mobile-on") : QStringLiteral("network-mobile-off"),
                 on ? i18nc("mobile internet was enabled, keep short", "Mobile Internet On")
                    : i18nc("mobile internet was disabled, keep short", "Mobile Internet Off"));
    } else if (member == QLatin1String("virtualKeyboardEnabledChanged")) {
        const bool on = boolArg(args, 0);
        showText(on ? QStringLiteral("input-keyboard-virtual-on") : QStringLiteral("input-keyboard-virtual-off"),
                 on ? i18nc("on screen keyboard was enabled, keep short", "On-Screen Keyboard Activated")
                    : i18nc("on screen keyboard was disabled, keep short", "On-Screen Keyboard Deactivated"));
    } else if (member == QLatin1String("powerManagementInhibitedChanged")) {
        const bool inhibited = boolArg(args, 0);
        showText(inhibited ? QStringLiteral("system-suspend-inhibited") : QStringLiteral("system-suspend-uninhibited"),
                 inhibited ? i18nc("power management was inhibited, keep short", "Sleep and Screen Locking Blocked")
                           : i18nc("power management was uninhibited, keep short", "Sleep and Screen Locking Unblocked"));
    } else if (member == QLatin1String("powerProfileChanged")) {
        const QString profile = strArg(args, 0);
        if (profile == QLatin1String("power-saver")) {
            showText(QStringLiteral("battery-profile-powersave"),
                     i18nc("Power profile changed to power save, keep short", "Power Save Mode"));
        } else if (profile == QLatin1String("balanced")) {
            showText(QStringLiteral("speedometer"),
                     i18nc("Power profile changed to balanced, keep short", "Balanced Power Mode"));
        } else if (profile == QLatin1String("performance")) {
            showText(QStringLiteral("battery-profile-performance"),
                     i18nc("Power profile changed to performance, keep short", "Performance Mode"));
        }
    } else if (member == QLatin1String("showText")) {
        showText(strArg(args, 0), strArg(args, 1));
    } else if (member == QLatin1String("hide")) {
        hide();
    }
}

void OsdModel::onLockChanged(int key, bool locked)
{
    Q_EMIT soundWanted(QStringLiteral("bell"));

    if (key == Qt::Key_CapsLock) {
        showText(QStringLiteral("lock-caps"),
                 locked ? i18nc("keep short", "Caps Lock On") : i18nc("keep short", "Caps Lock Off"),
                 !locked,
                 locked);
    } else if (key == Qt::Key_NumLock) {
        showText(QStringLiteral("lock-num"),
                 locked ? i18nc("keep short", "Num Lock On") : i18nc("keep short", "Num Lock Off"),
                 !locked,
                 locked);
    }
}

void OsdModel::onFnLockChanged(bool locked)
{
    showText(QStringLiteral("lock-fn"),
             locked ? i18nc("keep short", "Fn Lock On") : i18nc("keep short", "Fn Lock Off"),
             !locked,
             locked);
}

void OsdModel::showProgress(const QString &icon, int value, int maxValue, const QString &extra)
{
    /* Volume only. Brightness has no standard sound and macOS does not make
       one either; a click on every brightness step would be noise. */
    if (icon.contains(QLatin1String("volume")) || icon.contains(QLatin1String("audio"))) {
        Q_EMIT soundWanted(QStringLiteral("audio-volume-change"));
    }

    m_icon = icon;
    m_value = qBound(0, value, maxValue);
    m_maxValue = maxValue;
    m_text = extra;
    m_showingProgress = true;
    m_iconDimmed = false;
    m_iconAccent = false;
    bump();
}

void OsdModel::showText(const QString &icon, const QString &text, bool dimIcon, bool accentIcon)
{
    m_icon = icon;
    m_text = text;
    m_showingProgress = false;
    m_iconDimmed = dimIcon;
    m_iconAccent = accentIcon;
    bump();
}

void OsdModel::bump()
{
    m_active = true;
    Q_EMIT changed();
    m_timer.start();
}

void OsdModel::hide()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    m_timer.stop();
    Q_EMIT changed();
}
