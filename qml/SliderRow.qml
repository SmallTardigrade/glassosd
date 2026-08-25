/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Layouts

/*
    An icon and a track, used for both the volume and the brightness rows in
    the centre. Written by hand rather than with QtQuick.Controls.Slider so it
    picks up Style/Theme directly — a Controls Slider drags in a whole style
    plugin whose colours would then have to be fought.
*/
Item {
    id: root

    property string iconName: ""
    /* 0..1. Held locally while dragging so the thumb tracks the finger even
       if the backend is slow to report the new value back. */
    property real value: 0
    property bool muted: false
    signal moved(real v)
    signal iconClicked()

    implicitHeight: Math.max(Style.px(34), track.height)
    Layout.fillWidth: true

    RowLayout {
        anchors.fill: parent
        spacing: Style.px(10)

        Rectangle {
            Layout.preferredWidth: Style.px(30)
            Layout.preferredHeight: Style.px(30)
            radius: width * Style.chipRadiusRatio
            color: iconArea.containsMouse ? Style.controlFillHover : "transparent"

            Image {
                anchors.centerIn: parent
                width: Style.px(17)
                height: Style.px(17)
                source: root.iconName ? Icons.source(root.iconName) : ""
                opacity: root.muted ? Style.iconDimmedOpacity : 0.92
                sourceSize.width: width * 3
                sourceSize.height: height * 3
                smooth: true
            }

            MouseArea {
                id: iconArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.iconClicked()
            }
        }

        Item {
            id: track
            Layout.fillWidth: true
            height: Style.px(20)

            Rectangle {
                id: groove
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: Style.px(6)
                radius: height / 2
                color: Style.trackColor

                Rectangle {
                    width: Math.max(height, parent.width * root.value)
                    height: parent.height
                    radius: height / 2
                    color: root.muted ? Style.foregroundDim : Style.accent
                    /* Short, and linear-ish. Values now arrive within about
                       40ms of a keypress, so a long ease reads as lag rather
                       than as polish — the bar is still gliding to the last
                       value when the next keypress lands. */
                    Behavior on width {
                        enabled: !drag.pressed
                        NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
                    }
                }
            }

            Rectangle {
                id: thumb
                width: Style.px(14)
                height: width
                radius: width / 2
                color: "#ffffff"
                anchors.verticalCenter: parent.verticalCenter
                x: Math.max(0, Math.min(parent.width - width,
                                        parent.width * root.value - width / 2))
                scale: drag.pressed ? 1.15 : (drag.containsMouse ? 1.08 : 1.0)
                Behavior on scale { NumberAnimation { duration: 90 } }
                Behavior on x {
                    enabled: !drag.pressed
                    NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
                }
            }

            MouseArea {
                id: drag
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor

                function apply(mx) {
                    const v = Math.max(0, Math.min(1, mx / width))
                    root.value = v
                    root.moved(v)
                }
                onPressed: (m) => apply(m.x)
                onPositionChanged: (m) => { if (pressed) apply(m.x) }
                /* Wheel matches what the same key does elsewhere: 5% steps. */
                onWheel: (w) => {
                    const step = w.angleDelta.y > 0 ? 0.05 : -0.05
                    const v = Math.max(0, Math.min(1, root.value + step))
                    root.value = v
                    root.moved(v)
                }
            }
        }

        Text {
            Layout.preferredWidth: Style.px(34)
            horizontalAlignment: Text.AlignRight
            text: Math.round(root.value * 100) + "%"
            color: Style.foregroundDim
            font.family: Style.fontFamily
            font.pointSize: Style.fontSize - 1
        }
    }
}
