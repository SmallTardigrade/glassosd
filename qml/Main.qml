/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick

/*
    Root object of the daemon. Holds one instance of each surface.

    Each surface is created through a Loader gated on its module, so a
    disabled module never constructs a layer-shell window at all. Hiding the
    window instead would still reserve the surface with the compositor, which
    on some setups is enough to affect input regions and exclusive zones.
*/
QtObject {
    property Loader osd: Loader {
        active: Modules.osd
        sourceComponent: OsdSurface {}
    }
    property Loader notifications: Loader {
        active: Modules.notifications
        sourceComponent: NotificationStack {}
    }
    property Loader history: Loader {
        active: Modules.notificationCentre
        sourceComponent: HistoryPanel {}
    }
}
