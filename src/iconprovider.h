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
#include <QSet>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override
    {
        /* Every step of the chain, because each one has been the answer at
           least once and a missing icon is silent otherwise.

           The -symbolic retry is for the loader that needs it. Plasma
           announces a headset as "portable-symbolic", a name Breeze does not
           ship any file for, while it does ship "portable". KDE's icon engine
           resolves that on its own — measured, it returns an icon for every
           -symbolic name tried — so this line does nothing when the engine is
           present. Qt's built-in loader does no such thing, and that is
           exactly the configuration where icons were reported broken. */
        QIcon icon = QIcon::fromTheme(id);
        if (icon.isNull() && id.endsWith(QLatin1String("-symbolic"))) {
            icon = QIcon::fromTheme(id.chopped(9));
        }
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("dialog-information"));
        }
        if (icon.isNull()) {
            /* Nothing resolved, which means no usable icon theme rather than
               one missing name — every theme has dialog-information. Said
               once per name: a notification storm must not become a log
               storm, and the first line is the one that explains it. */
            static QSet<QString> reported;
            if (!reported.contains(id)) {
                reported.insert(id);
                qWarning("glassosd: no icon for '%s' and no dialog-information either — "
                         "icon theme '%s' is not resolving. Is an icon theme installed?",
                         qPrintable(id), qPrintable(QIcon::themeName()));
            }
            return {};
        }

        /* Ask for what the caller wants, but never for more than the theme
           actually has.

           Which matters only when KDE's icon engine is absent. With it — it
           arrives with plasma-integration — Breeze's SVGs are rendered at
           whatever size is asked for, and this cap never binds. Without it,
           Qt's own loader is limited to the sizes a theme ships on disk, and
           Breeze has no scalable directory: audio-volume-high exists at 16,
           22 and 24px and nothing larger, measured on a current Breeze. Ask
           that loader for 90 and it stretches the 24px drawing to 90.

           Taking the largest the theme really has means the scene graph does
           any scaling once, with its own filtering, rather than receiving
           something already stretched. It cannot invent detail the theme
           never drew — an icon that only exists at 24px will still look soft
           at 60 — but it is no longer degraded twice on the way.

           The old floor of 64 is gone with it: raising a request cannot
           improve an icon whose source size is fixed, and on the built-in
           loader it was what triggered the upscale. */
        const int asked = qMax(requested.width(), requested.height());
        const int want = asked > 0 ? asked : 64;
        const QSize available = icon.actualSize(QSize(want, want));
        const int edge = available.isEmpty() ? want : qMin(want, qMax(available.width(),
                                                                     available.height()));

        QPixmap pm = icon.pixmap(QSize(edge, edge));
        if (size) {
            *size = pm.size();
        }
        return pm;
    }
};
