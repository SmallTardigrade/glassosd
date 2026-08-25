/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Window
import QtQuick.Effects
import org.glassosd.ui

/*
    Rounded, blurred background shared by every surface.

    The blur region is handed to KWin in *logical* pixels, matching what
    KWindowEffects::enableBlurBehind() documents. At this machine's 1.75 scale,
    passing device pixels is the classic way to end up with a blur that is
    offset and clipped against the panel it is meant to sit under.
*/
Rectangle {
    id: root

    /* Fall back to a near-opaque background when KWin's blur effect is off,
       rather than showing a washed-out translucent panel over the wallpaper. */
    /* Surfaces that carry reading material override this with cardBackground. */
    /* Apple's rule, and the one we had been breaking: Liquid Glass belongs to
       the navigation layer that floats *above* content, never to content
       itself — lists, cards and tables stay solid — and there should be only
       one primary glass sheet per view.

       We had four-plus glass cards stacked in one window. That is what made
       text behind them readable through the surface, wrecked icon contrast,
       and stopped it reading as glass at all. Cards are solid now; glass is
       reserved for floating sheets (the OSD, and the history panel itself). */
    property bool glass: false
    property color surfaceColor: Style.background

    /* Off by default: a shadow needs padding around the item to render into,
       so only surfaces whose window provides that room switch it on. */
    property bool shadow: false

    layer.enabled: shadow
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Style.shadowColor
        shadowBlur: Style.shadowBlur
        blurMax: Style.shadowBlurMax
        shadowOpacity: Style.shadowOpacity
        shadowVerticalOffset: Style.shadowYOffset
        autoPaddingEnabled: true
    }
    color: glass && Surface.blurAvailable ? surfaceColor : Style.solidSurface
    radius: Style.pill ? height / 2 : 16
    border.color: glass ? Style.glassEdge : Style.edgeOuter
    border.width: Style.edgeWidth
    antialiasing: true




    /* Blur only the panel itself. The surface animates by opacity alone, so
       this region is set once per resize rather than churned every frame of a
       slide — each call is a Wayland protocol round trip. */
    /* Specular sheen: light catches the upper face of a pane. Drawn as a
       gradient inside the rounded rect rather than as a separate strip — an
       earlier attempt anchored a 1px line with margins and it landed *below*
       the edge, reading as a stray rule across the surface. */
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        visible: true
        gradient: Gradient {
            GradientStop { position: 0.0;  color: root.glass ? Style.glassSheen : Style.cardSheen }
            GradientStop { position: 0.45; color: "transparent" }
        }
    }

    function refreshBlur() {
        if (!Window.window)
            return
        const p = mapToItem(null, 0, 0)
        const r = Qt.rect(p.x, p.y, width, height)

        /* The input region is registered for every panel, glass or not.
           It used to sit behind the `glass` guard, so a plain panel — a
           notification card, for one — registered nothing, the aggregate
           came out empty, and the mask fell back to the whole window. That
           made the transparent shadow padding swallow clicks meant for
           whatever was underneath. */
        Surface.setPanelRegion(Window.window, root, r, root.radius)

        /* Register rather than set: several panels share one window and a
           direct call would wipe the others' regions. */
        if (glass) {
            Surface.applyContrast(Window.window, r, root.radius,
                                  Style.bgContrast, Style.bgIntensity, Style.bgSaturation)
        }
    }

    /* Note: binding to mapToItem(null, 0, 0) does NOT work as a way to track
       absolute position — QML will not re-evaluate it when an ancestor moves.
       Whoever owns the moving item has to call refreshBlur(); see the
       onYChanged in NotificationCard. */
    Component.onCompleted: refreshBlur()
    onWidthChanged: refreshBlur()
    onHeightChanged: refreshBlur()
    onRadiusChanged: refreshBlur()
    Component.onDestruction: if (Window.window) Surface.clearPanelRegion(Window.window, root)
}
