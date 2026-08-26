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
    height: Math.max(1, column.implicitHeight + Style.shadowPad * 2)

    visible: Modules.notifications && (list.count > 0 || NotificationModel.hiddenCount > 0)

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
        Surface.setOutput(win, Appearance.output)
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
        function onChanged() { win.applyPosition() }
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
        anchors.fill: parent
        anchors.margins: Style.shadowPad
        spacing: Style.notifySpacing

        Repeater {
            id: list
            model: NotificationModel

            delegate: NotificationCard {
                required property int index
                required property var model

                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight

                entry: model
                /* One sheet, however many are behind it. Two was a guess and
                   it reads as a thicker slab rather than as more depth — the
                   count is already stated in words on the card, so the second
                   sheet was spending vertical space to repeat it badly. Both
                   iOS and Figma draw exactly one for any depth of stack. */
                stackDepth: model.groupCount > 1 ? 1 : 0

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
            }
        }

        // Queued remainder
        GlassPanel {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: moreLabel.implicitWidth + 30
            Layout.preferredHeight: moreLabel.implicitHeight + 16
            radius: height / 2
            surfaceColor: Style.cardBackground
            visible: NotificationModel.hiddenCount > 0
            onVisibleChanged: if (visible) refreshBlur()

            Text {
                id: moreLabel
                anchors.centerIn: parent
                text: NotificationModel.hiddenCount + " more notification"
                      + (NotificationModel.hiddenCount > 1 ? "s" : "")
                color: Style.foregroundDim
                font.family: Style.fontFamily
                font.pointSize: Style.fontSize - 1
            }
        }
    }
}
