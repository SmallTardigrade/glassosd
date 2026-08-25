/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Layouts

/*
    swaync's buttons-grid: the wifi / bluetooth / DND / lock / power row.

    A Grid with an explicitly computed cell width rather than a Flow. A Flow
    stretches the items on its final row to fill the line, so any button count
    that is not a multiple of the row length comes out visibly wrong — which is
    most of them.
*/
Item {
    id: root
    visible: ButtonsModel.count > 0
    Layout.fillWidth: true
    implicitHeight: visible ? grid.implicitHeight : 0

    readonly property int gap: Style.px(8)
    readonly property int cellW: Math.floor((width - gap * (ButtonsModel.perRow - 1))
                                            / ButtonsModel.perRow)

    Grid {
        id: grid
        width: parent.width
        columns: ButtonsModel.perRow
        spacing: root.gap

        Repeater {
            model: ButtonsModel

            Rectangle {
                id: cell
                required property int index
                required property string iconName
                required property string label
                required property string tooltip
                required property bool isToggle
                required property bool active

                width: root.cellW
                height: Style.px(40)
                radius: Style.px(12)

                /* An active toggle fills with accent. Anything subtler — a
                   tinted border, a corner dot — is not readable at a glance,
                   and glanceability is the only thing this grid is for. */
                color: cell.active ? Style.chipAccent
                                   : (area.containsMouse ? Style.controlFillHover
                                                         : Style.controlFill)
                border.width: Style.edgeWidth
                border.color: cell.active ? "transparent" : Style.controlEdge

                Behavior on color { ColorAnimation { duration: 120 } }

                Image {
                    anchors.centerIn: parent
                    visible: cell.iconName !== ""
                    width: Style.px(18)
                    height: Style.px(18)
                    source: cell.iconName ? Icons.source(cell.iconName) : ""
                    sourceSize.width: width * 3
                    sourceSize.height: height * 3
                    smooth: true
                }

                Text {
                    anchors.centerIn: parent
                    visible: cell.iconName === ""
                    text: cell.label
                    color: cell.active ? "#ffffff" : Style.foreground
                    font.family: Style.fontFamily
                    font.pointSize: Style.fontSize
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ButtonsModel.activate(cell.index)
                }

                /* The tooltip is the only place the name survives once an icon
                   is set, so a grid of bare glyphs stays discoverable. */
                Rectangle {
                    visible: area.containsMouse && cell.tooltip !== ""
                    anchors.bottom: parent.top
                    anchors.bottomMargin: Style.px(6)
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: tipText.implicitWidth + Style.px(14)
                    height: tipText.implicitHeight + Style.px(8)
                    radius: Style.px(6)
                    color: Style.solidSurface
                    border.width: Style.edgeWidth
                    border.color: Style.controlEdge
                    z: 10

                    Text {
                        id: tipText
                        anchors.centerIn: parent
                        text: cell.tooltip
                        color: Style.foreground
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSize - 1
                    }
                }
            }
        }
    }
}
