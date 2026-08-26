/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Quiet while something is holding the screen awake.

    The roadmap asked for "cover fullscreen too", after screen sharing. On
    Wayland that is not available to us: a client cannot see another window's
    state, by design, and KWin implements no foreign-toplevel protocol — which
    is why dunst logs that it cannot do fullscreen detection here either. The
    only route to literal fullscreen on Plasma is a KWin script installed into
    the compositor, which is Plasma-only and a separate moving part to ship.

    What is available, and is arguably the better signal: applications already
    say "do not blank the screen, somebody is watching this". Video players,
    games and presentation tools all do it through
    org.freedesktop.PowerManagement.Inhibit, and it is both queryable and
    change-signalled, so there is nothing to poll.

    It is not the same as fullscreen and this file does not pretend otherwise.
    A long download can hold the screen awake without being fullscreen, and a
    fullscreen text editor holds nothing. But "an application says the user is
    watching something" is closer to the question a notification daemon is
    actually asking than "is a window the size of the screen".
*/
#pragma once

#include <QObject>

class QDBusInterface;

class BusyWatcher : public QObject
{
    Q_OBJECT
public:
    explicit BusyWatcher(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    /* Off unless asked for: silencing notifications because a video is playing
       is a reasonable thing to want and a surprising thing to be given. */
    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }

Q_SIGNALS:
    void changed(bool quiet);

private Q_SLOTS:
    void refresh();

private:
    void setBusy(bool busy);

    QDBusInterface *m_iface = nullptr;
    bool m_busy = false;
    bool m_enabled = false;
};
