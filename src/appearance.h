/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Appearance settings, exposed to QML and driven from glassosdrc.

    Style.qml reads everything from here rather than hardcoding values, so the
    light/dark palette, the accent colour and the level-indicator style are all
    changeable from the command line without a rebuild.
*/
#pragma once

#include <KSharedConfig>
#include <QColor>
#include <QObject>
#include <QStringList>
#include <QQmlEngine>

class Appearance : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    /* "segmented" (discrete blocks, macOS-classic) or "bar" (continuous). */
    Q_PROPERTY(QString levelStyle READ levelStyle NOTIFY changed)
    /* "current" (follow the compositor), "primary", or an output name. */
    Q_PROPERTY(QString output READ output NOTIFY changed)
    /* Multiplies every padding, icon and font size. Qt already handles the
       display's devicePixelRatio, so this is a *taste* control on top of that
       — "I want bigger text" rather than "this screen is HiDPI". */
    Q_PROPERTY(qreal scale READ scale NOTIFY changed)
    Q_PROPERTY(int notifyWidth READ notifyWidth NOTIFY changed)
    /* How far every glass surface is pushed toward opaque, 0..1. The tuned
       alphas assume the compositor is blurring behind us; sway, river and
       friends do not blur at all, and unblurred translucency over a busy
       wallpaper is unreadable. 0 keeps the glass, 1 makes it solid. */
    Q_PROPERTY(qreal solidity READ solidity NOTIFY changed)
    /* wlr-layer-shell layer for notification popups: 2 = Top, 3 = Overlay.
       There is no value between them — fullscreen surfaces sit in the gap —
       so this is an either/or the user has to pick. */
    Q_PROPERTY(int notifyLayer READ notifyLayer NOTIFY changed)
    /* How many lines of body text a popup shows before eliding, and the same
       for an entry in the centre. Separate values because a popup is glanced
       at and the centre is read. */
    Q_PROPERTY(int bodyLines READ bodyLines NOTIFY changed)
    Q_PROPERTY(int centreBodyLines READ centreBodyLines NOTIFY changed)
    /* Point size, overriding the value derived from Scale. Set it when you
       want bigger text without bigger padding, which is what Scale does. */
    Q_PROPERTY(int fontSize READ fontSize NOTIFY changed)
    /* Which widgets the centre shows and in what order — swaync's `widgets`
       list, and the same names, so a config written for swaync reads the same
       here. Known names: title, mpris, volume, dnd, notifications, backlight,
       buttons-grid.

       A list that does not name `notifications` is read as a plain set in the
       built-in order instead, because that is what this key used to mean and
       obeying such a list literally would give a notification centre with no
       notifications in it. Appearance::reload() has the detail. */
    Q_PROPERTY(QStringList widgets READ widgets NOTIFY changed)

public:
    explicit Appearance(QObject *parent = nullptr);

    bool dark() const { return m_dark; }
    QColor accent() const { return m_accent; }
    QString levelStyle() const { return m_levelStyle; }
    QString output() const { return m_output; }
    qreal scale() const { return m_scale; }
    int notifyWidth() const { return m_notifyWidth; }
    qreal solidity() const { return m_solidity; }
    int notifyLayer() const { return m_notifyLayer; }
    int bodyLines() const { return m_bodyLines; }
    int centreBodyLines() const { return m_centreBodyLines; }
    int fontSize() const { return m_fontSize; }
    QStringList widgets() const { return m_widgets; }
    Q_INVOKABLE bool hasWidget(const QString &name) const { return m_widgets.contains(name); }
    Q_INVOKABLE int widgetOrder(const QString &name) const { return m_widgets.indexOf(name); }

    void reload();

Q_SIGNALS:
    void changed();

private:
    KSharedConfig::Ptr m_config;
    bool m_dark = true;
    QColor m_accent = QColor(QStringLiteral("#3daee9"));
    QString m_levelStyle = QStringLiteral("segmented");
    QString m_output = QStringLiteral("current");
    qreal m_scale = 1.0;
    int m_notifyWidth = 380;
    qreal m_solidity = 0.0;
    int m_notifyLayer = 2;
    int m_bodyLines = 4;
    int m_centreBodyLines = 3;
    int m_fontSize = 0;   // 0 = derive from Scale
    QStringList m_widgets;
};
