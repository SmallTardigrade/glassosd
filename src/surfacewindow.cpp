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
    /* KWindowEffects::isEffectAvailable() is an X11-atom-era API and returns
       false on Wayland even when blur works perfectly. Verified here: with it
       used as a gate the panel fell back to opaque, and with the gate removed
       KWin blurred the surface correctly through ext_background_effect_v1.
       So never gate on it under Wayland — only trust it on X11. */
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        return true;
    }
    return KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
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
    QRegion combined;
    for (const Contribution &c : *it) {
        combined += roundedRegion(c.rect.toAlignedRect(), qRound(c.radius));
    }
    if (combined.isEmpty()) {
        return;
    }
    if (m_maskedWindows.contains(window)) {
        /* Grown slightly so the rounded corners stay comfortably clickable. */
        window->setMask(combined.boundingRect().isEmpty() ? QRegion() : combined);
    }
    KWindowEffects::enableBlurBehind(window, true, combined);
    KWindowEffects::enableBackgroundContrast(window, true,
                                             m_contrast, m_intensity, m_saturation,
                                             combined);
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
