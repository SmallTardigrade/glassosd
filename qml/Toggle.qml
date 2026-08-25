/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import org.glassosd.ui

/* A real switch, not a button that says "on" or "off". State should be
   readable from the control's shape rather than from its label. */
Item {
    id: root

    property bool checked: false
    signal toggled()

    implicitWidth: 42
    implicitHeight: 24

    /* Geometry is derived from the component's own fixed implicit size, not
       from parent.width. Deriving it from the parent fed back through the
       layout and produced a binding loop on the animation's "to" value. */
    Rectangle {
        id: track
        anchors.fill: parent
        radius: root.implicitHeight / 2
        color: root.checked ? Style.accent : Style.controlFill
        border.color: root.checked ? "transparent" : Style.controlEdge
        border.width: 1
        Behavior on color { ColorAnimation { duration: 150 } }

        Rectangle {
            id: knob
            width: root.implicitHeight - 6
            height: width
            radius: width / 2
            y: 3
            x: root.checked ? root.implicitWidth - width - 3 : 3
            color: "white"
            Behavior on x {
                NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.toggled()
    }
}
