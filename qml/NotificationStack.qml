/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.glassosd.ui

/*
    Notification popups. Position comes from [Appearance] NotifyPosition;
    top-right by default. Capped by NotificationModel, with the remainder
    queued behind a "+N more" row.

    New notifications are appended, so the column grows away from whichever
    edge it is anchored to and the newest card is always the one nearest that
    edge — no reordering needed when the anchor moves to the bottom.
*/
Window {
    id: win

    flags: Qt.FramelessWindowHint
    color: "transparent"

    width: Style.notifyWidth + Style.shadowPad * 2

    /* Grows while on screen, never shrinks; back to fitting once hidden.

       Shrinking a bottom-anchored layer surface costs a frame: the new, shorter
       layout is drawn before the compositor has moved the surface down, so
       the remaining cards were shown one card-height too high and then
       snapped back. Seen frame by frame whenever a dismissed card was
       removed from under the others. Held at its tallest, the window never
       moves while it is up; the column sits against the anchored edge
       inside it instead, and the empty part is transparent and outside the
       input region. */
    readonly property bool bottomAnchored: (Appearance.notifyAnchors & 2) && !(Appearance.notifyAnchors & 1)
    readonly property real neededHeight: column.implicitHeight + Style.shadowPad * 2
    property real heldHeight: 1
    height: Math.max(1, heldHeight)
    onNeededHeightChanged: if (neededHeight > heldHeight) heldHeight = neededHeight

    visible: Modules.notifications && (list.count > 0 || NotificationModel.hiddenCount > 0)

    /* Whether a frame has been presented since the window was last mapped.
       The first card of a quiet spell creates the surface, and its blur
       exists only from the commit that carries it; a card that started
       fading in at once spent ~170 ms see-through over a sharp backdrop
       (frame-by-frame capture). Cards wait for this before revealing. */
    property bool exposed: false
    onVisibleChanged: {
        exposed = false
        heldHeight = visible ? neededHeight : 1
    }
    onFrameSwapped: if (!exposed) exposed = true

    Component.onCompleted: {
        /* Anchors come from [Appearance] NotifyPosition — Top|Right by
           default. exclusiveZone 0 reserves nothing of our own but respects
           the panel's, so we sit clear of it whatever thickness and whichever
           edge it is configured on. */
        /* The base margin is 0 because shadowPad already provides the visual
           inset; adding both would push the stack too far from the corner.
           NotifyMarginX/Y are added on top for clearing things that are not
           panels and so have no exclusive zone — a browser's tab strip, a
           video player's controls. Only the anchored edges get the margin;
           on an unanchored edge it would fight the compositor's centring. */
        /* keyboardFocus false: a notification must never steal the next
           click. It is raised to OnDemand only while a reply field exists. */
        const a = Appearance.notifyAnchors
        Surface.initLayerShell(win, "glassosd-notifications", a,
                               (a & 1) ? Appearance.notifyMarginY : 0,
                               (a & 8) ? Appearance.notifyMarginX : 0,
                               (a & 2) ? Appearance.notifyMarginY : 0,
                               (a & 4) ? Appearance.notifyMarginX : 0,
                               false, 0, Appearance.notifyLayer)
        Surface.setInputFollowsPanels(win, true)
        Surface.setOutput(win, Appearance.notifyOutput)
    }

    /* Follow a position change made while the daemon is running. Without
       this, `glassosdctl position bottom-left` would look like it had done
       nothing until the next restart. */
    function applyPosition() {
        const a = Appearance.notifyAnchors
        Surface.setPosition(win, a,
                            (a & 1) ? Appearance.notifyMarginY : 0,
                            (a & 8) ? Appearance.notifyMarginX : 0,
                            (a & 2) ? Appearance.notifyMarginY : 0,
                            (a & 4) ? Appearance.notifyMarginX : 0)
    }

    Connections {
        target: Appearance
        function onChanged() {
            win.applyPosition()
            /* A stack on screen stays where it is until it empties; the next
               notification opens on the new output. */
            Surface.setOutput(win, Appearance.notifyOutput)
        }
    }

    /* The model holds a closed card for this long so it can animate away, so
       motion.out stays the one place the duration is written down.

       A Binding rather than an assignment in Component.onCompleted: that runs
       once, so editing motion.out in a theme changed the animation the view
       played while the model went on removing the row at the old duration —
       and the card vanished mid-fade. */
    Binding {
        target: NotificationModel
        property: "exitMs"
        value: Style.animOut
    }

    /* Only a card offering inline reply needs the keyboard. Anything else
       leaves focus where the user put it. */
    function refreshKeyboardFocus() {
        let wantsKeyboard = false
        for (let i = 0; i < NotificationModel.rowCount(); ++i) {
            if (NotificationModel.data(NotificationModel.index(i, 0),
                                       NotificationModel.InlineReplyRole) === true) {
                wantsKeyboard = true
                break
            }
        }
        Surface.setKeyboardFocus(win, wantsKeyboard)
    }

    Connections {
        target: NotificationModel
        function onRowsInserted() { win.refreshKeyboardFocus() }
        function onRowsRemoved()  { win.refreshKeyboardFocus() }
        function onModelReset()   { win.refreshKeyboardFocus() }
    }

    ColumnLayout {
        id: column
        x: Style.shadowPad
        width: win.width - Style.shadowPad * 2
        height: implicitHeight
        y: win.bottomAnchored ? win.height - Style.shadowPad - implicitHeight : Style.shadowPad
        spacing: Style.notifySpacing

        /* The cards register their regions in window coordinates, and moving
           the whole column moves them without changing their own y. */
        onYChanged: {
            for (let i = 0; i < list.count; ++i) {
                const c = list.itemAt(i)
                if (c) c.refreshRegions()
            }
            if (storm.shown) storm.refreshBlur()
        }

        Repeater {
            id: list
            model: NotificationModel

            delegate: NotificationCard {
                required property int index
                required property var model

                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight

                entry: model
                leaving: model.closing === true
                /* Never more sheets than there are notifications behind this
                   card — a stack of three under a group of two would be
                   drawing something that is not there. */
                stackDepth: Math.min(model.groupCount - 1, Style.stackMaxDepth)

                onDismissed: NotificationModel.dismiss(model.notifId)
                onHoverChanged: h => NotificationModel.setHovered(model.notifId, h)
                onActivated: NotificationModel.activate(win, model.notifId, "default")
                onActionTriggered: key => NotificationModel.activate(win, model.notifId, key)
                onReplySent: text => NotificationModel.sendReply(model.notifId, text)
                /* These two did the same thing, which is why "N more
                   notifications" opened the settings panel: it was the gear's
                   behaviour, and the group stayed folded on top of that. */
                onMoreRequested: HistoryModel.showGroup(model.groupKey, false)
                onSettingsRequested: HistoryModel.showGroup(model.groupKey, true)
                onSnoozeRequested: NotificationModel.snooze(model.notifId)
            }
        }

        // Queued remainder, and a way out of a storm
        /* Waking to twenty notifications meant swiping each one away: there
           was nothing on screen that cleared them, only `glassosdctl dismiss`.
           Shown once there is a pile — three cards or anything queued — and
           it clears what is on screen and what is waiting. Nothing is lost:
           every one is already in the notification centre's history. */
        GlassPanel {
            id: storm
            readonly property bool waiting: NotificationModel.hiddenCount > 0
            /* Live cards, not rows: a card fading out still has a row. Counted
               by rows, the pill outlived the cards it had just cleared by a
               quarter of a second, then jumped as they were removed. */
            readonly property bool shown: waiting || NotificationModel.liveCount >= 3
            /* Keeps the pill's place in the column, invisible, while the
               cards above it finish leaving. Collapsing it at once would drop
               every fading card by the pill's height mid-animation. */
            property bool held: false
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: stormRow.implicitWidth + 30
            Layout.preferredHeight: stormRow.implicitHeight + 16
            radius: height / 2
            surfaceColor: Style.cardBackground
            /* The same material as the cards above it. Without the backdrop
               blur it was the one see-through thing in the stack. */
            glass: true
            visible: shown || held
            opacity: shown ? 1 : 0
            onShownChanged: {
                if (shown) {
                    held = true
                    refreshBlur()
                } else if (Window.window) {
                    /* Gone with the click: blur and input region both, so no
                       blurred patch is left where the pill was. */
                    Surface.clearPanelRegion(Window.window, storm)
                }
            }
            onWidthChanged: if (shown) refreshBlur()
            /* GlassPanel registers its region when it is created, shown or
               not. Hidden from the start, it would leave a blurred, click-
               swallowing patch where no pill is drawn. */
            Component.onCompleted: Qt.callLater(() => {
                if (!storm.shown && Window.window)
                    Surface.clearPanelRegion(Window.window, storm)
            })
            Connections {
                target: list
                function onCountChanged() { if (list.count === 0) storm.held = false }
            }

            Row {
                id: stormRow
                anchors.centerIn: parent
                spacing: 8

                Text {
                    /* "Waiting", not "more notifications": these are queued
                       behind the display limit and will appear on their own.
                       The card's own pill used to read the same and means
                       something quite different — the rest of a group, which
                       pressing it opens. */
                    visible: storm.waiting
                    text: NotificationModel.hiddenCount + " waiting"
                    color: Style.foregroundDim
                    font.family: Style.fontFamily
                    font.pointSize: Style.fontSize - 1
                }
                Text {
                    visible: storm.waiting
                    text: "·"
                    color: Style.foregroundDim
                    font.family: Style.fontFamily
                    font.pointSize: Style.fontSize - 1
                }
                Text {
                    id: clearAll
                    text: "Clear all"
                    color: clearHover.hovered ? Style.accent : Style.foreground
                    font.family: Style.fontFamily
                    font.pointSize: Style.fontSize - 1
                    font.weight: Font.DemiBold

                    HoverHandler {
                        id: clearHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: NotificationModel.dismissAll()
                    }
                }
            }
        }
    }
}
