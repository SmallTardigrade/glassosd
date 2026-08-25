/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "notification.h"

QDBusArgument &operator<<(QDBusArgument &arg, const ImageData &i)
{
    arg.beginStructure();
    arg << i.width << i.height << i.rowstride << i.hasAlpha << i.bitsPerSample << i.channels << i.data;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ImageData &i)
{
    arg.beginStructure();
    arg >> i.width >> i.height >> i.rowstride >> i.hasAlpha >> i.bitsPerSample >> i.channels >> i.data;
    arg.endStructure();
    return arg;
}
