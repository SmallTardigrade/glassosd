/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
pragma Singleton
import QtQuick

/*
    Our own icon set, so the four surfaces share one drawing style instead of
    borrowing whatever the icon theme happens to have.

    This also sidesteps a real problem found in Phase 2: Breeze's lock icons
    are an uneven set — caps lock has a matched on/off pair, num lock has only
    an "on", and Fn lock has nothing at all. Owning the glyphs means state is
    expressed by how we present them, not by which files a theme shipped.

    Anything we have not drawn falls through to the icon theme, which keeps the
    long tail (wifi, bluetooth, touchpad, power profile) working.
*/
QtObject {
    readonly property var custom: ({
        "volume-muted":        "icons/volume-muted.svg",
        "volume-low":          "icons/volume-low.svg",
        "volume-medium":       "icons/volume-medium.svg",
        "volume-high":         "icons/volume-high.svg",
        "mic-on":              "icons/mic-on.svg",
        "mic-muted":           "icons/mic-muted.svg",
        "brightness":          "icons/brightness.svg",
        "keyboard-brightness": "icons/keyboard-brightness.svg",
        "media":               "icons/media.svg",
        "lock-caps":           "icons/lock-caps.svg",
        "lock-num":            "icons/lock-num.svg",
        "lock-fn":             "icons/lock-fn.svg",
        "dnd":                 "icons/dnd.svg",
        "close":               "icons/close.svg",
        "snooze":              "icons/snooze.svg",
        "search":              "icons/search.svg",
        "settings":            "icons/settings.svg",
        "media-play":          "icons/media-play.svg",
        "media-pause":         "icons/media-pause.svg",
        "media-next":          "icons/media-next.svg",
        "media-prev":          "icons/media-prev.svg",
        /* Quick-action grid. Drawn rather than taken from the icon theme so
           the grid matches the rest of the surfaces; without an entry here
           source() falls through to image://icon/<name>, and a theme that
           has no such name silently returns a generic placeholder. */
        "wifi":                "icons/wifi.svg",
        "bluetooth":           "icons/bluetooth.svg",
        "lock":                "icons/lock.svg",
        "power":               "icons/power.svg",
        "reboot":              "icons/reboot.svg"
    })

    /* Our own glyphs are monochrome white and must be tinted to the current
       foreground; app icons are full-colour artwork and must not be touched. */
    function isCustom(key) {
        return !!key && custom[key] !== undefined
    }

    /* Whether an icon is a single-colour glyph that should be tinted to match
       the surface, rather than artwork to be shown as drawn.

       Ours always are. So is anything the icon theme calls "-symbolic": that
       suffix means monochrome by convention, and such icons are drawn in one
       flat colour on the assumption the caller will recolour them. Plasma
       hands us those names — a headset arrives as "portable-symbolic" — and
       leaving them alone painted a dark glyph on a dark chip, which is
       invisible rather than subtle. */
    function isMonochrome(key) {
        return isCustom(key) || (!!key && key.endsWith("-symbolic"))
    }

    function source(key) {
        if (!key)
            return ""
        const f = custom[key]
        return f ? Qt.resolvedUrl(f) : "image://icon/" + key
    }
}
