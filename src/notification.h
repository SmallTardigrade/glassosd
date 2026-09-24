/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    One notification, plus the freedesktop wire types needed to receive it.
*/
#pragma once

#include <QByteArray>
#include <QDBusArgument>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

/* The "image-data" hint: (iiibiiay). Raw pixels inline in the D-Bus message,
   used by clients that have no themed icon to point at. */
struct ImageData {
    int width = 0;
    int height = 0;
    int rowstride = 0;
    bool hasAlpha = false;
    int bitsPerSample = 0;
    int channels = 0;
    QByteArray data;

    bool isValid() const
    {
        return width > 0 && height > 0 && !data.isEmpty();
    }

    /* Length excluding padding in the final row, matching the freedesktop
       layout: (height - 1) * rowstride + width * ceil(channels * bps / 8). */
    int expectedLength() const
    {
        return (height - 1) * rowstride + width * ((channels * bitsPerSample + 7) / 8);
    }
};
Q_DECLARE_METATYPE(ImageData)

QDBusArgument &operator<<(QDBusArgument &arg, const ImageData &i);
const QDBusArgument &operator>>(const QDBusArgument &arg, ImageData &i);

enum class Urgency { Low = 0, Normal = 1, Critical = 2 };

/* Notifications arrive by two interfaces with incompatible id types: the
   freedesktop one addresses them by uint, the portal by (app id, string). The
   portal side mints uints of its own, and the top bit is reserved for them so
   that the two sequences can never meet — NotificationServer counts up from 1
   and nothing has ever come close. Any id can therefore be attributed to the
   interface that produced it by looking at one bit. */
namespace NotificationId
{
constexpr uint portalMask = 1u << 31;
constexpr bool isPortal(uint id) { return (id & portalMask) != 0; }
} // namespace NotificationId

struct Notification {
    uint id = 0;
    QString appName;
    QString desktopEntry;   // "desktop-entry" hint; better group key than appName
    QString appIcon;
    QString summary;
    QString body;
    QStringList actions;    // flat [key, label, key, label, ...] per the spec
    Urgency urgency = Urgency::Normal;
    int timeoutMs = -1;     // -1 server decides, 0 never expires
    QString category;      // "category" hint, used by rule matching
    QString stackTag;

    /* Inline reply is a KDE extension, not part of the freedesktop spec: an
       action with the key "inline-reply" plus x-kde-reply-* hints, answered
       with a NotificationReplied(id, text) signal. Honouring it means apps
       that already support it on Plasma keep working here. */
    bool inlineReply = false;
    QString replyPlaceholder;
    QString replySubmitText;
    ImageData image;

    QDateTime received;
    QDateTime displayedAt;  // set when it first reaches the visible list

    /* Coalescing: when several from one app collapse, the survivor carries the
       others' count and they are kept individually in history. */
    int groupCount = 1;

    /* "value" hint: a 0-100 progress figure. Both dunst and swaync render it
       as a bar; ignoring it means file transfers and app volume changes lose
       the only part that mattered. -1 means the sender did not send one. */
    int progressValue = -1;

    /* "transient" hint: the spec says such notifications should bypass
       persistence. We were recording them, which is a compliance bug. */
    bool transientHint = false;

    /* Lock screen privacy, from the portal's display-hint. A sender that sets
       either is saying this should not be readable by whoever is standing in
       front of a locked machine — a balance, a message, a one-time code.

       Three states rather than two bools, because a notification that says
       nothing also needs to be distinguishable from one that was never asked
       about: the configured default applies to Unset, and a sender's own
       choice always wins over it. */
    enum class LockPrivacy {
        Unset,       // sender said nothing; the configured default decides
        Show,        // readable even on the lock screen
        HideContent, // shown, but the summary and body are withheld
        Hide,        // not shown at all while locked
    };
    LockPrivacy lockPrivacy = LockPrivacy::Unset;

    /* Closed, but still on screen while it animates away. Nothing but the
       view should see one of these — indexOfDisplayed() hides them, so every
       piece of logic that looks a notification up treats it as already gone. */
    bool closing = false;
    /* Set by a rule's sound= key; empty means category and urgency decide.
       "none" is a rule asking for silence, which is not the same as no rule. */
    QString sound;
    /* Set by a rule's run= key: a command to run when this arrives. */
    QString run;
    /* Set by a rule's snooze= key: minutes to defer this on arrival. */
    int snoozeMinutes = -1;
    bool skipDisplay = false;    // rule said: no popup, history only
    /* Seconds for which an identical repeat should not re-open a popup.
       -1 == no suppression. Set by a rule; see Rule::repeatWindowSec. */
    int repeatWindowSec = -1;
    /* How many times this exact message has arrived. History collapses
       byte-identical repeats onto one entry and counts them here rather than
       keeping a row each; 1 means it has only happened once. Popup cards never
       set this — it is a property of the record, not of the notification. */
    int repeatCount = 1;
    bool historyIgnore = false;  // rule said: do not even record it

    QString groupKey() const
    {
        return desktopEntry.isEmpty() ? appName : desktopEntry;
    }
};

/* Close reasons, straight from the spec. Getting these right matters: senders
   read Dismissed as "the user saw this". */
namespace CloseReason
{
constexpr uint Expired = 1;
constexpr uint Dismissed = 2;
constexpr uint Closed = 3;   // via CloseNotification()
constexpr uint Undefined = 4;
}
