/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "surfacewindow.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QRegion>

#include <KWindowEffects>
#include <KConfigGroup>
#include <KSharedConfig>
#include <LayerShellQt/Window>

namespace
{
/* QRegion has no rounded-rect constructor, so compose one: two overlapping
   bands plus four corner ellipses. */
QRegion roundedRegion(const QRect &r, int radius)
{
    if (radius <= 0) {
        return QRegion(r);
    }
    const int d = radius * 2;
    QRegion region(r.adjusted(radius, 0, -radius, 0));
    region += QRegion(r.adjusted(0, radius, 0, -radius));
    region += QRegion(r.left(), r.top(), d, d, QRegion::Ellipse);
    region += QRegion(r.right() - d + 1, r.top(), d, d, QRegion::Ellipse);
    region += QRegion(r.left(), r.bottom() - d + 1, d, d, QRegion::Ellipse);
    region += QRegion(r.right() - d + 1, r.bottom() - d + 1, d, d, QRegion::Ellipse);
    return region;
}
} // namespace

Surface::Surface(QObject *parent)
    : QObject(parent)
{
}

bool Surface::blurAvailable() const
{
    /* Not auto-detected, because nothing reliable exists to detect it with.

       KWindowEffects::isEffectAvailable(BlurBehind) cannot be trusted on
       Wayland in either direction: measured on KWin 6.7 with the Blur effect
       loaded and the protocol reporting
           ext_background_effect_manager_v1#46.capabilities(1)
       it still returned false. Gating on it therefore disables glass on
       systems where blur works perfectly. The capability is only visible in
       the ext-background-effect-v1 handshake, which would mean binding the
       protocol ourselves.

       So it is a setting. `auto` assumes blur is present on Wayland, which is
       right for KWin and Hyprland; sway, river and labwc have no blur at all
       and should set it to off, which also closes the translucency up so the
       surfaces stay readable. */
    static const bool available = [] {
        const QString mode = KSharedConfig::openConfig(QStringLiteral("glassosdrc"))
                                 ->group(QStringLiteral("Appearance"))
                                 .readEntry("Blur", QStringLiteral("auto"))
                                 .toLower();
        if (mode == QLatin1String("on")) {
            return true;
        }
        if (mode == QLatin1String("off")) {
            qInfo("glassosd: blur disabled by config — surfaces drawn solid");
            return false;
        }
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
            return true;
        }
        return KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
    }();
    return available;
}

void Surface::initLayerShell(QQuickWindow *window,
                             const QString &scope,
                             int anchors,
                             int marginTop,
                             int marginRight,
                             int marginBottom,
                             int marginLeft,
                             bool keyboardFocus,
                             int exclusiveZone,
                             int layer)
{
    if (!window) {
        return;
    }
    auto *layerWindow = LayerShellQt::Window::get(window);
    if (!layerWindow) {
        return; // not a Wayland layer-shell session; window stays a normal one
    }

    layerWindow->setScope(scope);
    layerWindow->setLayer(static_cast<LayerShellQt::Window::Layer>(layer));
    layerWindow->setAnchors(static_cast<LayerShellQt::Window::Anchors>(anchors));
    layerWindow->setMargins(QMargins(marginLeft, marginTop, marginRight, marginBottom));
    layerWindow->setExclusiveZone(exclusiveZone);
    layerWindow->setKeyboardInteractivity(keyboardFocus
                                        ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                                        : LayerShellQt::Window::KeyboardInteractivityNone);
}

void Surface::setPanelRegion(QQuickWindow *window, QObject *panel, const QRectF &rect, qreal radius)
{
    if (!window || !panel || rect.isEmpty()) {
        return;
    }
    /* Both containers are keyed on a raw QQuickWindow*, and recompute()
       dereferences it. Nothing purged them when a window died, so a panel
       outliving its window — which multi-monitor hotplug will produce as soon
       as surfaces are created per output — would have called setMask() on
       freed memory. */
    if (!m_regions.contains(window)) {
        connect(window, &QObject::destroyed, this, [this, window]() {
            m_regions.remove(window);
            m_maskedWindows.remove(window);
            m_lastMask.remove(window);
        });
    }

    auto &perWindow = m_regions[window];
    if (!perWindow.contains(panel)) {
        /* Cards are created and destroyed constantly; a stale rect would keep
           a blurred hole on screen after its card is gone. */
        connect(panel, &QObject::destroyed, this, [this, window, panel]() {
            clearPanelRegion(window, panel);
        });
    }
    perWindow.insert(panel, {rect, radius});
    recompute(window);
}

void Surface::clearPanelRegion(QQuickWindow *window, QObject *panel)
{
    auto it = m_regions.find(window);
    if (it == m_regions.end()) {
        return;
    }
    it->remove(panel);
    /* Drop the destroyed-handler too. Qt reuses freed addresses aggressively
       for QML delegates, so a panel cleared explicitly and then re-registered
       at the same address would otherwise stack a second connection on this
       singleton every time round. */
    disconnect(panel, &QObject::destroyed, this, nullptr);

    if (it->isEmpty()) {
        m_regions.erase(it);
        clearBlur(window);
        clearContrast(window);
        return;
    }
    recompute(window);
}

QStringList Surface::outputs() const
{
    QStringList names;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *s : screens) {
        names << s->name();
    }
    return names;
}

