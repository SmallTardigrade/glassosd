/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "appearance.h"

#include <KConfigGroup>

#include <QDebug>
#include <QHash>

#include <cmath>
#include <utility>

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

    /* "top" keeps popups below fullscreen windows; "overlay" puts them above.
       The protocol has no layer between the two — fullscreen surfaces occupy
       exactly that gap — so it is an either/or.

       Note this does NOT decide whether Show Desktop hides the popup.
       Measured on KWin 6.7: an overlay-layer surface is hidden by Show
       Desktop just as a top-layer one is, while plasmashell's own surfaces
       stay because they belong to the desktop. KWin hides everything that is
       not the desktop rather than restacking, and layer-shell offers a client
       no way to opt out. Any layer-shell notification daemon has this — dunst
       and swaync included — so switching layer will not fix it. */
    /* 1 is a legitimate choice — a one-line popup is a real preference —
       but 0 would hide the body entirely, which is a typo, not a choice. */
    m_bodyLines = qBound(1, g.readEntry("BodyLines", 4), 20);
    m_centreBodyLines = qBound(1, g.readEntry("CentreBodyLines", 3), 20);
    /* 0 means "derive from Scale"; anything else is an explicit point size. */
    const int fs = g.readEntry("FontSize", 0);
    m_fontSize = (fs == 0) ? 0 : qBound(6, fs, 32);

    const QString layer = g.readEntry("NotifyLayer", QStringLiteral("top")).toLower();
    m_notifyLayer = (layer == QLatin1String("overlay")) ? 3 : 2;

    /* Where popups appear. Both spellings of centre are accepted — the config
       should not be the place a user finds out which side of the Atlantic the
       author was on. */
    {
        QString pos = g.readEntry("NotifyPosition", QStringLiteral("top-right"))
                          .trimmed().toLower();
        pos.replace(QLatin1String("center"), QLatin1String("centre"));

        static const QHash<QString, int> anchorFor{
            {QStringLiteral("top-left"),      1 | 4},
            {QStringLiteral("top-centre"),    1},
            {QStringLiteral("top-right"),     1 | 8},
            {QStringLiteral("bottom-left"),   2 | 4},
            {QStringLiteral("bottom-centre"), 2},
            {QStringLiteral("bottom-right"),  2 | 8},
        };
        const auto it = anchorFor.constFind(pos);
        if (it == anchorFor.cend()) {
            qWarning("glassosd: unknown [Appearance] NotifyPosition '%s' — "
                     "using top-right. Known: top-left top-centre top-right "
                     "bottom-left bottom-centre bottom-right",
                     qUtf8Printable(pos));
            m_notifyPosition = QStringLiteral("top-right");
            m_notifyAnchors = 1 | 8;
        } else {
            m_notifyPosition = pos;
            m_notifyAnchors = it.value();
        }
    }

    /* Bounded well short of a screen dimension: a margin large enough to push
       every popup off the display is a typo, and the symptom — notifications
       that arrive and are never seen — gives no hint of the cause. */
    m_notifyMarginX = qBound(0, g.readEntry("NotifyMarginX", 0), 2000);
    m_notifyMarginY = qBound(0, g.readEntry("NotifyMarginY", 0), 2000);

    /* swaync's positionX, same name in the config, same two values. */
    const QString side = g.readEntry("CentrePosition", QStringLiteral("right"))
                             .trimmed().toLower();
    m_centreSide = (side == QLatin1String("left")) ? 4 : 8;

    const QString osd = g.readEntry("OsdPosition", QStringLiteral("top"))
                            .trimmed().toLower();
    m_osdAnchor = (osd == QLatin1String("bottom")) ? 2
                : (osd == QLatin1String("centre") || osd == QLatin1String("center")) ? 0
                : 1;

    /* The canonical order, and the default. Every widget the centre can draw
       appears here exactly once; the configured list is an ordering of these
       names, swaync-style. */
    static const QStringList canonical{
        QStringLiteral("title"),   QStringLiteral("mpris"),
        QStringLiteral("volume"),  QStringLiteral("dnd"),
        QStringLiteral("notifications"), QStringLiteral("backlight"),
        QStringLiteral("buttons-grid"),
    };

    QStringList wanted = g.readEntry("Widgets", canonical);
    /* swaync calls the media widget "mpris"; earlier versions of this config
       accepted "media". Normalise so both spellings order identically. */
    for (QString &w : wanted) {
        w = w.trimmed().toLower();
        if (w == QLatin1String("media")) {
            w = QStringLiteral("mpris");
        }
    }

    /* Until now this key was a *set*: it decided which widgets appeared and
       the centre laid them out in a fixed sequence regardless. Reading an old
       config as an order would be a disaster rather than a change — a list
       like "mpris,volume,dnd" never mentioned the notification list, so
       honouring it literally would produce a notification centre with no
       notifications in it.

       So: naming `notifications` is how you opt in to ordering. A list
       without it is read the old way — the canonical order, filtered to what
       was asked for, with the structural widgets kept whatever happens. Any
       config copied from swaync names it (swaync's own default is
       "title,dnd,notifications"), so nothing has to be migrated by hand. */
    if (wanted.contains(QLatin1String("notifications"))) {
        m_widgets.clear();
        for (const QString &w : std::as_const(wanted)) {
            /* Silently dropping a typo would present as a widget that will
               not appear however the user sets it. */
            if (!canonical.contains(w)) {
                qWarning("glassosd: unknown widget '%s' in [Appearance] Widgets — "
                         "known names are %s",
                         qUtf8Printable(w), qUtf8Printable(canonical.join(QLatin1Char(' '))));
                continue;
            }
            if (!m_widgets.contains(w)) {   // a repeat would draw it twice
                m_widgets.append(w);
            }
        }
    } else {
        m_widgets.clear();
        for (const QString &w : canonical) {
            const bool structural = w == QLatin1String("title")
                                 || w == QLatin1String("notifications");
            if (structural || wanted.contains(w)) {
                m_widgets.append(w);
            }
        }
        if (g.hasKey("Widgets")) {
            qWarning("glassosd: [Appearance] Widgets does not list 'notifications', "
                     "so it is being read as a set in the built-in order. Add "
                     "'notifications' to the list to control the order yourself.");
        }
    }

    Q_EMIT changed();
}
