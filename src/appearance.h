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
    /* Which widgets the centre shows — swaync's `widgets` list, and the
       same names, so a config written for swaync reads the same here.

       Note this is a *set*, not an order: the centre lays its widgets out in
       a fixed sequence and this decides which of them appear. swaync treats
       the list as an order too. Matching that means every widget moving into
       its own Component behind a Repeater, which is a real refactor of
       HistoryPanel rather than a config change. */
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
    QStringList m_widgets;
};
