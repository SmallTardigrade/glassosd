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

QImage ImageData::toImage() const
{
    if (!isValid()) {
        return {};
    }
    const QImage::Format fmt = channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    /* QImage does not take ownership of the buffer, so copy before the
       QByteArray goes out of scope. */
    return QImage(reinterpret_cast<const uchar *>(data.constData()), width, height, rowstride, fmt).copy();
}

ImageData ImageData::fromImage(QImage img)
{
    if (img.isNull()) {
        return {};
    }
    if (img.width() > maxStoredEdge || img.height() > maxStoredEdge) {
        /* At least a pixel each way: a 1x5000 strip would otherwise round
           to nothing. */
        const QSize fit = img.size().scaled(maxStoredEdge, maxStoredEdge, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        img = img.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    img = img.convertToFormat(QImage::Format_RGBA8888);

    ImageData out;
    out.width = img.width();
    out.height = img.height();
    out.rowstride = int(img.bytesPerLine());
    out.hasAlpha = true;
    out.bitsPerSample = 8;
    out.channels = 4;
    out.data = QByteArray(reinterpret_cast<const char *>(img.constBits()), qsizetype(img.sizeInBytes()));
    return out;
}
