/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Resolves "image://icon/audio-volume-high" against the icon theme.

    Phase 1 only needs the standard audio/brightness/keyboard icons, which
    Breeze always ships, so plain QIcon::fromTheme (which follows the theme's
    own Inherits= chain down to hicolor) is enough. The Papirus-Dark leg of the
    dunstrc chain exists for third-party *application* icons and is therefore a
    Phase 3 concern, handled when notifications arrive.
*/
#pragma once

#include <QIcon>
#include <QQuickImageProvider>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override
    {
        /* Honour the requested size, and never below 64 — QIcon picks the
           nearest *available* theme size, so asking for 30 on a theme that
           ships 22 and 48 yields a blurry upscaled 22. */
        const int edge = qMax(64, qMax(requested.width(), requested.height()));

        QIcon icon = QIcon::fromTheme(id);
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("dialog-information"));
        }
        QPixmap pm = icon.pixmap(QSize(edge, edge));
        if (size) {
            *size = pm.size();
        }
        return pm;
    }
};
