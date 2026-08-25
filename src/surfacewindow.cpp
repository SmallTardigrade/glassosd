/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "surfacewindow.h"

#include <cmath>

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
    /* Clamp to what the rectangle can actually hold. The corner ellipses are
       2*radius across, so a radius larger than half the shorter side put them
       outside the rect and the region came out *bigger* than the thing it was
       describing — as a blur or input area extending past the surface. It
       never showed up because every caller had been a card, where the radius
       is a fraction of the height; a short strip is the first caller where
       the difference is the whole shape. */
    radius = qMin(radius, qMin(r.width(), r.height()) / 2);
    if (radius <= 0) {
        return QRegion(r);
    }

    /* The corners are built a scanline at a time and rounded so the region is
       strictly *inscribed* — never a pixel outside the rounded rectangle the
       card actually paints.

       This is the whole point, and it is why QRegion::Ellipse is not used
       here. A region is integer rectangles by definition, so its corner is a
       staircase whatever we do; the question is only which side of the true
       arc the steps fall on. Ellipse approximates the curve, so the staircase
       crosses it and lands outside on some rows. Those rows are backdrop that
       KWin blurs and contrast-adjusts while the card does not paint over
       them, so against a coloured background they show up as a stepped halo
       clinging to the corner — the corner reads as pixelated even though Qt
       drew the card's own arc perfectly smoothly.

       Inscribed, every step is underneath the card. What is left is a thin
       crescent at the very edge that is painted but not blurred, and that
       falls under the card's border stroke, where a one-pixel difference in
       backdrop sharpness is not something the eye has anything to compare
       against.

       Note this cannot be fixed by making the region smoother: it is handed
       over in logical pixels, so on a 1.75x display each step is 1.75 device
       pixels whatever we compute. Putting the steps on the right side of the
       arc is the only lever there is. */
    QRegion region(r.adjusted(0, radius, 0, -radius));
    const qreal rr = qreal(radius) * radius;
    for (int i = 0; i < radius; ++i) {
        /* Row centres, so a row counts as covered when its middle is inside
           the arc rather than when its top edge is. */
        const qreal dy = radius - i - 0.5;
        const qreal half = std::sqrt(qMax(qreal(0), rr - dy * dy));
        const int inset = int(std::ceil(radius - half));
        const int w = r.width() - 2 * inset;
        if (w <= 0) {
            continue;
        }
        region += QRect(r.left() + inset, r.top() + i, w, 1);
        region += QRect(r.left() + inset, r.bottom() - i, w, 1);
    }
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

void Surface::setPosition(QQuickWindow *window,
                          int anchors,
                          int marginTop,
                          int marginRight,
                          int marginBottom,
                          int marginLeft)
{
    if (!window) {
        return;
    }
    auto *layerWindow = LayerShellQt::Window::get(window);
    if (!layerWindow) {
        return;
    }

    layerWindow->setAnchors(static_cast<LayerShellQt::Window::Anchors>(anchors));
    layerWindow->setMargins(QMargins(marginLeft, marginTop, marginRight, marginBottom));

    /* Anchors and margins are sent with the next surface commit, and a window
       that is currently hidden — which the popup stack is, most of the time —
       will not commit until it has something to show. Asking for an update
       makes the move take effect now rather than on the next notification,
       which otherwise arrives in the old corner. */
    if (window->isVisible()) {
        window->requestUpdate();
    }
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
    /* The effect region matches the painted surface exactly — no inset.

       wl_region is integer rectangles by definition, so the region's rounded
       corners are stair-stepped and no antialiasing is available at the
       protocol level. Where that step lands is therefore the only thing we
       control. Insetting pushes it *inside* the card, where a translucent
       theme shows it plainly as jaggies on the corner. Matching the card
       exactly puts it at the edge, underneath the card's own anti-aliased
       falloff, which is the best available hiding place.

       The coloured fringe this inset was originally added to fix came from
       saturation above 1.0 boosting the backdrop in that same edge pixel.
       That belongs to the theme's blur.saturation, not to the region. */
    QRegion combined;
    for (const Contribution &c : *it) {
        combined += roundedRegion(c.rect.toAlignedRect(), qRound(c.radius));
    }
    const QRegion &effectRegion = combined;
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
