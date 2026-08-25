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

    bool skipDisplay = false;    // rule said: no popup, history only
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
