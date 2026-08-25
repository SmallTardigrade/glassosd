/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Layouts
import org.glassosd.ui

/*
    Notification card, following Apple's notification language:
    a small-caps app identity line with the time on the right, then title,
    body, an "N more notifications" line, and full-width segmented actions.

    Grouped notifications render as a physical stack with card edges peeking
    out beneath — the single clearest signal that more are hiding behind this
    one, and the reason a burst reads as "several" rather than "a card with a
    number on it".
*/
Item {
    id: root

    property var entry            // the model row
    property int stackDepth: 0    // how many edges to peek out below

    /* The ListView moves this delegate when a card above it goes away. The
       GlassPanel inside does not move relative to *us*, so none of its own
       geometry handlers fire, and the input region it registered — which is
       in window coordinates — keeps describing where the card used to be.
       The compositor is then left with a hole in the mask exactly where the
       card now is, so clicks fall through to the window behind and nothing
       even highlights on hover. Binding through mapToItem() does not help:
       QML does not re-evaluate it when an ancestor moves. The delegate's own
       y is the thing that actually changes, so drive it from here. */
    onYChanged: card.refreshBlur()
    signal dismissed()
    signal settingsRequested()
    signal moreRequested()
    signal actionTriggered(string key)
    signal replySent(string text)
    signal activated()
    signal hoverChanged(bool hovered)

    implicitHeight: card.implicitHeight + stackDepth * Style.stackOffset

    // ---- stacked edges, drawn behind and below --------------------------
    Repeater {
        model: root.stackDepth
        delegate: Rectangle {
            required property int index
            z: -1 - index
            anchors.horizontalCenter: parent.horizontalCenter
            width: card.width - (index + 1) * Style.stackInset * 2
            height: card.height
            y: (index + 1) * Style.stackOffset
            radius: Style.cardRadius
            color: Style.cardStackEdge
            antialiasing: true

            /* Seam along the *bottom* of each sheet. Only the bottom sliver
               of an edge is visible — its top is hidden behind the card in
               front — so a seam on the top edge draws nothing at all. */
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Style.cardRadius * 0.5
                anchors.rightMargin: Style.cardRadius * 0.5
                height: 1
                color: Style.cardStackSeam
            }
        }
    }

    GlassPanel {
        id: card
        width: parent.width
        radius: Style.cardRadius
        surfaceColor: Style.cardBackground
        /* Cards are glass too. At the default 0.975 alpha the backdrop effect
           is imperceptible, but a theme that lowers card.background needs the
           blur behind it or the card is merely see-through. */
        glass: true
        shadow: true
        implicitHeight: body.implicitHeight + Style.padding * 2

        Component.onCompleted: refreshBlur()
        onWidthChanged: refreshBlur()

        MouseArea {
            id: cardHover
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
            /* Pause the dwell timer while the pointer is over the card. */
            onContainsMouseChanged: root.hoverChanged(containsMouse)
            onClicked: (mouse) => {
                /* Clicking the body means the sender's "default" action when
                   it published one — that is what makes a chat notification
                   open the conversation. Middle-click always just dismisses,
                   and so does left-click when there is no default action. */
                if (mouse.button === Qt.LeftButton && root.entry.hasDefaultAction)
                    root.activated()
                else
                    root.dismissed()
            }
        }

        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Style.padding
            spacing: 3

            // ---- identity line ------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 7

                /* The squircle container is for *symbolic* glyphs, which need
                   something to sit in. An application icon is already a
                   designed shape — very often a rounded square itself — and
                   putting one inside our squircle gives a squircle within a
                   squircle at two different radii, which reads as a mistake.
                   macOS and Windows both show app icons bare for this reason.
                   Full-colour artwork with its own silhouette therefore gets
                   no container; only our own monochrome glyphs do. */
                Rectangle {
                    Layout.preferredWidth: Style.notifyIconSize + 8
                    Layout.preferredHeight: Style.notifyIconSize + 8
                    visible: root.entry.iconSource !== ""
                    radius: width * Style.chipRadiusRatio
                    color: appIcon.isSymbolic ? Style.chipIdle : "transparent"

                    Image {
                        id: appIcon
                        anchors.centerIn: parent
                        /* Bare app icons render a touch larger, because they
                           are no longer inset within a container. */
                        width: isSymbolic ? Style.notifyIconSize
                                          : Style.notifyIconSize + 6
                        height: width
                        source: root.entry.iconSource
                        sourceSize: Qt.size(Style.notifyIconSize * 3, Style.notifyIconSize * 3)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true

                        /* Our own glyphs are drawn white-on-nothing and need a
                           container to read against the card; anything from an
                           icon theme is artwork and does not. */
                        readonly property bool isSymbolic:
                            Icons.isCustom(root.entry.iconSource)
                            || String(root.entry.iconSource).indexOf("symbolic") !== -1
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.entry.appName
                    color: Style.foregroundMuted
                    font.family: Style.fontFamily
                    font.pointSize: Style.fontSize - 1
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.9
                    font.bold: true
                    elide: Text.ElideRight
                }

                /* Controls are always present rather than hover-revealed:
                   a button you cannot see is a button you do not know exists.
                   They sit at low opacity so they stay quiet until wanted,
                   which keeps the card calm without hiding the affordance. */
                Text {
                    text: root.entry.when
                    color: Style.foregroundDim
                    font.family: Style.fontFamily
                    font.pointSize: Style.fontSize - 2
                }

                CardButton {
                    icon: "settings"
                    onActivated: root.settingsRequested()
                }

                CardButton {
                    icon: "close"
                    onActivated: root.dismissed()
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 2
                visible: text !== ""
                text: root.entry.summary
                color: Style.foreground
                font.family: Style.fontFamily
                font.pointSize: Style.fontSize
                font.bold: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.entry.body
                color: Style.foregroundDim
                font.family: Style.fontFamily
                font.pointSize: Style.fontSize - 1
                wrapMode: Text.WordWrap
                maximumLineCount: Appearance.bodyLines
                elide: Text.ElideRight
                textFormat: Text.StyledText
            }

            /* A pill, matching the queue-overflow row exactly. "More exists
               beyond what is shown" was previously two different shapes
               depending on which mechanism produced it. */
            Rectangle {
                Layout.topMargin: 6
                visible: root.entry.groupCount > 1
                implicitWidth: moreLine.implicitWidth + 22
                implicitHeight: moreLine.implicitHeight + 10
                radius: height / 2
                color: moreArea.containsMouse ? Style.controlFillHover : Style.controlFill
                border.color: Style.controlEdge
                border.width: 1
                Behavior on color { ColorAnimation { duration: 110 } }

                Text {
                    id: moreLine
                    anchors.centerIn: parent
                    text: (root.entry.groupCount - 1) + " more notification"
                          + (root.entry.groupCount > 2 ? "s" : "")
                    color: Style.foreground
                    font.family: Style.fontFamily
                    font.pointSize: Style.fontSize - 2
                }

                MouseArea {
                    id: moreArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.moreRequested()
                }
            }

            /* "value" hint rendered as a bar — what a file transfer or an
               app-driven volume change actually wants to show. */
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.preferredHeight: 5
                visible: root.entry.progress >= 0
                radius: height / 2
                color: Style.trackColor

                Rectangle {
                    height: parent.height
                    radius: parent.radius
                    width: parent.width * Math.max(0, Math.min(1, root.entry.progress / 100))
                    color: Style.accent
                    Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }
            }

            // ---- inline reply (KDE extension, sender-driven) -------------
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 6
                visible: root.entry.inlineReply === true

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: 9
                    color: Qt.rgba(1, 1, 1, 0.08)
                    border.color: replyInput.activeFocus ? Style.accent : Qt.rgba(1, 1, 1, 0.12)
                    border.width: 1

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        visible: replyInput.text === "" && !replyInput.activeFocus
                        text: root.entry.replyPlaceholder
                        color: Style.foregroundDim
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSize - 1
                    }

                    TextInput {
                        id: replyInput
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        color: Style.foreground
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSize - 1
                        selectByMouse: true
                        clip: true
                        onAccepted: root.replySent(text)
                    }
                }

                Rectangle {
                    Layout.preferredWidth: sendLabel.implicitWidth + 22
                    Layout.preferredHeight: 32
                    radius: 9
                    color: replyInput.text !== ""
                           ? (sendHover.containsMouse ? Qt.lighter(Style.accent, 1.15) : Style.accent)
                           : Qt.rgba(1, 1, 1, 0.08)
                    Behavior on color { ColorAnimation { duration: 100 } }

                    Text {
                        id: sendLabel
                        anchors.centerIn: parent
                        text: root.entry.replySubmitText
                        color: Style.foreground
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSize - 1
                    }

                    MouseArea {
                        id: sendHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: if (replyInput.text !== "") root.replySent(replyInput.text)
                    }
                }
            }

            // ---- segmented actions --------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 1
                visible: root.entry.actions.length > 0

                Repeater {
                    model: root.entry.actions
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: actHover.containsMouse ? Style.controlFillHover
                                                      : Style.controlFill
                        Behavior on color { ColorAnimation { duration: 100 } }

                        // Square off the inner edges so the row reads as one
                        // segmented control rather than separate buttons.
                        topLeftRadius: index === 0 ? 9 : 0
                        bottomLeftRadius: index === 0 ? 9 : 0
                        topRightRadius: index === root.entry.actions.length - 1 ? 9 : 0
                        bottomRightRadius: index === root.entry.actions.length - 1 ? 9 : 0

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: Style.foreground
                            font.family: Style.fontFamily
                            font.pointSize: Style.fontSize - 1
                        }

                        MouseArea {
                            id: actHover
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.actionTriggered(modelData.key)
                        }
                    }
                }
            }
        }
    }
}
