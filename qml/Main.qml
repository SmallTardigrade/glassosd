/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick

/*
    Root object of the daemon. Holds one instance of each surface.
    Phases 3-5 add NotificationStack, HistoryPanel and DisplaySwitcher here.
*/
QtObject {
    property OsdSurface osd: OsdSurface {}
    property NotificationStack notifications: NotificationStack {}
    property HistoryPanel history: HistoryPanel {}
}
