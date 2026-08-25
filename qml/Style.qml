/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
pragma Singleton
import QtQuick

/*
    The single source of visual truth. Every surface imports this, which is the
    entire reason for building one daemon instead of matching hex codes across
    four unrelated config formats.
*/
QtObject {
    /* One multiplier applied to every geometry and font value below, so a
       size change is a single config key rather than a dozen. Qt already
       handles the display's devicePixelRatio; this is taste on top of that. */
    readonly property real s: Appearance.scale
    function px(v) { return Math.round(v * s) }

    /* Every alpha below is tuned on the assumption that something is blurring
       behind us. sway, river and labwc do not blur at all, and translucency
       without blur over a photo wallpaper is simply unreadable. `Solidity`
       lerps each glass surface toward opaque; 0 keeps the tuned glass. */
    function glass(c) {
        /* With no compositor blur, translucency is just see-through, and text
           behind the surface reads straight through it. Rather than ship an
           illegible surface, close most of the gap automatically and leave
           Solidity as the manual override on top. */
        const floor = Surface.blurAvailable ? 0.0 : 0.90
        const s = Math.max(floor, Appearance.solidity)
        return Qt.rgba(c.r, c.g, c.b, c.a + (1.0 - c.a) * s)
    }

    // ---- Geometry ------------------------------------------------------
    readonly property int padding: Math.round(Theme.num("spacing.padding", px(12)))
    /* Fully-rounded pills curve inward at the ends, so uniform padding leaves
       content crowding the edge even though the numbers match. */
    readonly property int pillPaddingH: Math.round(Theme.num("spacing.pillPaddingH", px(20)))
    readonly property int spacing: Math.round(Theme.num("spacing.gap", px(13)))
    readonly property int iconSize: Math.round(Theme.num("size.icon", px(21)))
    readonly property int chipSize: iconSize + px(17)

    /* One shape language for "icon in a container". The OSD used a circle and
       notification icons had no container at all, which read as two different
       systems. Both now use the same squircle proportion. */
    readonly property real chipRadiusRatio: Theme.num("radius.chipRatio", 0.32)
    /* The wide minimum exists to give the progress bar room. Text messages
       have no bar, so holding them to the same width just adds dead space. */
    readonly property int minWidth: Math.round(Theme.num("size.osdMinWidth", px(268)))
    readonly property int minWidthText: Math.round(Theme.num("size.osdMinWidthText", px(150)))
    readonly property int barHeight: Math.round(Theme.num("size.barHeight", px(6)))
    readonly property int segmentCount: Math.round(Theme.num("level.segments", 16))
    readonly property int segmentGap: Math.round(Theme.num("level.segmentGap", 3))
    readonly property int segmentHeight: Math.round(Theme.num("level.segmentHeight", px(14)))

    // ---- Notifications ---------------------------------------------------
    readonly property int notifyWidth: Math.round(Appearance.notifyWidth * s)
    /* Breathing room inside the surface so drop shadows have somewhere to
       render instead of being clipped at the window edge. */
    readonly property int shadowPad: Math.round(Theme.num("shadow.pad", 42))
    readonly property int notifyMargin: Math.round(Theme.num("spacing.screenMargin", 10))
    readonly property int notifySpacing: Math.round(Theme.num("spacing.cardGap", 8))
    /* Large enough that detailed app icons stay readable. 21px was fine for
       flat glyphs but turned busy ones (an arrow inside a circle, say) into
       mush. Rendered at 3x into the texture so they stay crisp at 1.75 scale. */
    readonly property int notifyIconSize: Math.round(Theme.num("size.notifyIcon", px(30)))

    /* Peeking card edges for a grouped stack. */
    readonly property int stackOffset: Math.round(Theme.num("stack.offset", 9))
    readonly property int stackInset: Math.round(Theme.num("stack.inset", 8))
    /* Each peeking edge is lighter than the card in front so the layers read
       as separate sheets; matching the card colour made the stack invisible.
       A dark seam along the top of each edge is what separates one sheet from
       the next — without it the lighter tones merge into a single soft blob. */
    readonly property color cardStackEdge: Theme.color("card.stackEdge", dark ? Qt.rgba(0x2a/255, 0x2f/255, 0x36/255, 1.0)
                                               : Qt.rgba(0xe6/255, 0xe8/255, 0xed/255, 1.0))
    readonly property color cardStackSeam: Theme.color("card.stackSeam", dark ? Qt.rgba(0,0,0,0.55) : Qt.rgba(0,0,0,0.18))
    readonly property int cardRadius: Math.round(Theme.num("radius.card", px(16)))

    /* Notifications need more opacity than the OSD. An OSD is glanced at for
       a second; a notification or history entry is *read*.

       0.95 was measured as genuinely working, but 5% of pure white backdrop
       text over a #16191d panel still lifts it from 25 to ~38 — a ~50%
       relative brightness change, which reads as ghosting behind body copy.
       0.975 halves that while keeping the surface visibly glassy. */
    readonly property color cardBackground: glass(Theme.color("card.background", dark ? Qt.rgba(0x16/255, 0x19/255, 0x1d/255, 0.975)
                                                     : Qt.rgba(0xfc/255, 0xfc/255, 0xfd/255, 0.985)))

    /* The single glass sheet in the history view; its rows sit on top as
       plain content and carry no glass of their own. */
    /* 0.82 was glassy but group headers sit directly on this sheet, and
       high-contrast backdrop text read straight through them. Legibility of
       content on the glass wins over how glassy the glass is. */
    readonly property color panelGlass: glass(Theme.color("panel.background", dark ? Qt.rgba(0x14/255, 0x17/255, 0x1b/255, 0.93)
                                                 : Qt.rgba(0xf4/255, 0xf5/255, 0xf8/255, 0.92)))

    readonly property int historyWidth: Math.round(Theme.num("size.centreWidth", px(400)))
    readonly property bool pill: Theme.flag("radius.pill", true)                 // radius = height/2

    /* Distance from the top screen edge. */
    readonly property int osdTopMargin: Math.round(Theme.num("spacing.osdTopMargin", px(118)))


    // ---- Palette ---------------------------------------------------------
    /* Every surface colour is derived from Appearance.dark, so the whole
       system flips with one config key rather than needing a second theme
       file to be kept in sync. Light values follow the Apple/Windows
       convention: near-white surfaces, dark text, and a *dark* scrim behind
       glass rather than a light one. */
    readonly property bool dark: Appearance.dark

    // ---- Surface -------------------------------------------------------
    /* 0.55 read as washed out and 0.78 was still too see-through. The
       backdrop should be sensed behind the surface, not read through it. */
    /* Apple's OSDs are near-solid frosted panels, not windows onto the
       desktop — the blur reads as a material rather than as see-through. 0.88
       still let text underneath show, which is exactly what makes it feel
       cheap rather than clean. */
    /* Genuinely translucent again, but now paired with 180% saturation and a
       proper glass edge. The earlier 0.88 looked washed out because the
       backdrop came through grey; the fix was chroma, not opacity. */
    readonly property color background: glass(Theme.color("osd.background", dark ? Qt.rgba(0x16/255, 0x19/255, 0x1d/255, 0.84)
                                                 : Qt.rgba(0xf7/255, 0xf8/255, 0xfa/255, 0.80)))
    /* Content surfaces: fully opaque, because they are read. */
    /* Rows inside the glass sheet are solid, not a translucent wash. A wash
       lets the desktop behind the *panel* bleed through the content sitting on
       it, which is how a terminal ended up legible through a notification. */
    readonly property color entryCard: Theme.color("entry.background", dark ? Qt.rgba(0x28/255, 0x2e/255, 0x36/255, 1.0)
                                           : Qt.rgba(0xff/255, 0xff/255, 0xff/255, 1.0))
    readonly property color entryCardHover: Theme.color("entry.backgroundHover", dark ? Qt.rgba(0x33/255, 0x3a/255, 0x43/255, 1.0)
                                                : Qt.rgba(0xed/255, 0xef/255, 0xf3/255, 1.0))
    /* A hairline as well as a lighter fill: adjacent cards of the same tone
       run together without an edge to separate them. */
    readonly property color entryCardEdge: Theme.color("entry.edge", dark ? Qt.rgba(1,1,1,0.07) : Qt.rgba(0,0,0,0.09))

    readonly property color solidSurface: Theme.color("surface.solid", dark ? Qt.rgba(0x17/255, 0x1a/255, 0x1f/255, 1.0)
                                              : Qt.rgba(0xfd/255, 0xfd/255, 0xfe/255, 1.0))
    readonly property color backgroundOpaque: Qt.rgba(0x23 / 255, 0x26 / 255, 0x29 / 255, 0.97)

    /* Two-tone edge. Every reference implementation of this look — Apple's
       Liquid Glass, Windows 11 Acrylic — traces the glass edge with a light
       hairline; a single flat border reads as a plain translucent rectangle. */
    /* One whisper-thin edge, not two concentric strokes. At OSD size a
       two-tone edge sells the glass; at card size it reads as a drawn outline
       and the stack stops feeling like one surface. */
    /* The lit edge of a pane of glass. Only glass surfaces get this; solid
       content cards keep the near-invisible edgeOuter, since a bright outline
       on a card is what read as "unseamless". */
    readonly property color glassEdge: Theme.color("glass.edge", dark ? Qt.rgba(1,1,1,0.22) : Qt.rgba(1,1,1,0.55))
    /* Even a solid card should look like a surface catching light rather than
       a flat rectangle; this is the cheapest cue that reads as "material". */
    readonly property color cardSheen: Theme.color("card.sheen", dark ? Qt.rgba(1,1,1,0.028) : Qt.rgba(1,1,1,0.5))

    readonly property color glassSheen: Theme.color("glass.sheen", dark ? Qt.rgba(1,1,1,0.07) : Qt.rgba(1,1,1,0.55))

    readonly property color edgeOuter: Theme.color("edge.outer", dark ? Qt.rgba(1,1,1,0.09) : Qt.rgba(0,0,0,0.10))
    readonly property color edgeInner: Theme.color("edge.inner", Qt.rgba(1, 1, 1, 0.05))
    readonly property int edgeWidth: Math.round(Theme.num("edge.width", 1))

    // ---- KWin background-contrast effect --------------------------------
    readonly property real bgContrast: Theme.num("blur.contrast", 0.32)
    readonly property real bgIntensity: Theme.num("blur.intensity", dark ? 0.92 : 1.12)
    /* 1.8 == the documented saturate(180%). At 1.35 the backdrop came through
       desaturated, which reads as cloudy rather than as glass — the chromatic
       depth is what sells the material. */
    readonly property real bgSaturation: Theme.num("blur.saturation", 1.80)

    // ---- Content ---------------------------------------------------------
    readonly property color foreground: Theme.color("text.primary", dark ? "#f4f6f8" : "#16181c")
    /* Between foreground and foregroundDim: for the app identity line, which
       should carry weight without competing with the message itself. */
    readonly property color foregroundMuted: Theme.color("text.muted", dark ? Qt.rgba(0xf4/255,0xf6/255,0xf8/255,0.88) : Qt.rgba(0x16/255,0x18/255,0x1c/255,0.78))

    /* 0.55 measured at 3.94 against the light card — under the 4.5 AA body
       threshold. 0.68 clears it while still reading as secondary text. */
    readonly property color foregroundDim: Theme.color("text.secondary", dark ? Qt.rgba(0xf4/255,0xf6/255,0xf8/255,0.60) : Qt.rgba(0x16/255,0x18/255,0x1c/255,0.68))
    readonly property color accent: Theme.color("accent", Appearance.accent)
    readonly property color critical: Theme.color("urgency.critical", "#da4453")

    /* An "on" lock has to be unmistakable at a glance, so the chip fills with
       near-solid accent and gains a halo rather than merely tinting. */
    /* Buttons and toggles sit *above* the surface they are on, so they carry
       more lightness and their own hairline. Previously everything shared one
       flat value and the whole UI sat on a single plane. */
    readonly property color controlFill: Theme.color("control.fill", dark ? Qt.rgba(1,1,1,0.13) : Qt.rgba(0,0,0,0.06))
    readonly property color controlFillHover: Theme.color("control.fillHover", dark ? Qt.rgba(1,1,1,0.22) : Qt.rgba(0,0,0,0.12))
    readonly property color controlEdge: Theme.color("control.edge", dark ? Qt.rgba(1,1,1,0.14) : Qt.rgba(0,0,0,0.10))

    readonly property color chipIdle: Theme.color("chip.idle", dark ? Qt.rgba(1,1,1,0.07) : Qt.rgba(0,0,0,0.06))
    readonly property color chipAccent: Qt.rgba(accent.r, accent.g, accent.b, 0.95)
    readonly property color chipHalo: Qt.rgba(accent.r, accent.g, accent.b, 0.26)
    readonly property int chipHaloWidth: Math.round(Theme.num("chip.haloWidth", 5))
    readonly property color trackColor: Theme.color("slider.track", dark ? Qt.rgba(1,1,1,0.15) : Qt.rgba(0,0,0,0.13))

    /* Used where a state has no distinct glyph of its own. */
    readonly property real iconDimmedOpacity: Theme.num("icon.dimmedOpacity", 0.32)

    readonly property string fontFamily: Theme.str("font.family", "Noto Sans")
    readonly property int fontSize: Math.round(Theme.num("font.size", Math.max(7, Math.round(11 * s))))

    // ---- Depth ------------------------------------------------------------
    /* Apple separates cards from the desktop with a soft shadow rather than an
       outline. Having neither is a large part of why the surfaces read as flat
       stickers rather than as floating panels. */
    readonly property color shadowColor: Theme.color("shadow.color", "#000000")
    readonly property real shadowBlur: Theme.num("shadow.spread", 1.0)
    /* blurMax must be matched by shadowPad in the window, or the falloff is
       clipped at the surface edge and the shadow looks weak rather than soft. */
    readonly property int shadowBlurMax: Math.round(Theme.num("shadow.blur", 40))
    readonly property real shadowOpacity: Theme.num("shadow.opacity", dark ? 0.48 : 0.22)
    readonly property int shadowYOffset: Math.round(Theme.num("shadow.offsetY", 8))

    // ---- Motion -----------------------------------------------------------
    readonly property int animIn: Math.round(Theme.num("motion.in", 150))
    readonly property int animOut: Math.round(Theme.num("motion.out", 200))
}
