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

    width: panel.implicitWidth
    height: panel.implicitHeight

    visible: OsdModel.active || panel.opacity > 0.001

    Component.onCompleted: {
        // anchors: 1 == Top. No keyboard focus: this must never steal input.
        /* Overlay (3), unlike the notification stack. Transient feedback must be
           visible even over a fullscreen video or game — Top sits *below*
           fullscreen windows and the OSD would simply never appear. */
        Surface.initLayerShell(win, "glassosd-osd", 1, Style.osdTopMargin, 0, 0, 0, false, -1, 3)
        Surface.setOutput(win, Appearance.output)
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
        implicitWidth: Math.max(OsdModel.showingProgress ? Style.minWidth : Style.minWidthText,
                                content.implicitWidth + Style.pillPaddingH * 2)
        implicitHeight: Style.chipSize + Style.padding * 2
        anchors.fill: parent

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
                visible: OsdModel.iconName !== ""

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
                    sourceSize: Qt.size(Style.iconSize * 3, Style.iconSize * 3)
                    smooth: true
                    visible: !Icons.isCustom(OsdModel.iconName)
                    opacity: OsdModel.iconDimmed ? Style.iconDimmedOpacity : 1.0
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                }

                /* Our glyphs are white SVGs. Tint them so they read on an
                   accent chip in dark mode and on a pale chip in light mode;
                   full-colour app icons are left untouched. */
                MultiEffect {
                    anchors.fill: osdGlyph
                    source: osdGlyph
                    visible: Icons.isCustom(OsdModel.iconName)
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
                /* Never fillWidth. The bar is the only element that should
                   absorb slack; a filling Text is handed whatever is left and
                   elides itself instead of driving the panel wider. */
                Layout.fillWidth: false
                Layout.rightMargin: OsdModel.showingProgress ? 4 : 0
                Layout.alignment: Qt.AlignVCenter
                horizontalAlignment: OsdModel.showingProgress ? Text.AlignRight : Text.AlignLeft
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
