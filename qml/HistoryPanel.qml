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

    /* One row of the per-app settings panel: what the switch does, then why
       you would want it, then the switch. The second line is not decoration —
       "Ignore" and "Mute" are one word apart and four letters different, and
       the difference between them (does a record survive?) is the entire
       reason both exist. */
    component SettingRow: RowLayout {
        id: settingRow
        property alias title: rowTitle.text
        property alias detail: rowDetail.text
        property bool checked: false
        /* Held on with no way to turn it off — Mute while Ignore is set. */
        property bool locked: false
        signal toggled()

        Layout.fillWidth: true
        spacing: 10
        opacity: locked ? 0.5 : 1.0

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                id: rowTitle
                Layout.fillWidth: true
                color: Style.foreground
                font.family: Style.fontFamily
                font.pointSize: Style.fontSize - 1
                elide: Text.ElideRight
            }
            Text {
                id: rowDetail
                Layout.fillWidth: true
                color: Style.foregroundDim
                font.family: Style.fontFamily
                font.pointSize: Style.fontSize - 3
                wrapMode: Text.WordWrap
            }
        }

        Toggle {
            Layout.alignment: Qt.AlignVCenter
            enabled: !settingRow.locked
            checked: settingRow.checked
            onToggled: settingRow.toggled()
        }
    }

    /* Close when the centre stops being the focused surface. Alt+Tab, or a
       click on another window, should dismiss it the way any other transient
       panel behaves — leaving it floating over whatever you switched to is
       just in the way. Guarded on panelOpen so this cannot fight the opening
       animation. */
    onActiveChanged: {
        if (!active && HistoryModel.panelOpen) {
            HistoryModel.panelOpen = false
        }
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
        /* Which side the centre lives on — swaync's positionX, and the same
           two values. The surface itself covers the whole screen either way;
           only this strip and the click-away area either side of it move. */
        anchors.right: Appearance.centreSide === 8 ? parent.right : undefined
        anchors.left: Appearance.centreSide === 4 ? parent.left : undefined
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

        /* Each widget lives in its own Component so [Appearance] Widgets can
           decide both which appear and in what order — see Appearance::reload.
           Two rules make that work through a Loader:

           - the root sets implicitHeight rather than Layout.preferredHeight,
             because Layout attached properties on a Loader's *contents* apply
             to the contents, not to the Loader that the layout actually sees;
           - visibility is a `shown` property rather than `visible`, because
             an item's visible is already forced false by a hidden parent, so
             binding the Loader's visible back to it is a loop. */
        Component {
            id: titleWidget
            // ---- title row ----------------------------------------------
            RowLayout {
                property bool shown: true
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
                    enabled: HistoryModel.total > 0
                    onActivated: HistoryModel.clearAll()
                }
            }
        }

        Component {
            id: mprisWidget
            /* Media player widget — swaync can host one in its control centre
               and this was the last thing it had that we did not. Only shown
               when a player is actually on the bus. */
            Rectangle {
                property bool shown: MprisController.available
                implicitHeight: 74
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
        }

        Component {
            id: volumeWidget
            // ---- volume ---------------------------------------------------
            /* Wrapped in a card like every other widget rather than floating
               on the panel: a bare slider on glass has nothing to sit on and
               reads as an orphan control. */
            Rectangle {
                property bool shown: SystemControls.volumeAvailable
                implicitHeight: Style.px(46)
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
        }

        Component {
            id: dndWidget

            // ---- Do Not Disturb -----------------------------------------
            Rectangle {
                property bool shown: true
                implicitHeight: 46
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
        }

        Component {
            id: notificationsWidget
            /* The notification list, and the two things that belong to it:
               the per-app settings panel that the group headers open into,
               and the empty state. These travel together — "notifications"
               names the whole area, the way swaync's widget of that name
               does, not the ListView on its own. */
            Item {
                property bool shown: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

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

                            /* One place holding the four values, re-read from disk
                               rather than bound. AppSettings' accessors are invokable
                               methods, and a binding onto a method call is evaluated
                               once and never again — the old panel bound to them and
                               then assigned `checked` by hand afterwards, which meant
                               the switches showed whatever the last click had been
                               rather than what the config said. Reloading on the app
                               changing and on the panel opening covers every way the
                               file gets edited underneath us. */
                            QtObject {
                                id: appRules
                                readonly property string app: HistoryModel.groupFilterLabel
                                property bool muted: false
                                property bool ignored: false
                                property bool neverExpires: false
                                property bool alwaysCollapsed: false

                                function refresh() {
                                    if (app === "") return
                                    muted = AppSettings.muted(app)
                                    ignored = AppSettings.ignored(app)
                                    neverExpires = AppSettings.neverExpires(app)
                                    alwaysCollapsed = AppSettings.alwaysCollapsed(app)
                                }
                                onAppChanged: refresh()
                            }

                            Connections {
                                target: HistoryModel
                                function onPanelOpenChanged() {
                                    if (HistoryModel.panelOpen) appRules.refresh()
                                }
                            }

                            /* Four outcomes, not four rule keys. Each one is still a
                               single key in glassosdrc — see AppSettings — so nothing
                               here is doing anything a config file cannot say, but the
                               panel asks the question in the form the user has, which
                               is "what should this app be allowed to do", not "what
                               should skip_display be set to".

                               Read back from the config on every open rather than held
                               in QML state: glassosdctl writes the same keys, and a
                               switch showing what this panel last did rather than what
                               the file says would be a lie the moment anyone used the
                               command line. */
                            SettingRow {
                                id: muteRow
                                title: "Mute"
                                detail: locked ? "Held on while this app is ignored"
                                               : "No popup, but still kept in history"
                                locked: ignoreRow.checked
                                checked: appRules.muted
                                onToggled: {
                                    AppSettings.setMuted(appRules.app, !checked)
                                    appRules.refresh()
                                }
                            }

                            SettingRow {
                                id: ignoreRow
                                title: "Ignore"
                                detail: "Dropped entirely — no popup and no record"
                                checked: appRules.ignored
                                onToggled: {
                                    AppSettings.setIgnored(appRules.app, !checked)
                                    appRules.refresh()
                                }
                            }

                            SettingRow {
                                id: expireRow
                                title: "Never expire"
                                detail: "Popups stay on screen until you dismiss them"
                                checked: appRules.neverExpires
                                onToggled: {
                                    AppSettings.setNeverExpires(appRules.app, !checked)
                                    appRules.refresh()
                                }
                            }

                            SettingRow {
                                id: collapseRow
                                title: "Always collapsed"
                                detail: "This app's group opens folded in the centre"
                                checked: appRules.alwaysCollapsed
                                onToggled: {
                                    AppSettings.setAlwaysCollapsed(appRules.app, !checked)
                                    appRules.refresh()
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

                        /* A list in a desktop panel, not a page on a phone: running
                           off the end and springing back reads as the list refusing
                           to go further rather than as having reached the bottom. */
                        boundsBehavior: Flickable.StopAtBounds
                        /* Touchpad flicks glide roughly twice as far as the defaults
                           (1500 / 2500) allow, which is much closer to what every
                           other scrolling surface on the desktop does. */
                        flickDeceleration: 900
                        maximumFlickVelocity: 6000

                        /* Mouse wheel, handled here rather than by Flickable.

                           Flickable turns one notch into a very short flick and then
                           decelerates it away almost immediately, and — the part that
                           actually makes it feel wrong — a second notch arriving
                           during that deceleration *replaces* the first instead of
                           adding to it. Spinning the wheel therefore moves the list
                           barely further than one notch does. Accumulating onto
                           scrollAnim.to fixes exactly that: each notch extends the
                           distance already being travelled.

                           Mouse only. A touchpad sends pixel deltas that Flickable
                           already tracks one-to-one with your fingers, and taking
                           that over would replace direct manipulation with an
                           animation chasing it. */
                        WheelHandler {
                            id: wheel
                            target: null
                            acceptedDevices: PointerDevice.Mouse
                            property real notch: 180   // px per detent, ~3 entries

                            onWheel: (event) => {
                                const maxY = Math.max(0, list.contentHeight - list.height)
                                if (maxY <= 0)
                                    return
                                const from = scrollAnim.running ? scrollAnim.to : list.contentY
                                const delta = event.angleDelta.y / 120 * wheel.notch
                                scrollAnim.to = Math.max(0, Math.min(maxY, from - delta))
                                scrollAnim.restart()
                            }
                        }
                        NumberAnimation {
                            id: scrollAnim
                            target: list
                            property: "contentY"
                            duration: 220
                            easing.type: Easing.OutCubic
                        }

                        /* Something to show how far down the backlog you are. Without
                           it a list that has stopped scrolling and a list that has
                           run out of entries look identical. */
                        Rectangle {
                            anchors.right: parent.right
                            anchors.rightMargin: 1
                            width: 3
                            radius: 1.5
                            color: Style.foregroundDim
                            visible: list.contentHeight > list.height
                            opacity: list.moving || scrollAnim.running ? 0.55 : 0.18
                            Behavior on opacity { NumberAnimation { duration: 250 } }

                            height: Math.max(24, list.height * (list.height / list.contentHeight))
                            /* Offset by contentY because a visual child of a
                               Flickable is parented to its contentItem, not to the
                               view — without this the indicator scrolls away with
                               the entries it is supposed to be tracking. */
                            y: list.contentY + (list.contentHeight <= list.height ? 0
                               : (list.contentY / (list.contentHeight - list.height))
                                 * (list.height - height))
                        }

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

                                        /* The app's icon beside its name. A collapsed
                                           group is otherwise a bare word, and a column
                                           of bare words is far slower to scan than a
                                           column of icons — which is the whole point
                                           of collapsing in the first place. */
                                        Image {
                                            Layout.preferredWidth: 16
                                            Layout.preferredHeight: 16
                                            visible: model.headerIcon !== ""
                                            source: model.headerIcon
                                            sourceSize: Qt.size(48, 48)
                                            fillMode: Image.PreserveAspectFit
                                            smooth: true
                                            mipmap: true
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
                                    /* Style.padding, not a hardcoded 20. The
                                       entries were noticeably tighter than the
                                       popups showing the same notification, and
                                       got relatively tighter still as Scale went
                                       up, because the padding here never scaled
                                       with anything while the text did. */
                                    height: entryBody.implicitHeight + Style.padding * 2
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
                                        cursorShape: Qt.PointingHandCursor
                                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                        onClicked: (mouse) => {
                                            if (mouse.button === Qt.MiddleButton) {
                                                HistoryModel.removeAt(index)
                                                return
                                            }
                                            /* Same gesture as a popup: open the thing
                                               the notification was about. For a live
                                               notification that is its default action;
                                               for one that has expired the sender is
                                               gone, so it falls back to launching the
                                               application. */
                                            HistoryModel.activateEntry(index)
                                            HistoryModel.panelOpen = false
                                        }
                                    }

                                    RowLayout {
                                        id: entryBody
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: Style.padding
                                        anchors.rightMargin: Math.round(Style.padding * 0.7)
                                        spacing: Math.round(Style.padding * 0.8)

                                        Image {
                                            Layout.preferredWidth: Style.notifyIconSize - 4
                                            Layout.preferredHeight: Style.notifyIconSize - 4
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
                                            spacing: 3

                                            /* Same two sizes the popup uses for
                                               the same two strings. They used to
                                               be a step smaller each, so the
                                               centre read as a cramped version of
                                               the notification rather than as the
                                               same notification. The centre is the
                                               surface you actually *read* — it has
                                               no business being the smaller of the
                                               two. */
                                            Text {
                                                Layout.fillWidth: true
                                                text: model.summary
                                                color: Style.foreground
                                                font.family: Style.fontFamily
                                                font.pointSize: Style.fontSize
                                                font.bold: true
                                                elide: Text.ElideRight
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                visible: text !== ""
                                                text: model.body
                                                color: Style.foregroundDim
                                                font.family: Style.fontFamily
                                                font.pointSize: Style.fontSize - 1
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: Appearance.centreBodyLines
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
                }
            }
        }

        Component {
            id: backlightWidget
            // ---- brightness -----------------------------------------------
            Rectangle {
                property bool shown: SystemControls.brightnessAvailable
                implicitHeight: Style.px(46)
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
        }

        Component {
            id: buttonsWidget
            // ---- quick actions --------------------------------------------
            ButtonsGrid {
                property bool shown: ButtonsModel.count > 0
            }
        }

        readonly property var widgetComponents: ({
            "title": titleWidget,
            "mpris": mprisWidget,
            "volume": volumeWidget,
            "dnd": dndWidget,
            "notifications": notificationsWidget,
            "backlight": backlightWidget,
            "buttons-grid": buttonsWidget
        })

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.padding
            spacing: 10

            /* Order and membership both come from the config. A name the
               build does not know is dropped by Appearance::reload with a
               warning, so widgetComponents never has to answer for one. */
            Repeater {
                model: Appearance.widgets

                delegate: Loader {
                    required property string modelData
                    /* Only the notification list grows; everything else is
                       as tall as it says it is. */
                    readonly property bool fills: modelData === "notifications"

                    Layout.fillWidth: true
                    Layout.fillHeight: fills
                    Layout.preferredHeight: fills ? 0 : (item ? item.implicitHeight : 0)
                    visible: item ? item.shown : false

                    sourceComponent: panel.widgetComponents[modelData] || null
                }
            }
        }
    }
}
