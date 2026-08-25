/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import org.glassosd.ui

/* Small pill button used in the history panel header. */
Rectangle {
    id: root

    property string label: ""
    property bool accented: false
    signal activated()

    implicitWidth: text.implicitWidth + 18
    implicitHeight: text.implicitHeight + 9
    radius: height / 2
    opacity: enabled ? 1 : 0.4   // Item.enabled, inherited
    color: accented ? Style.chipAccent
                    : (hover.containsMouse && enabled ? Style.controlFillHover : Style.controlFill)
    border.color: Style.controlEdge
    border.width: 1
    Behavior on color { ColorAnimation { duration: 110 } }

    Text {
        id: text
        anchors.centerIn: parent
        text: root.label
        color: Style.foreground
        font.family: Style.fontFamily
        font.pointSize: Style.fontSize - 2
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        onClicked: root.activated()
    }
}
