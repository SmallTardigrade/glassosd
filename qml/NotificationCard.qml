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

    /* A stack is one object and casts one shadow, around the silhouette of
       the card and its sheets together. Rasterising the lot and shadowing the
       result is what makes that silhouette available — MultiEffect works off
       the item's own alpha, so the shape comes out right without anyone
       describing it. Off for a lone card, which shadows itself as before and
       should not pay for a layer it does not need. */
    layer.enabled: stackDepth > 0
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

    implicitHeight: card.implicitHeight + stackDepth * Style.stackOffset

    // ---- stacked edges, drawn behind and below --------------------------
    /* Each sheet is clipped to the sliver that actually peeks out.

       It used to be a full card-height rectangle parked behind the card. That
       was invisible while cards were opaque, but they are glass now and an
       opaque rectangle behind a translucent card is simply a backing: the top
       9px of the card showed the wallpaper through it and everything below
       showed this rectangle instead. The card read as translucent along its
       top edge and solid for the rest of its height — a hard horizontal step
       across the card with nothing in the design to explain it.

       Clipping to the sliver means the sheet never covers any part of the
       card, so the glass is glass the whole way down. The visible result is
       unchanged for an opaque theme, which is what this looked like when it
       was written.

       The sheets are the card's own material — same fill, same alpha, same
       backdrop region — rather than a colour of their own. That is not
       tidiness, it is the only thing that works: a glass card's *appearance*
       is whatever the backdrop makes it, so no fixed sheet colour relates to
       it. Opaque went black against a white page while the card sat at
       mid-grey; lightened-and-translucent washed out to a pale band. Sharing
       the material means both are lit by the same backdrop and cannot drift
       apart, and the card's own drop shadow falling across the slivers is
       what separates the layers. */
    Repeater {
        id: sheets
        model: root.stackDepth
        delegate: Item {
            required property int index
            /* Above the card, not behind it.

               This is why every fill colour tried for these sheets came out
               black. The card draws its drop shadow as part of itself at z 0 —
               opacity 0.55, blur 50, offset 9px straight down in this theme —
               and the sheets peek out 9 and 18px below the card, which is
               precisely the darkest part of that shadow. Sitting behind it,
               they were being viewed through a half-black overlay, so opaque,
               translucent and lightened all looked like the same black band.

               They cannot cover the card by being in front of it: each is
               clipped to a strip that starts at the card's bottom edge. All
               they cover is the shadow directly beneath the card, which is
               what a sheet sitting there would cover in reality. The shadow
               still falls past the last sheet, so the stack as a whole keeps
               one. Index order is preserved: 0 is nearest the front. */
            z: root.stackDepth - index
            anchors.horizontalCenter: parent.horizontalCenter
            width: card.width - (index + 1) * Style.stackInset * 2
            /* Each sheet gets its own band, starting where the one in front
               of it stops — not from the card's bottom edge every time.

               Overlapping them is what turned the nearest sheet black. Two
               translucent layers compose: 0.78 over 0.78 is an effective 0.95,
               so the strip where sheet 0 lay over sheet 1 was very nearly
               opaque while the strip where only sheet 1 showed was not.
               Measured over a white page: card 79, sheet 1 at 62, sheet 0 at
               29 — and 0.95*18 + 0.05*218 predicts 27.7, which is the whole
               explanation. The fill colour was never the problem. */
            y: card.height + index * Style.stackOffset
            height: Style.stackOffset
            clip: true

            /* The region has to describe the *painted* rectangle, not the clip
               band: the band is only stackOffset tall, so the radius gets
               clamped to half of that and the region comes out near-square
               while the sheet is painted with the card's full radius. That
               left blurred backdrop showing outside the sheet's corners, which
               reads as square corners with lines running off the ends. The
               card-sized rectangle inside the clip is the real shape; its
               upper part lands inside the card's own region, which costs
               nothing because regions are unioned. */
            function refreshSheet() {
                if (!Window.window)
                    return
                const p = mapToItem(null, 0, height - card.height)
                const r = Qt.rect(p.x, p.y, width, card.height)
                Surface.setPanelRegion(Window.window, sheetRect, r, Style.cardRadius)
                if (card.glass) {
                    Surface.applyContrast(Window.window, r, Style.cardRadius,
                                          Style.bgContrast, Style.bgIntensity,
                                          Style.bgSaturation)
                }
            }
            Component.onCompleted: refreshSheet()
            onYChanged: refreshSheet()
            onWidthChanged: refreshSheet()
            onHeightChanged: refreshSheet()
            Component.onDestruction: if (Window.window) Surface.clearPanelRegion(Window.window, sheetRect)


            /* Card-sized and bottom-aligned inside the clip, so the corners
               that show are the same radius as the card's own. Drawing a
               short rounded rect instead would round the top corners too and
               read as a separate pill rather than as a sheet behind. */
            Rectangle {
                id: sheetRect
                width: parent.width
                height: card.height
                y: parent.height - height
                /* Capped to the height of the strip this sheet shows through.

                   At the card's own 16px radius only the bottom 9px of the
                   curve is ever visible, and over those 9px the edge sweeps
                   14px inward — an almost 60-degree diagonal. Geometrically
                   that is exactly what a card of the same radius peeking out
                   by 9px looks like, and it reads as a chamfered wedge rather
                   than as a rounded card. Capping it lets the corner finish
                   inside the strip. */
                radius: Math.min(Style.cardRadius, Style.stackOffset)
                /* Same rule between one sheet and the next: only the last of
                   them is an outside edge. */
                bottomLeftRadius: index === root.stackDepth - 1 ? radius : 0
                bottomRightRadius: index === root.stackDepth - 1 ? radius : 0
                color: Style.cardStackEdge
                antialiasing: true

                /* Seam along the bottom of each sheet. Only the bottom sliver
                   is ever visible, so a seam on the top edge draws nothing. */
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
    }

    GlassPanel {
        id: card
        width: parent.width
        radius: Style.cardRadius
        /* Sheets meet the card's bottom edge; a rounded corner there would
           leave a wedge of backdrop between the two. */
        flatBottom: root.stackDepth > 0
        surfaceColor: Style.cardBackground
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
