/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Watches the method calls Plasma's components make to org.kde.osdService.

    Plasma's own OSD is silenced via plasmarc [OSD] Enabled=false. That makes
    Osd::init() return false, so showProgress()/showText() return *before*
    their Q_EMIT — the osdProgress/osdText signals stop firing entirely.
    Verified empirically: with the OSD enabled a volume nudge produced 2 method
    calls and 2 signals; with it disabled, 2 method calls and 0 signals.

    The calls themselves keep flowing, because they originate in kded6 (the
    audioshortcutsservice module) and PowerDevil, separate processes with no
    knowledge of plasmashell's OSD setting. So we eavesdrop on the calls.

    QtDBus has no BecomeMonitor or message-filter API, hence raw libdbus-1 on a
    private connection, pumped from the Qt event loop via QSocketNotifier.
*/
#pragma once

#include <QObject>
#include <QVariantList>

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
class QSocketNotifier;

class OsdMonitor : public QObject
{
    Q_OBJECT
public:
    explicit OsdMonitor(QObject *parent = nullptr);
    ~OsdMonitor() override;

    /* Returns false if the bus refused BecomeMonitor, in which case the caller
       should fall back to the direct sources (PipeWire + org.kde.ScreenBrightness). */
    bool start();

Q_SIGNALS:
    /* member is e.g. "volumeChanged"; args are the raw call arguments. */
    void osdCall(const QString &member, const QVariantList &args);

public:
    /* Called from the libdbus filter. */
    void dispatchCall(const QString &member, const QVariantList &args) { Q_EMIT osdCall(member, args); }

private:
    void pump();

    /* The libdbus filter lives as a free function in the .cpp so that
       <dbus/dbus.h> and its DBusHandlerResult enum stay out of this header. */
    friend class OsdMonitorFilter;

    DBusConnection *m_conn = nullptr;
    QSocketNotifier *m_notifier = nullptr;
};
