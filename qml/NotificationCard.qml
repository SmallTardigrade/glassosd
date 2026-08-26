/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Window
import QtQuick.Effects
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

    readonly property bool stacked: stackDepth > 0

    /* One layer for the whole stack: it flattens the cards so their alphas
       cannot compound, and it gives MultiEffect a silhouette to cast a single
       shadow around — the card and its sheets together, rather than the card
       shadowing its own sheets. Off for a lone card, which needs neither and
       should not pay for a layer.

       The glass alpha is applied here, to the flattened result, which is why
       everything inside is drawn opaque while stacked. */
    opacity: stacked ? Style.cardBackground.a : 1.0
    layer.enabled: stacked
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Style.shadowColor
        shadowBlur: Style.shadowBlur
        blurMax: Style.shadowBlurMax
        shadowOpacity: Style.shadowOpacity
        shadowVerticalOffset: Style.shadowYOffset
        autoPaddingEnabled: true
    }

    /* The ListView moves this delegate when a card above it goes away. The
       GlassPanel inside does not move relative to *us*, so none of its own
       geometry handlers fire, and the input region it registered — which is
       in window coordinates — keeps describing where the card used to be.
       The compositor is then left with a hole in the mask exactly where the
       card now is, so clicks fall through to the window behind and nothing
       even highlights on hover. Binding through mapToItem() does not help:
       QML does not re-evaluate it when an ancestor moves. The delegate's own
       y is the thing that actually changes, so drive it from here. */
    onYChanged: {
        card.refreshBlur()
        /* The sheets register in window coordinates too, so they go stale for
           the same reason the card does when the ListView moves us. */
        for (let i = 0; i < sheets.count; ++i) {
            const s = sheets.itemAt(i)
            if (s) s.refreshSheet()
        }
    }
    signal dismissed()
    signal settingsRequested()
    signal moreRequested()
    signal actionTriggered(string key)
    signal replySent(string text)
    signal activated()
    signal hoverChanged(bool hovered)

    implicitHeight: card.implicitHeight
                    + (stackDepth > 0 ? Style.stackDrop(stackDepth - 1) : 0)

    // ---- stacked edges, drawn behind and below --------------------------
    /* The stack is flattened and faded once, rather than composited card by
       card. That is the whole trick, and everything else here follows from it.

       Two constraints fight each other on a translucent card. A sheet must not
       overlap the card, because 0.78 over 0.78 composes to an effective 0.95
       and the doubled strip goes nearly opaque — that was the black band. But
       not overlapping means the sheet has to stop at the card's bottom edge,
       and the card's rounded corner curves away from it, leaving a wedge of
       backdrop between the two. Squaring the corners closed the wedge and gave
       three different shapes down one edge; rounding them back restored the
       shapes and the wedge with them; widening the offset shrank the wedge by
       making the whole stack twice as tall.

       None of that is fixable one card at a time, because the problem is that
       they are being composited one card at a time. Rendering the lot into a
       layer at full opacity and applying the glass alpha to the *result* means
       overlap costs nothing: sheets sit fully behind the card, no strip is
       doubled, no corner has a wedge to hide, and the offset can go back to
       being small. Inside the layer they are ordinary opaque shapes stacked in
       z order, which is what they always wanted to be. */
    Repeater {
        id: sheets
        model: root.stackDepth
        delegate: Rectangle {
            id: sheetRect
            required property int index

            z: -1 - index
            anchors.horizontalCenter: parent.horizontalCenter
            width: card.width - (index + 1) * Style.stackInset * 2
            y: Style.stackDrop(index)
            height: card.height
            radius: Style.stackRadius
            color: root.stacked ? Style.opaque(Style.cardStackEdge) : Style.cardStackEdge
            /* The same lit edge the card carries. Without it the sheets had a
               fill and nothing else, so where one ended and the next began
               there was no boundary at all — same colour, no border, and no
               seam since a single sheet does not need one. The strip below the
               card rendered as one undifferentiated ledge rather than as two
               cards, and the silhouette went soft exactly where the card's own
               crisp edge stopped. */
            border.color: Style.glassEdge
            border.width: Style.edgeWidth
            antialiasing: true

            /* Registered so the compositor blurs behind the sliver too. The
               region is this rectangle exactly; the part of it behind the card
               simply lands inside the card's own region, and regions union. */
            function refreshSheet() {
                if (!Window.window)
                    return
                const p = mapToItem(null, 0, 0)
                const r = Qt.rect(p.x, p.y, width, height)
                Surface.setPanelRegion(Window.window, sheetRect, r, radius)
            }
            Component.onCompleted: refreshSheet()
            onYChanged: refreshSheet()
            onWidthChanged: refreshSheet()
            onHeightChanged: refreshSheet()
            Component.onDestruction: if (Window.window) Surface.clearPanelRegion(Window.window, sheetRect)

            /* No seam. With a single sheet the only boundary that matters is
               between it and the card, and the card's own bottom edge already
               draws that. A seam along the sheet's bottom would be a dark line
               under the outermost edge of the whole stack, which is not a
               boundary between anything. The references separate the layers by
               inset and tone alone, and so do we. */
        }
    }

    GlassPanel {
        id: card
        width: parent.width
        radius: Style.cardRadius
        /* Opaque while stacked: the root layer carries the alpha for the whole
           stack, so anything inside it that is translucent in its own right
           would be faded twice. */
        surfaceColor: root.stacked ? Style.opaque(Style.cardBackground) : Style.cardBackground
        /* Cards are glass too. At the default 0.975 alpha the backdrop effect
           is imperceptible, but a theme that lowers card.background needs the
           blur behind it or the card is merely see-through. */
        glass: true
        /* Only when this card is on its own. With sheets beneath it the whole
           stack casts one shadow, from the root — see below. A per-card shadow
           falls straight onto the sheets, and since they sit 9 and 18px below
           it, the nearest one lands in the darkest part of it and the next in
           a lighter part. That gradient across two sheets painted the same
           colour is what reads as "the first one is black and the second is
           grey", and it only shows over a bright backdrop, where a 55% black
           overlay has something to darken. */
        shadow: root.stackDepth === 0
        /* Every card in the stack keeps its own rounded corners. Squaring off
           the ones that meet something below closed the small wedge of
           backdrop between them, but at the price of a card with square
           bottom corners sitting above a sheet with square ones sitting above
           a sheet with round ones — three different shapes down one edge. The
           wedge is what a stack of rounded cards actually looks like; the
           mismatched shapes were something else entirely. */
        flatBottom: false
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
