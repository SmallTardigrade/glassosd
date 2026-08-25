/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "appearance.h"

#include <KConfigGroup>

#include <cmath>

namespace
{
qreal channel(int v)
{
    const qreal c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

qreal luminance(const QColor &c)
{
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) + 0.0722 * channel(c.blue());
}

qreal contrast(const QColor &a, const QColor &b)
{
    const qreal la = luminance(a);
    const qreal lb = luminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

/* Walk the colour toward black until it clears the target ratio. Hue is
   preserved because every channel is scaled by the same factor. */
QColor darkenUntilReadable(QColor c, const QColor &bg, qreal target)
{
    for (int i = 0; i < 20 && contrast(c, bg) < target; ++i) {
        c = QColor(int(c.red() * 0.92), int(c.green() * 0.92), int(c.blue() * 0.92));
    }
    return c;
}
} // namespace

Appearance::Appearance(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("glassosdrc")))
{
    reload();
}

void Appearance::reload()
{
    m_config->reparseConfiguration();
    KConfigGroup g(m_config, QStringLiteral("Appearance"));

    const QString theme = g.readEntry("Theme", QStringLiteral("dark"));
    m_dark = theme.compare(QLatin1String("light"), Qt::CaseInsensitive) != 0;

    const QColor configured(g.readEntry("AccentColor", QStringLiteral("#3daee9")));
    /* An unparseable colour would otherwise blank every accented element. */
    QColor accent = configured.isValid() ? configured : QColor(QStringLiteral("#3daee9"));

    /* Any accent bright enough to read on a dark surface is, almost by
       definition, too pale to read on a light one: measured, the default blue
       scores 7.0 on the dark card and only 2.45 on the light card, well under
       the 4.5 AA body threshold. Rather than demand the user pick two
       colours, darken theirs until it actually passes. */
    if (!m_dark) {
        accent = darkenUntilReadable(accent, QColor(0xfd, 0xfd, 0xfe), 4.5);
    }
    m_accent = accent;

    const QString style = g.readEntry("LevelStyle", QStringLiteral("segmented"));
    m_levelStyle = (style == QLatin1String("bar")) ? QStringLiteral("bar")
                                                   : QStringLiteral("segmented");

    m_output = g.readEntry("Output", QStringLiteral("current"));

    /* Clamped: a scale of 0 would collapse every surface to nothing and a
       huge one would push notifications off screen, and both are easy typos
       in a config file. */
    m_scale = qBound(0.6, g.readEntry("Scale", 1.0), 2.0);
    m_notifyWidth = qBound(240, g.readEntry("NotifyWidth", 380), 900);
    m_solidity = qBound(0.0, g.readEntry("Solidity", 0.0), 1.0);

    /* "top" keeps popups below fullscreen windows, which is what most people
       want, but KWin's Show Desktop hides top-layer surfaces along with
       ordinary windows — deliberately, so it does not fight layer-shell
       stacking. "overlay" survives Show Desktop and rides above fullscreen
       too. The protocol has no layer between the two: fullscreen surfaces
       occupy exactly that gap, so this cannot be had both ways. */
    const QString layer = g.readEntry("NotifyLayer", QStringLiteral("top")).toLower();
    m_notifyLayer = (layer == QLatin1String("overlay")) ? 3 : 2;

    /* Default order mirrors swaync's: media, then DND, then the list. */
    m_widgets = g.readEntry("Widgets", QStringList{QStringLiteral("media"),
                                                   QStringLiteral("dnd")});

    Q_EMIT changed();
}
