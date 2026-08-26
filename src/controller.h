/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    org.glassosd.Control — the compositor-independent way in.

    On Plasma the daemon is driven by KGlobalAccel shortcuts and by monitoring
    org.kde.osdService. Neither of those exists on wlroots compositors, so
    everything they can do is also reachable over plain D-Bus: Hyprland, sway,
    river and friends bind their own keys to `glassosdctl`, which calls in
    here. The rendering path is identical either way — these slots feed the
    same OsdModel that the Plasma monitor feeds.
*/
#pragma once

#include <QObject>

class OsdModel;
class HistoryModel;
class NotificationModel;

class Controller : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.glassosd.Control")

public:
    Controller(OsdModel *osd, NotificationModel *notifications, HistoryModel *history, QObject *parent = nullptr);

    /* Claims org.glassosd.Daemon and exports this object at /Control.
       Returns false if another instance already holds the name. */
    bool start();

public Q_SLOTS:
    QString Version() const;

    int DismissAll();

    void ToggleHistory();
    void ShowHistory();
    /* Open the centre with a search already applied. Scriptable, and the only
       way to drive the search field from outside — which also makes it
       testable without synthesising key presses. */
    void Search(const QString &text);
    void HideHistory();

    bool ToggleDoNotDisturb();
    void SetDoNotDisturb(bool on);
    bool DoNotDisturb() const;

    /* Mirrors the org.kde.osdService member names so the Plasma and non-Plasma
       paths stay one code path. */
    void VolumeChanged(int percent, int maxPercent);
    void MicrophoneVolumeChanged(int percent);
    void BrightnessChanged(int percent);
    void KeyboardBrightnessChanged(int percent);
    void ShowText(const QString &icon, const QString &text);
    void ShowProgress(const QString &icon, int value, int maxValue);
    void Hide();

    /* KConfigWatcher only fires for writes that emit KConfig's change signal.
       Anything editing glassosdrc by hand calls this instead. */
    void Reload();

Q_SIGNALS:
    void reloadRequested();

private:
    OsdModel *m_osd;
    NotificationModel *m_notifications;
    HistoryModel *m_history;
};