void Surface::setOutput(QQuickWindow *window, const QString &preference)
{
    if (!window) {
        return;
    }
    auto *layerWindow = LayerShellQt::Window::get(window);
    if (!layerWindow) {
        return;
    }

    if (preference.isEmpty() || preference == QLatin1String("current")) {
        /* Follow the active screen — dunst's follow=mouse equivalent and the
           sane default. (screenConfiguration is deprecated since 6.6.) */
        layerWindow->setWantsToBeOnActiveScreen(true);
        return;
    }

    layerWindow->setWantsToBeOnActiveScreen(false);
    if (preference == QLatin1String("primary")) {
        window->setScreen(QGuiApplication::primaryScreen());
        return;
    }
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        if (s->name().compare(preference, Qt::CaseInsensitive) == 0) {
            window->setScreen(s);
            return;
        }
    }
    /* Named output not present (unplugged, or a typo) — fall back rather than
       leaving the surface on no screen at all. */
    qWarning("glassosd: output '%s' not found; following the active screen",
             qPrintable(preference));
    layerWindow->setWantsToBeOnActiveScreen(true);
}

void Surface::setKeyboardFocus(QQuickWindow *window, bool on)
{
    if (!window) {
        return;
    }
    if (auto *layerWindow = LayerShellQt::Window::get(window)) {
        layerWindow->setKeyboardInteractivity(on
            ? LayerShellQt::Window::KeyboardInteractivityOnDemand
            : LayerShellQt::Window::KeyboardInteractivityNone);
    }
}

void Surface::setInputFollowsPanels(QQuickWindow *window, bool on)
{
    if (!window) {
        return;
    }
    if (on) {
        m_maskedWindows.insert(window);
        recompute(window);
    } else {
        m_maskedWindows.remove(window);
        window->setMask(QRegion());
    }
}

void Surface::recompute(QQuickWindow *window)
{
    if (!window || !blurAvailable()) {
        return;
    }
    const auto it = m_regions.constFind(window);
    if (it == m_regions.constEnd()) {
        return;
    }
    /* The effect region is inset one pixel from the painted surface.

       The card's rounded edge is anti-aliased, so its outermost pixel is only
       partly opaque, while the effect region is a hard-edged QRegion covering
       it fully — with saturation above 1.0 the backdrop showing through that
       pixel fringes visibly, cyan over a blue banner.

       The inset has to be applied to the *rectangle*, before the rounded
       region is derived from it. Insetting the finished region instead
       deletes the corners outright: roundedRegion builds them from
       one-pixel-tall strips, and adjusting each by a pixel vertically
       collapses every strip to zero height. That took the whole rounded top
       off the blur region and left a hard horizontal seam across the card,
       sharp above and blurred below. */
    QRegion combined;
    QRegion effectRegion;
    for (const Contribution &c : *it) {
        const QRect r = c.rect.toAlignedRect();
        const int radius = qRound(c.radius);
        combined += roundedRegion(r, radius);
        /* Two pixels, not one. The region's rounded corners are built from
           integer strips, so its boundary is stair-stepped; the card's own
           corner is anti-aliased over roughly two pixels and is not opaque
           enough there to hide a stepped edge underneath it. At one pixel the
           steps show as visible jaggies on the corner. */
        const QRect inner = r.adjusted(2, 2, -2, -2);
        if (inner.isValid()) {
            effectRegion += roundedRegion(inner, radius);
        }
    }
    if (effectRegion.isEmpty()) {
        effectRegion = combined;
    }
    if (combined.isEmpty()) {
        return;
    }
    if (m_maskedWindows.contains(window)) {
        qDebug("glassosd: mask for %p -> %d rect(s), bounds %d,%d %dx%d (window %dx%d)",
               (void *)window, int(combined.rectCount()),
               combined.boundingRect().x(), combined.boundingRect().y(),
               combined.boundingRect().width(), combined.boundingRect().height(),
               window->width(), window->height());
        /* Grown slightly so the rounded corners stay comfortably clickable. */
        const QRegion mask = combined.boundingRect().isEmpty() ? QRegion() : combined;
        if (mask != m_lastMask.value(window)) {
            m_lastMask[window] = mask;
            window->setMask(mask);
            /* Qt only forwards the mask to wl_surface.set_input_region on a
               commit. Without nudging one, the region is sent once when the
               window first appears and never updated — measured in a protocol
               trace as exactly one set_input_region call for the whole
               session, while the blur region beside it updated every time.
               The input region then describes the first card's geometry
               forever: later cards cannot be clicked, and the stale rects go
               on swallowing clicks in the gap where nothing is drawn. */
            window->requestUpdate();
        }
    }
    KWindowEffects::enableBlurBehind(window, true, effectRegion);
    KWindowEffects::enableBackgroundContrast(window, true,
                                             m_contrast, m_intensity, m_saturation,
                                             effectRegion);
}

void Surface::clearBlur(QQuickWindow *window)
{
    if (window) {
        KWindowEffects::enableBlurBehind(window, false);
    }
}

void Surface::applyContrast(QQuickWindow *window,
                            const QRectF &rect,
                            qreal radius,
                            qreal contrast,
                            qreal intensity,
                            qreal saturation)
{
    /* Remember these so recompute() can reapply contrast with the combined
       region without every caller having to pass them again. */
    m_contrast = contrast;
    m_intensity = intensity;
    m_saturation = saturation;
    if (!window || !blurAvailable()) {
        return;
    }
    const QRect logical = rect.toAlignedRect();
    if (logical.isEmpty()) {
        return;
    }
    KWindowEffects::enableBackgroundContrast(window,
                                             true,
                                             contrast,
                                             intensity,
                                             saturation,
                                             roundedRegion(logical, qRound(radius)));
}

void Surface::clearContrast(QQuickWindow *window)
{
    if (window) {
        KWindowEffects::enableBackgroundContrast(window, false);
    }
}
