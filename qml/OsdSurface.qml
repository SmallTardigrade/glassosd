/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Effects
import org.glassosd.ui

/*
    Volume, brightness, mute, and the caps / num / Fn lock indicators.

    Anchored to the top edge only, which centres it horizontally under
    layer-shell without us computing anything.
*/
Window {
    id: win

    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"

    /* Room round the panel for its shadow to render into. Without this the
       window is exactly the panel and there is nowhere for a falloff to go,
       which is why the OSD was the one surface with no shadow at all. */
    width: panel.implicitWidth + Style.shadowPad * 2
    height: panel.implicitHeight + Style.shadowPad * 2

    /* The pad is transparent but it is still part of the surface, so anchoring
       at osdTopMargin would push the *visible* panel down by the pad. Take it
       back off the margin so the OSD sits exactly where it did before. */
    readonly property int edgeMargin: Math.max(0, Style.osdTopMargin - Style.shadowPad)

    visible: Modules.osd && (OsdModel.active || panel.opacity > 0.001)

    Component.onCompleted: {
        /* Anchor from [Appearance] OsdPosition: Top (1), Bottom (2), or 0 for
           no vertical anchor at all, which leaves the compositor to centre us.
           No keyboard focus: this must never steal input. */
        /* osdTopMargin only applies when anchored to an edge — against a
           centred OSD it would be an offset from the middle, which is not
           what anybody setting a "top margin" is asking for. */
        /* Overlay (3), unlike the notification stack. Transient feedback must be
           visible even over a fullscreen video or game — Top sits *below*
           fullscreen windows and the OSD would simply never appear. */
        const a = Appearance.osdAnchor
        Surface.initLayerShell(win, "glassosd-osd", a,
                               a === 1 ? win.edgeMargin : 0, 0,
                               a === 2 ? win.edgeMargin : 0, 0,
                               false, -1, 3)
        /* Confine input to the panel.

           The window is shadowPad larger than the panel on every side so the
           shadow has somewhere to render, and that padding is transparent but
           still part of the surface — so without this the OSD captures pointer
           events in an invisible 40px border around itself for as long as it
           is on screen. It was harmless while the window was exactly the panel
           and stopped being so the moment it grew.

           The notification stack has always done this for the same reason; the
           OSD simply never needed it before. */
        Surface.setInputFollowsPanels(win, true)
        Surface.setOutput(win, Appearance.osdOutput)
    }

    Connections {
        target: Appearance
        function onChanged() {
            /* Waits for the OSD to hide if it is showing; see Surface.setOutput. */
            Surface.setOutput(win, Appearance.osdOutput)
            const a = Appearance.osdAnchor
            Surface.setPosition(win, a,
                                a === 1 ? win.edgeMargin : 0, 0,
                                a === 2 ? win.edgeMargin : 0, 0)
        }
    }

    Connections {
        target: OsdModel
        function onChanged() {
            if (OsdModel.active) {
                panel.refreshBlur()
            } else {
                Surface.clearBlur(win)
                Surface.clearContrast(win)
            }
        }
    }

    GlassPanel {
        id: panel

        glass: true
        shadow: true
        implicitWidth: Math.max(OsdModel.showingProgress ? Style.minWidth : Style.minWidthText,
                                content.implicitWidth + Style.pillPaddingH * 2)
        implicitHeight: Style.chipSize + Style.padding * 2
        /* Centred inside the padded window rather than filling it, so the pad
           stays clear on all four sides for the shadow. */
        width: implicitWidth
        height: implicitHeight
        anchors.centerIn: parent

        opacity: OsdModel.active ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: OsdModel.active ? Style.animIn : Style.animOut
                easing.type: Easing.OutCubic
            }
        }

        RowLayout {
            id: content
            anchors.fill: parent
            anchors.leftMargin: Style.pillPaddingH
            anchors.rightMargin: Style.pillPaddingH
            spacing: Style.spacing

            /* Circular well behind the icon. Carrying state in the chip means
               a single glyph per lock key works for both on and off, which is
               what lets us own the icon set rather than depend on whichever
               on/off pairs a theme happens to ship. */
            Item {
                Layout.preferredWidth: Style.chipSize
                Layout.preferredHeight: Style.chipSize
                Layout.alignment: Qt.AlignVCenter
                /* [Appearance] OsdIcon=false leaves the bar and the text and
                   drops the chip, for people who want the OSD to be only the
                   level. */
                visible: OsdModel.iconName !== "" && Appearance.osdIcon

                // Halo, only present when the lock is engaged
                Rectangle {
                    anchors.centerIn: parent
                    width: Style.chipSize + Style.chipHaloWidth * 2
                    height: width
                    radius: width * Style.chipRadiusRatio
                    color: "transparent"
                    border.color: Style.chipHalo
                    border.width: Style.chipHaloWidth
                    antialiasing: true
                    opacity: OsdModel.iconAccent ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 160 } }
                }

                Rectangle {
                    id: chip
                    anchors.fill: parent
                    radius: width * Style.chipRadiusRatio
                    color: OsdModel.iconAccent ? Style.chipAccent : Style.chipIdle
                    antialiasing: true
                    Behavior on color { ColorAnimation { duration: 160 } }
                }

                Image {
                    id: osdGlyph
                    anchors.centerIn: parent
                    width: Style.iconSize
                    height: Style.iconSize
                    source: Icons.source(OsdModel.iconName)
                    /* The size it is drawn at, in logical pixels. Qt
                       multiplies by the device pixel ratio itself —
                       qquickimagebase.cpp requests sourcesize * devicePixelRatio
                       — so this asks the SVG renderer for exactly the pixels
                       the icon occupies and no more.

                       It used to ask for three times that, on the reasoning
                       that spare detail cannot hurt. It does: at a device
                       ratio of 2 that is a 126px texture minified into ~37
                       device pixels, and minification is the soft part. */
                    sourceSize: Qt.size(Style.iconSize, Style.iconSize)
                    smooth: true
                    visible: !Icons.isMonochrome(OsdModel.iconName)
                    opacity: OsdModel.iconDimmed ? Style.iconDimmedOpacity : 1.0
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                }

                /* Single-colour glyphs get tinted so they read on an accent
                   chip in dark mode and a pale chip in light mode. Full-colour
                   app icons are left as they are. See Icons.isMonochrome. */
                MultiEffect {
                    anchors.fill: osdGlyph
                    source: osdGlyph
                    visible: Icons.isMonochrome(OsdModel.iconName)
                    colorization: 1.0
                    colorizationColor: OsdModel.iconAccent ? "#ffffff" : Style.foreground
                    opacity: OsdModel.iconDimmed ? Style.iconDimmedOpacity : 1.0
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                }
            }

            /* Segmented, after macOS's classic volume OSD, rather than a
               continuous line. Discrete blocks make the level readable at a
               glance without reading the percentage, and they give the OSD an
               identity of its own instead of looking like a progress bar. */
            /* Continuous bar, when LevelStyle=bar. */
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Style.barHeight
                Layout.alignment: Qt.AlignVCenter
                visible: OsdModel.showingProgress && Appearance.levelStyle === "bar"
                radius: height / 2
                color: Style.trackColor

                Rectangle {
                    height: parent.height
                    radius: parent.radius
                    width: parent.width * Math.min(1, OsdModel.maxValue > 0
                                                      ? OsdModel.value / OsdModel.maxValue : 0)
                    color: OsdModel.value > 100 ? Style.critical : Style.accent
                    Behavior on width { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
                }
            }

            Row {
                id: segments
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                visible: OsdModel.showingProgress && Appearance.levelStyle === "segmented"
                spacing: Style.segmentGap

                readonly property int count: Style.segmentCount
                readonly property real ratio: OsdModel.maxValue > 0
                    ? Math.min(1, OsdModel.value / OsdModel.maxValue) : 0
                readonly property real filled: ratio * count

                Repeater {
                    model: segments.count
                    delegate: Rectangle {
                        required property int index
                        width: (segments.width - Style.segmentGap * (segments.count - 1))
                               / segments.count
                        height: Style.segmentHeight
                        radius: 1.5

                        /* The partially-covered block fades rather than
                           snapping, so small changes still read as movement. */
                        readonly property real cover:
                            Math.max(0, Math.min(1, segments.filled - index))

                        color: OsdModel.value > 100 ? Style.critical : Style.accent
                        opacity: 0.18 + 0.82 * cover
                        Behavior on opacity {
                            NumberAnimation { duration: 110 }
                        }
                    }
                }
            }

            Text {
                /* Never fillWidth while anything else can absorb the slack.
                   The bar is meant to have it; a filling Text is handed
                   whatever is left and elides itself instead of driving the
                   panel wider.

                   With no bar and no icon there is nothing else, and the
                   panel is still at least minWidthText wide, so the slack has
                   to go somewhere: left alone the text sat hard against the
                   left edge with the gap all on the right. In that one case
                   the text takes the slack and centres in it. */
                readonly property bool aloneInThePill:
                    !OsdModel.showingProgress && !Appearance.osdIcon
                Layout.fillWidth: aloneInThePill
                Layout.rightMargin: OsdModel.showingProgress ? 4 : 0
                Layout.alignment: Qt.AlignVCenter
                horizontalAlignment: OsdModel.showingProgress ? Text.AlignRight
                                   : aloneInThePill ? Text.AlignHCenter
                                                    : Text.AlignLeft
                text: OsdModel.showingProgress
                      ? Math.round(OsdModel.value) + "%"
                      : OsdModel.text
                color: OsdModel.showingProgress ? Style.foregroundDim : Style.foreground
                font.family: Style.fontFamily
                font.pointSize: Style.fontSize
                font.features: { "tnum": 1 }   // stop the width jittering per digit
                elide: Text.ElideRight
            }
        }
    }
}
