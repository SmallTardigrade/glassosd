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

    /* GridLayout rather than Grid, because a Grid has no notion of a cell
       spanning columns and a wide button is the whole point of Span. The
       explicit cellW is kept: GridLayout would otherwise distribute width by
       content, so a labelled button would starve the glyph-only ones beside
       it. */
    GridLayout {
        id: grid
        width: parent.width
        columns: ButtonsModel.perRow
        columnSpacing: root.gap
        rowSpacing: root.gap

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
                required property int span

                /* Its own cells plus the gaps it swallows between them. */
                Layout.columnSpan: Math.min(cell.span, ButtonsModel.perRow)
                Layout.preferredWidth: root.cellW * Layout.columnSpan
                                     + root.gap * (Layout.columnSpan - 1)
                Layout.preferredHeight: Style.px(40)
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

                /* Glyph and words together when there is room for both. A
                   one-cell button shows the glyph alone and keeps its name in
                   the tooltip; a spanning one can say what it is outright,
                   which is the reason to give it the width. */
                readonly property bool showsLabel: cell.label !== "" && cell.span > 1

                /* Breathing room at the ends. The row was centred with no width
                   of its own, so a label just long enough ran to both edges of
                   the button and sat against them. */
                readonly property int labelPad: Style.px(10)

                Row {
                    id: face
                    anchors.centerIn: parent
                    spacing: cell.showsLabel ? Style.px(8) : 0

                    Image {
                        id: glyph
                        anchors.verticalCenter: parent.verticalCenter
                        visible: cell.iconName !== ""
                        width: Style.px(18)
                        height: Style.px(18)
                        source: cell.iconName ? Icons.source(cell.iconName) : ""
                        sourceSize.width: width * 3
                        sourceSize.height: height * 3
                        smooth: true
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: cell.iconName === "" || cell.showsLabel
                        text: cell.label !== "" ? cell.label : cell.tooltip
                        color: cell.active ? "#ffffff" : Style.foreground
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSize
                        /* Never wider than the room actually left over, so a
                           label that does not fit is shortened rather than
                           being drawn over the edge of its own button. */
                        width: Math.min(implicitWidth,
                                        Math.max(0, cell.width - cell.labelPad * 2
                                                    - (glyph.visible ? glyph.width + face.spacing : 0)))
                        elide: Text.ElideRight
                    }
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
                    visible: area.containsMouse && cell.tooltip !== "" && !cell.showsLabel
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
