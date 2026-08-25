/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Serves "image://notifyimage/<id>" from a notification's inline image-data
    hint, which arrives as raw pixels on the wire rather than as a file.
*/
#pragma once

#include "historymodel.h"
#include "notificationmodel.h"

#include <QImage>
#include <QQuickImageProvider>

class NotifyImageProvider : public QQuickImageProvider
{
public:
    NotifyImageProvider(NotificationModel *model, HistoryModel *history)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_model(model)
        , m_history(history)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override
    {
        /* "h123" asks history, "123" asks the live stack — the live one drops
           its copy when the popup closes, but history keeps it. */
        const ImageData d = id.startsWith(QLatin1Char('h'))
            ? m_history->imageFor(QStringView(id).mid(1).toUInt())
            : m_model->imageFor(id.toUInt());
        if (!d.isValid() || d.data.size() < d.expectedLength()) {
            return {};
        }

        /* The hint carries 8-bit RGB or RGBA. Anything else (16 bits per
           sample, odd channel counts) is rare enough that refusing beats
           rendering garbage. */
        if (d.bitsPerSample != 8 || (d.channels != 3 && d.channels != 4)) {
            return {};
        }
        const QImage::Format fmt = d.channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;

        /* QImage does not take ownership of the buffer, so copy before the
           QByteArray goes out of scope. */
        QImage img(reinterpret_cast<const uchar *>(d.data.constData()),
                   d.width, d.height, d.rowstride, fmt);
        QImage owned = img.copy();

        if (requested.isValid() && !requested.isEmpty()) {
            owned = owned.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (size) {
            *size = owned.size();
        }
        return owned;
    }

private:
    NotificationModel *m_model;
    HistoryModel *m_history;
};
