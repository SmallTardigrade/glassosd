/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Layer-shell placement and KWin blur for our QQuickWindows.

    Exposed to QML as a singleton so geometry stays in QML, where it belongs,
    while the protocol calls stay in C++.
*/
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QHash>
#include <QSet>
#include <QRectF>
#include <QStringList>

class Surface : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /* False when KWin's blur effect is switched off, in which case QML should
       fall back to a more opaque background rather than showing a flat, washed
       out panel over the wallpaper. */
    Q_PROPERTY(bool blurAvailable READ blurAvailable CONSTANT)

public:
    explicit Surface(QObject *parent = nullptr);

    bool blurAvailable() const;

    /* Must be called before the window is first shown. anchors uses the same
       bit values as LayerShellQt::Window::Anchor (1 top, 2 bottom, 4 left, 8 right).

       exclusiveZone: -1 floats over everything including panels (right for a
       centred OSD); 0 requests no zone of our own but respects other
       surfaces', so the compositor places us clear of the panel (right for
       notifications, and robust across whatever panel layout is configured).

       layer: matches LayerShellQt::Window::Layer. Top (2) sits above ordinary
       windows but *below* overlay surfaces, which is what screenshot region
       selectors use. On Overlay (3) our notifications drew on top of the
       selector and ended up inside the screenshot. */
    Q_INVOKABLE void initLayerShell(QQuickWindow *window,
                                    const QString &scope,
                                    int anchors,
                                    int marginTop,
                                    int marginRight,
                                    int marginBottom,
                                    int marginLeft,
                                    bool keyboardFocus = false,
                                    int exclusiveZone = -1,
                                    int layer = 2);

    /* Move an already-mapped surface. initLayerShell must run before the
       window is first shown, so it cannot answer a position change made while
       the daemon is running; this can, and takes the same anchor bits. */
    Q_INVOKABLE void setPosition(QQuickWindow *window,
                                 int anchors,
                                 int marginTop = 0,
                                 int marginRight = 0,
                                 int marginBottom = 0,
                                 int marginLeft = 0);

    /* Legacy single-panel helper, kept for surfaces that own their window
       outright. rect is in logical pixels, which is what
       KWindowEffects::enableBlurBehind() expects. Passing device pixels is
       the classic way to get a blur offset and clipped at 1.75x scaling, and
       passing the whole window would blur the transparent slide-in margin. */
    /* Region contributions are aggregated per window.

       KWindowEffects::enableBlurBehind() *sets* the blur region for a whole
       window rather than adding to it, so when several panels share one
       window — as every notification card does — each call silently wiped the
       previous one and only the last panel was ever blurred. Everything else
       showed sharp, unblurred backdrop through its alpha, which reads as cheap
       transparency rather than glass. Panels now register their own rect and
       the union is applied once. */
    Q_INVOKABLE void setPanelRegion(QQuickWindow *window, QObject *panel,
                                    const QRectF &rect, qreal radius);
    Q_INVOKABLE void clearPanelRegion(QQuickWindow *window, QObject *panel);

    /* Restrict input to the registered panel rects.

       A layer surface receives clicks across its whole extent, and the
       notification window is deliberately much larger than its cards so drop
       shadows have room. Without this, the ~42px transparent margin around the
       stack silently swallows clicks that should reach the window underneath,
       and the gaps between cards do the same.

       Not used by the history panel, whose full-screen area is the
       click-away catcher and must stay clickable. */
    Q_INVOKABLE void setInputFollowsPanels(QQuickWindow *window, bool on);

    /* Keyboard focus must be off by default. OnDemand lets a layer surface
       take focus, so a notification appearing steals the next click from
       whatever the user was working in. It is only switched on while a
       notification with a reply field is actually on screen. */
    Q_INVOKABLE void setKeyboardFocus(QQuickWindow *window, bool on);

    /* Which output a surface appears on. dunst has follow/monitor and swaync
       has preferred-output; with only one screen this is a no-op, but it has
       to exist before the external display is plugged in.

       "current" tracks the screen holding the pointer, "primary" pins to the
       primary output, or an exact output name such as "DP-1". */
    Q_INVOKABLE void setOutput(QQuickWindow *window, const QString &preference);
    Q_INVOKABLE QStringList outputs() const;
    Q_INVOKABLE void clearBlur(QQuickWindow *window);

    /* Blur alone over a dark desktop just looks like a dark panel — there is
       nothing interesting to blur. What actually reads as "glass" is the
       compositor also lifting saturation and brightness behind the surface,
       which is what Plasma's own panels do. */
    Q_INVOKABLE void applyContrast(QQuickWindow *window,
                                   const QRectF &rect,
                                   qreal radius,
                                   qreal contrast,
                                   qreal intensity,
                                   qreal saturation);
    Q_INVOKABLE void clearContrast(QQuickWindow *window);

private:
    void recompute(QQuickWindow *window);

    struct Contribution {
        QRectF rect;
        qreal radius = 0;
    };
    QHash<QQuickWindow *, QHash<QObject *, Contribution>> m_regions;
    QSet<QQuickWindow *> m_maskedWindows;
    QHash<QQuickWindow *, QRegion> m_lastMask;

    qreal m_contrast = 0.32;
    qreal m_intensity = 0.92;
    qreal m_saturation = 1.35;
};
