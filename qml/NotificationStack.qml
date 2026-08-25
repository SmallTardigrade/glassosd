/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.glassosd.ui

/*
    Notification popups: top-right, newest at the top, capped by
    NotificationModel with the remainder queued behind a "+N more" row.
*/
Window {
    id: win

    flags: Qt.FramelessWindowHint
    color: "transparent"

    width: Style.notifyWidth + Style.shadowPad * 2
    height: Math.max(1, column.implicitHeight + Style.shadowPad * 2)

    visible: Modules.notifications && (list.count > 0 || NotificationModel.hiddenCount > 0)

    Component.onCompleted: {
        /* anchors 1|8 == Top|Right. exclusiveZone 0 reserves nothing of our
           own but respects the panel's, so we sit clear of it whatever
           thickness it is configured to. */
        /* Margins are 0 because shadowPad already provides the visual inset;
           adding both would push the stack too far from the corner. */
        /* OnDemand keyboard focus so an inline reply field can actually be
           typed into; it is only taken when the surface is clicked. */
        /* keyboardFocus false: a notification must never steal the next
           click. It is raised to OnDemand only while a reply field exists. */
        Surface.initLayerShell(win, "glassosd-notifications", 1 | 8, 0, 0, 0, 0, false, 0)
        Surface.setInputFollowsPanels(win, true)
        Surface.setOutput(win, Appearance.output)
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
                /* Two peeking edges is enough to read as "a stack"; more just
                   eats vertical space without adding information. */
                stackDepth: model.groupCount > 2 ? 2 : (model.groupCount > 1 ? 1 : 0)

                onDismissed: NotificationModel.dismiss(model.notifId)
                onHoverChanged: h => NotificationModel.setHovered(model.notifId, h)
                onActivated: NotificationModel.activate(win, model.notifId, "default")
                onActionTriggered: key => NotificationModel.activate(win, model.notifId, key)
                onReplySent: text => NotificationModel.sendReply(model.notifId, text)
                onMoreRequested: {
                    HistoryModel.groupFilter = model.groupKey
                    HistoryModel.panelOpen = true
                }
                onSettingsRequested: {
                    HistoryModel.groupFilter = model.groupKey
                    HistoryModel.panelOpen = true
                }
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
