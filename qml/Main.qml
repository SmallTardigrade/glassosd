/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick

/*
    Root object of the daemon. Holds one instance of each surface.

    These are Windows, so they are instantiated directly rather than through a
    Loader. A Loader is an Item and cannot parent a Window, and assigning
    `sourceComponent: OsdSurface {}` instantiates the object instead of
    supplying a Component — which fails silently and leaves every surface
    dead with nothing in the log.

    Module gating therefore lives in each surface's own `visible` binding. A
    QML Window creates no wl_surface until it is first shown, so a module that
    is off still costs the compositor nothing.
*/
QtObject {
    property OsdSurface osd: OsdSurface {}
    property NotificationStack notifications: NotificationStack {}
    property HistoryPanel history: HistoryPanel {}
}
