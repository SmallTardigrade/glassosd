/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.glassosd.ui

/*
    Notification centre: a single glass sheet holding solid content cards,
    grouped by app and collapsible — the pattern both Windows 11 and macOS
    use, and the thing a flat list cannot do.
*/
Window {
    id: win

    flags: Qt.FramelessWindowHint
    color: "transparent"
    visible: Modules.notificationCentre && HistoryModel.panelOpen

    Component.onCompleted: {
        /* Anchored on all four sides so the surface covers the screen: the
           panel takes the right-hand strip and the rest is a click-away
           catcher. Top layer, so screenshot selectors stay above us. */
        /* keyboardFocus starts false and is raised only while the panel is
           actually open. This surface is anchored on all four sides, so it
           covers the whole screen; holding keyboard focus while hidden meant
           it swallowed every global shortcut — Alt+Tab included — with
           nothing visible on screen to explain why. */
        Surface.initLayerShell(win, "glassosd-history", 1 | 2 | 4 | 8,
                               0, 0, 0, 0, false, 0)
        Surface.setOutput(win, Appearance.output)
    }

    onVisibleChanged: {
        Surface.setKeyboardFocus(win, visible)
        /* wpctl and brightnessctl are only queried while the centre is open;
           polling them for a panel nobody is looking at is waste.

           This has to hang off the *window*. The GlassPanel inside is always
           visible, so the earlier version — bound to the panel's own visible
           — never fired at all, and the volume and brightness sliders never
           moved when the keys were pressed. */
        SystemControls.setPolling(visible)
    }

    MouseArea {
        anchors.fill: parent
        onClicked: HistoryModel.panelOpen = false
    }

    GlassPanel {
        id: panel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Style.notifyMargin
        width: Style.historyWidth
        radius: Style.cardRadius + 4
        surfaceColor: Style.panelGlass
        glass: true
        shadow: true

        Component.onCompleted: refreshBlur()

        Keys.onEscapePressed: HistoryModel.panelOpen = false

        MouseArea { anchors.fill: parent; onClicked: {} }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.padding
            spacing: 10

            // ---- title row ----------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: HistoryModel.groupFilter !== ""
                          ? HistoryModel.groupFilterLabel : "Notifications"
                    color: Style.foreground
                    font.family: Style.fontFamily
                    font.pointSize: Style.fontSize + 4
                    font.bold: true
                    elide: Text.ElideRight
                }

                PanelButton {
                    visible: HistoryModel.groupFilter !== ""
                    label: "Show all"
                    onActivated: HistoryModel.clearFilter()
                }

                PanelButton {
                    label: "Clear"
                    enabled: list.count > 0
                    onActivated: HistoryModel.clearAll()
                }
            }

            /* Media player widget — swaync can host one in its control centre
               and this was the last thing it had that we did not. Only shown
               when a player is actually on the bus. */
            Rectangle {
                Layout.fillWidth: true
                visible: MprisController.available && (Appearance.hasWidget("mpris") || Appearance.hasWidget("media"))
                Layout.preferredHeight: visible ? 74 : 0
                radius: 12
                color: Style.entryCard

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    // Album art, falling back to a generic glyph
                    Rectangle {
                        Layout.preferredWidth: 50
                        Layout.preferredHeight: 50
                        radius: width * Style.chipRadiusRatio
                        color: Style.chipIdle
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: MprisController.artUrl
                            fillMode: Image.PreserveAspectCrop
                            visible: MprisController.artUrl !== "" && status === Image.Ready
                            smooth: true
                        }

                        Image {
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            source: Icons.source("media")
                            visible: MprisController.artUrl === ""
                            opacity: 0.6
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: MprisController.title !== "" ? MprisController.title
                                                               : MprisController.identity
                            color: Style.foreground
                            font.family: Style.fontFamily
                            font.pointSize: Style.fontSize - 1
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: MprisController.artist
                            color: Style.foregroundDim
                            font.family: Style.fontFamily
                            font.pointSize: Style.fontSize - 2
                            elide: Text.ElideRight
                        }
                    }

                    CardButton { icon: "media-prev";  onActivated: MprisController.previous() }
                    CardButton {
                        icon: MprisController.playing ? "media-pause" : "media-play"
                        onActivated: MprisController.playPause()
                    }
                    CardButton { icon: "media-next";  onActivated: MprisController.next() }
                }
            }

            // ---- volume ---------------------------------------------------
            /* Wrapped in a card like every other widget rather than floating
               on the panel: a bare slider on glass has nothing to sit on and
               reads as an orphan control. */
            Rectangle {
                Layout.fillWidth: true
                visible: Appearance.hasWidget("volume") && SystemControls.volumeAvailable
                Layout.preferredHeight: visible ? Style.px(46) : 0
                radius: 12
                color: Style.entryCard

                SliderRow {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    iconName: SystemControls.muted ? "volume-muted"
                            : SystemControls.volume <= 0.25 ? "volume-low"
                            : SystemControls.volume <= 0.75 ? "volume-medium" : "volume-high"
                    muted: SystemControls.muted
                    value: SystemControls.volume
                    onMoved: (v) => SystemControls.setVolume(v)
                    onIconClicked: SystemControls.toggleMute()
                }
            }


            // ---- Do Not Disturb -----------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: Appearance.hasWidget("dnd")
                Layout.preferredHeight: visible ? 46 : 0
                radius: 12
                color: Style.entryCard

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 10

                    Image {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        source: Icons.source("dnd")
                        sourceSize: Qt.size(54, 54)
                        smooth: true
                        opacity: NotificationModel.doNotDisturb ? 1.0 : 0.55
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Do Not Disturb"
                        color: Style.foreground
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSize
                    }

                    Toggle {
                        checked: NotificationModel.doNotDisturb
                        onToggled: NotificationModel.doNotDisturb = !NotificationModel.doNotDisturb
                    }
                }
            }

            /* Per-app settings, shown when drilled into one app. The gear on a
               notification card lands here — previously it opened a filtered
               list and nothing else, which is not "settings" by any reading.

               Every control writes a rule into glassosdrc, so the UI and
               glassosdctl manipulate exactly the same mechanism. */
            Rectangle {
                Layout.fillWidth: true
                visible: HistoryModel.groupFilter !== ""
                Layout.preferredHeight: visible ? appSettingsCol.implicitHeight + 20 : 0
                radius: 12
                color: Style.entryCard


                ColumnLayout {
                    id: appSettingsCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 10

                    Text {
                        text: "Settings for " + HistoryModel.groupFilterLabel
                        color: Style.foregroundMuted
                        font.family: Style.fontFamily
                        font.pointSize: Style.fontSize - 2
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Mute popups"
                            color: Style.foreground
                            font.family: Style.fontFamily
                            font.pointSize: Style.fontSize - 1
                        }
                        Toggle {
                            id: muteToggle
                            checked: AppSettings.muted(HistoryModel.groupFilterLabel)
                            onToggled: {
                                AppSettings.setMuted(HistoryModel.groupFilterLabel, !checked)
                                checked = !checked
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Keep in history"
                            color: Style.foreground
                            font.family: Style.fontFamily
                            font.pointSize: Style.fontSize - 1
                        }
                        Toggle {
                            id: histToggle
                            checked: !AppSettings.historyIgnored(HistoryModel.groupFilterLabel)
                            onToggled: {
                                AppSettings.setHistoryIgnored(HistoryModel.groupFilterLabel, checked)
                                checked = !checked
                            }
                        }
                    }
                }
            }

            // ---- grouped list -------------------------------------------
            ListView {
                id: list

                /* Open at the newest entry rather than at the top of the
                   backlog. positionViewAtEnd alone runs before the delegates
                   have been sized, so it lands short on a long list; the
                   Timer re-runs it once layout has settled. */
                function jumpToNewest() {
                    if (HistoryModel.newestFirst) positionViewAtBeginning()
                    else positionViewAtEnd()
                }
                Timer {
                    id: settle
                    interval: 30; repeat: false
                    onTriggered: list.jumpToNewest()
                }
                Connections {
                    target: HistoryModel
                    function onPanelOpenChanged() {
                        if (HistoryModel.panelOpen) { list.jumpToNewest(); settle.restart() }
                    }
                }
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: HistoryModel

                delegate: Loader {
                    required property int index
                    required property var model
                    width: ListView.view.width
                    sourceComponent: model.isHeader ? groupHeader : entryCard

                    // ---- group header ----
                    Component {
                        id: groupHeader
                        Item {
                            width: parent ? parent.width : 0
                            height: 30

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                spacing: 6

                                // Chevron rotates to show collapse state
                                Text {
                                    text: "▾"
                                    color: Style.foregroundDim
                                    font.pointSize: Style.fontSize - 1
                                    rotation: model.collapsed ? -90 : 0
                                    Behavior on rotation {
                                        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                                    }
                                }

                                Text {
                                    text: model.appName
                                    color: Style.foregroundMuted
                                    font.family: Style.fontFamily
                                    font.pointSize: Style.fontSize - 2
                                    font.capitalization: Font.AllUppercase
                                    font.letterSpacing: 0.9
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    Layout.preferredWidth: cnt.implicitWidth + 12
                                    Layout.preferredHeight: cnt.implicitHeight + 3
                                    radius: height / 2
                                    color: Qt.rgba(1, 1, 1, 0.10)
                                    Text {
                                        id: cnt
                                        anchors.centerIn: parent
                                        text: model.groupCount
                                        color: Style.foregroundDim
                                        font.family: Style.fontFamily
                                        font.pointSize: Style.fontSize - 3
                                        font.bold: true
                                    }
                                }

                                Item { Layout.fillWidth: true }

                                /* Drills into the app, which is where its
                                   settings live. Reachable from the centre
                                   itself, not only from a live popup. */
                                CardButton {
                                    icon: "settings"
                                    onActivated: HistoryModel.groupFilter = model.groupKey
                                }

                                CardButton {
                                    icon: "close"
                                    onActivated: HistoryModel.clearGroup(model.groupKey)
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                anchors.rightMargin: 62   // leave the gear and clear buttons clickable
                                onClicked: HistoryModel.toggleGroup(model.groupKey)
                            }
                        }
                    }

                    // ---- entry card ----
                    Component {
                        id: entryCard
                        Rectangle {
                            width: parent ? parent.width : 0
                            height: entryBody.implicitHeight + 20
                            radius: Style.cardRadius - 2
                            color: entryHover.containsMouse
                                   ? Style.entryCardHover : Style.entryCard
                            border.color: Style.entryCardEdge
                            border.width: 1
                            Behavior on color { ColorAnimation { duration: 110 } }

                            MouseArea {
                                id: entryHover
                                anchors.fill: parent
                                hoverEnabled: true
                            }

                            RowLayout {
                                id: entryBody
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                anchors.rightMargin: 8
                                spacing: 10

                                Image {
                                    Layout.preferredWidth: 26
                                    Layout.preferredHeight: 26
                                    Layout.alignment: Qt.AlignTop
                                    visible: model.iconSource !== ""
                                    source: model.iconSource
                                    sourceSize: Qt.size(78, 78)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    mipmap: true
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        Layout.fillWidth: true
                                        text: model.summary
                                        color: Style.foreground
                                        font.family: Style.fontFamily
                                        font.pointSize: Style.fontSize - 1
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: text !== ""
                                        text: model.body
                                        color: Style.foregroundDim
                                        font.family: Style.fontFamily
                                        font.pointSize: Style.fontSize - 2
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                    }
                                }

                                ColumnLayout {
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 2

                                    Text {
                                        Layout.alignment: Qt.AlignRight
                                        text: model.when
                                        color: Style.foregroundDim
                                        font.family: Style.fontFamily
                                        font.pointSize: Style.fontSize - 3
                                    }

                                    CardButton {
                                        Layout.alignment: Qt.AlignRight
                                        opacity: entryHover.containsMouse ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 110 } }
                                        icon: "close"
                                        onActivated: HistoryModel.removeAt(index)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: list.count === 0
                text: HistoryModel.groupFilter !== "" ? "Nothing from this app" : "No notifications"
                color: Style.foregroundDim
                font.family: Style.fontFamily
                font.pointSize: Style.fontSize - 1
                horizontalAlignment: Text.AlignHCenter
            }

            Item { Layout.fillHeight: list.count === 0 }

            // ---- brightness -----------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                visible: Appearance.hasWidget("backlight") && SystemControls.brightnessAvailable
                Layout.preferredHeight: visible ? Style.px(46) : 0
                radius: 12
                color: Style.entryCard

                SliderRow {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    iconName: "brightness"
                    value: SystemControls.brightness
                    onMoved: (v) => SystemControls.setBrightness(v)
                }
            }

            // ---- quick actions --------------------------------------------
            ButtonsGrid {
                Layout.fillWidth: true
                visible: Appearance.hasWidget("buttons-grid") && ButtonsModel.count > 0
            }
        }
    }
}
