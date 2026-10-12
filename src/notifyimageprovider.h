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
        /* toImage() validates the header against the buffer and refuses
           anything but 8-bit RGB or RGBA, so a malformed hint is a missing
           image here rather than a read past the end of it. */
        QImage owned = d.toImage();
        if (owned.isNull()) {
            return {};
        }

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
