/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Effects
import org.glassosd.ui

/* Small round icon button in a notification card header. */
Rectangle {
    id: root
    property string icon: ""
    signal activated()

    implicitWidth: 24
    implicitHeight: 24
    radius: width / 2
    color: hover.containsMouse ? Style.controlFillHover : Style.controlFill
    border.color: Style.controlEdge
    border.width: 1
    Behavior on color { ColorAnimation { duration: 100 } }

    Image {
        id: glyph
        anchors.centerIn: parent
        width: 13
        height: 13
        source: Icons.source(root.icon)
        sourceSize: Qt.size(39, 39)
        smooth: true
        visible: false
    }

    /* Tinted, so the same white SVG works on a dark or a light surface. */
    MultiEffect {
        anchors.fill: glyph
        source: glyph
        colorization: 1.0
        colorizationColor: Style.foreground
        opacity: hover.containsMouse ? 1.0 : 0.52
        Behavior on opacity { NumberAnimation { duration: 100 } }
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.activated()
    }
}
