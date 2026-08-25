/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "controller.h"

#include "historymodel.h"
#include "notificationmodel.h"
#include "osdmodel.h"

#include <QDBusConnection>
#include <QDebug>

Controller::Controller(OsdModel *osd, NotificationModel *notifications, HistoryModel *history, QObject *parent)
    : QObject(parent)
    , m_osd(osd)
    , m_notifications(notifications)
    , m_history(history)
{
}

bool Controller::start()
{
    auto bus = QDBusConnection::sessionBus();
    /* ExportAllSlots rather than a generated adaptor: the interface is small
       and hand-maintaining an adaptor for it buys nothing. */
    if (!bus.registerObject(QStringLiteral("/Control"), this, QDBusConnection::ExportAllSlots)) {
        qWarning("glassosd: could not export /Control on the session bus");
        return false;
    }
    if (!bus.registerService(QStringLiteral("org.glassosd.Daemon"))) {
        qWarning("glassosd: org.glassosd.Daemon is already taken — is another instance running?");
        return false;
    }
    return true;
}

QString Controller::Version() const
{
    return QStringLiteral(GLASSOSD_VERSION);
}

void Controller::ToggleHistory()
{
    m_history->togglePanel();
}

void Controller::ShowHistory()
{
    m_history->setPanelOpen(true);
}

void Controller::HideHistory()
{
    m_history->setPanelOpen(false);
}

bool Controller::ToggleDoNotDisturb()
{
    m_notifications->setDoNotDisturb(!m_notifications->doNotDisturb());
    return m_notifications->doNotDisturb();
}

void Controller::SetDoNotDisturb(bool on)
{
    m_notifications->setDoNotDisturb(on);
}

bool Controller::DoNotDisturb() const
{
    return m_notifications->doNotDisturb();
}

void Controller::VolumeChanged(int percent, int maxPercent)
{
    m_osd->onOsdCall(QStringLiteral("volumeChanged"),
                     {percent, maxPercent > 0 ? maxPercent : 100});
}

void Controller::MicrophoneVolumeChanged(int percent)
{
    m_osd->onOsdCall(QStringLiteral("microphoneVolumeChanged"), {percent});
}

void Controller::BrightnessChanged(int percent)
{
    m_osd->onOsdCall(QStringLiteral("brightnessChanged"), {percent});
}

void Controller::KeyboardBrightnessChanged(int percent)
{
    m_osd->onOsdCall(QStringLiteral("keyboardBrightnessChanged"), {percent});
}

void Controller::ShowText(const QString &icon, const QString &text)
{
    m_osd->showText(icon, text);
}

void Controller::ShowProgress(const QString &icon, int value, int maxValue)
{
    m_osd->showProgress(icon, value, maxValue > 0 ? maxValue : 100);
}

void Controller::Hide()
{
    m_osd->hide();
}

void Controller::Reload()
{
    Q_EMIT reloadRequested();
}
