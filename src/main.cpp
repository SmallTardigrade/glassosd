/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "capslockwatcher.h"
#include "appearance.h"
#include "appsettings.h"
#include "controller.h"
#include "trayicon.h"
#include "historymodel.h"
#include "notificationmodel.h"
#include "notificationserver.h"
#include "notifyimageprovider.h"
#include "fnlockwatcher.h"
#include "iconprovider.h"

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include "osdmodel.h"
#include "osdmonitor.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QQmlApplicationEngine>

#include <KConfigGroup>
#include <KConfigWatcher>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KSharedConfig>

int main(int argc, char *argv[])
{
    /* No LayerShellQt::Shell::useLayerShell() call: deprecated since 6.6 and
       unnecessary since Qt 6.5, which wires the platform integration up on
       demand when a Window is given layer-shell properties. */
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("glassosd"));

    app.setOrganizationName(QStringLiteral("glassosd"));
    app.setQuitOnLastWindowClosed(false); // we are a daemon, not a window

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("glassosd"));

    QQmlApplicationEngine engine;
    /* Phase 2 finding: Fedora's Papirus-Dark inherits breeze-dark, not
       Papirus, so the dunstrc chain never actually reached Papirus. Naming
       plain Papirus as the fallback is what restores third-party app-icon
       coverage for notifications. */
    QIcon::setFallbackThemeName(QStringLiteral("Papirus"));

    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);

    auto *model = engine.singletonInstance<OsdModel *>(QStringLiteral("org.glassosd.ui"),
                                                       QStringLiteral("OsdModel"));

    auto *monitor = new OsdMonitor(&app);
    if (monitor->start()) {
        QObject::connect(monitor, &OsdMonitor::osdCall, model, &OsdModel::onOsdCall);
    } else {
        /* Expected off Plasma: there is no org.kde.osdService to monitor. The
           OSD still works — the compositor's own volume/brightness keybinds
           call glassosdctl, which drives the same OsdModel over D-Bus. */
        qInfo("glassosd: no org.kde.osdService to monitor (normal outside Plasma) — "
              "route your volume/brightness keys through `glassosdctl osd`");
    }

    auto *caps = new CapsLockWatcher(&app);
    QObject::connect(caps, &CapsLockWatcher::lockChanged, model, &OsdModel::onLockChanged);

    auto *notifications = engine.singletonInstance<NotificationModel *>(
        QStringLiteral("org.glassosd.ui"), QStringLiteral("NotificationModel"));

    auto cfg = KSharedConfig::openConfig(QStringLiteral("glassosdrc"));
    KConfigGroup notifyCfg(cfg, QStringLiteral("Notifications"));

    /* Applied on start and again whenever glassosdrc changes on disk, so
       `kwriteconfig6` (or glassosdctl) takes effect immediately instead of
       needing a restart. */
    auto applySettings = [=]() mutable {
        cfg->reparseConfiguration();
        KConfigGroup g(cfg, QStringLiteral("Notifications"));
        notifications->setLimit(g.readEntry("Limit", 4));
        notifications->setIndicateHidden(g.readEntry("IndicateHidden", true));
        notifications->setIdleThresholdMs(g.readEntry("IdleThresholdMs", 120000));
        notifications->setCoalesce(g.readEntry("CoalesceThreshold", 3),
                                   g.readEntry("CoalesceWindowMs", 20000));
        notifications->setDoNotDisturb(g.readEntry("DoNotDisturb", false));
    };
    applySettings();

    auto *history = engine.singletonInstance<HistoryModel *>(
        QStringLiteral("org.glassosd.ui"), QStringLiteral("HistoryModel"));

    engine.addImageProvider(QStringLiteral("notifyimage"),
                            new NotifyImageProvider(notifications, history));

    history->setAutoCollapseOver(notifyCfg.readEntry("AutoCollapseOver", 3));
    history->setCapacity(notifyCfg.readEntry("HistoryLength", 200));

    /* Flush on shutdown so the last few notifications are not lost to the
       debounce window. */
    QObject::connect(&app, &QCoreApplication::aboutToQuit, history, [history] { history->save(); });

    auto *server = new NotificationServer(notifications, history, &app);
    /* On by default now that we ship a D-Bus activation file. An app that
       posts a notification starts us on demand, and a daemon that starts and
       then declines to claim the name would swallow the message that woke it.
       Set [Notifications] Enabled=false to run the OSD only and leave your
       existing notification daemon alone. */
    server->loadRules(cfg);

    /* Settings changes reload the rules in-process; no need to wait for the
       file watcher, which only fires for writes that emit the KConfig notify
       signal. */
    auto *appearance = engine.singletonInstance<Appearance *>(
        QStringLiteral("org.glassosd.ui"), QStringLiteral("Appearance"));

    auto *appSettings = engine.singletonInstance<AppSettings *>(
        QStringLiteral("org.glassosd.ui"), QStringLiteral("AppSettings"));
    QObject::connect(appSettings, &AppSettings::rulesChanged, &app, [=]() mutable {
        server->loadRules(cfg);
    });

    /* Hold the Ptr. KConfigWatcher::create() returns a shared pointer; taking
       .get() off the temporary let it be destroyed at the end of the statement
       and connecting to the dangling pointer segfaulted on startup. */
    KConfigWatcher::Ptr watcher = KConfigWatcher::create(cfg);
    QObject::connect(watcher.get(), &KConfigWatcher::configChanged, &app,
                     [=](const KConfigGroup &, const QByteArrayList &) mutable {
                         applySettings();
                         appearance->reload();
                         server->loadRules(cfg);
                     });
    if (notifyCfg.readEntry("Enabled", true)) {
        server->start();
    } else {
        qInfo("glassosd: notification server disabled by config — OSD only "
              "(set [Notifications] Enabled=true in glassosdrc to serve notifications)");
    }

    /* Do Not Disturb persists: a setting you toggle with a shortcut and then
       forget about is worse than useless if it silently resets at login.
       No feedback loop here — setDoNotDisturb() early-returns when the value
       is unchanged, so a write-then-reload settles immediately. */
    QObject::connect(notifications, &NotificationModel::doNotDisturbChanged, &app, [=]() mutable {
        notifyCfg.writeEntry("DoNotDisturb", notifications->doNotDisturb());
        notifyCfg.sync();
        /* Reuse our own OSD for the confirmation rather than adding a tray
           icon — same surface, same styling, one less moving part. */
        model->showText(QStringLiteral("dnd"),
                        notifications->doNotDisturb()
                            ? i18nc("keep short", "Do Not Disturb On")
                            : i18nc("keep short", "Do Not Disturb Off"),
                        !notifications->doNotDisturb(),
                        notifications->doNotDisturb());
    });

    /* Always available, on every compositor: `glassosdctl` and any keybind a
       wlroots user writes in their own config come in through here. */
    auto *control = new Controller(model, notifications, history, &app);
    control->start();
    QObject::connect(control, &Controller::reloadRequested, &app, [=]() mutable {
        applySettings();
        appearance->reload();
        server->loadRules(cfg);
    });

    /* KGlobalAccel is Plasma-only. Registering against a bus name that is not
       there logs a timeout and leaves a dead QAction behind, so check first —
       on Hyprland/sway the compositor owns the keybinding and calls
       glassosdctl instead. */
    const bool haveKGlobalAccel = QDBusConnection::sessionBus().interface()
        && QDBusConnection::sessionBus().interface()->isServiceRegistered(
               QStringLiteral("org.kde.kglobalaccel"));

    if (haveKGlobalAccel) {
        auto *dndAction = new QAction(i18n("Toggle Do Not Disturb"), &app);
        dndAction->setObjectName(QStringLiteral("toggle_dnd"));
        dndAction->setProperty("componentName", QStringLiteral("glassosd"));
        QObject::connect(dndAction, &QAction::triggered, notifications, [notifications] {
            notifications->setDoNotDisturb(!notifications->doNotDisturb());
        });
        KGlobalAccel::setGlobalShortcut(dndAction, QKeySequence(QStringLiteral("Meta+Shift+N")));

        auto *historyAction = new QAction(i18n("Toggle notification history"), &app);
        historyAction->setObjectName(QStringLiteral("toggle_history"));
        historyAction->setProperty("componentName", QStringLiteral("glassosd"));
        QObject::connect(historyAction, &QAction::triggered, history, [history] {
            history->togglePanel();
        });
        KGlobalAccel::setGlobalShortcut(historyAction, QKeySequence(QStringLiteral("Meta+N")));
    } else {
        qInfo("glassosd: no kglobalaccel on the bus — bind your compositor's keys to "
              "`glassosdctl history` and `glassosdctl dnd toggle` instead");
    }

    new TrayIcon(notifications, history, &app);

    auto *fn = new FnLockWatcher(&app);
    QObject::connect(fn, &FnLockWatcher::lockChanged, model, &OsdModel::onFnLockChanged);

    engine.loadFromModule(QStringLiteral("org.glassosd.ui"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
