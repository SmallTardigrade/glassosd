/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    MPRIS2 media player, for the notification centre.

    swaync's control centre can host a player widget and ours could not. This
    watches the session bus for org.mpris.MediaPlayer2.* and drives whichever
    player is currently active.
*/
#pragma once

#include <QDBusServiceWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>

class QDBusInterface;

class MprisController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString identity READ identity NOTIFY changed)

public:
    explicit MprisController(QObject *parent = nullptr);

    bool available() const { return !m_service.isEmpty(); }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString artUrl() const { return m_artUrl; }
    bool playing() const { return m_playing; }
    QString identity() const { return m_identity; }

    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    /* Connected by name from the PropertiesChanged signal, so it has to be a
       real slot rather than a plain member. */
    void refresh();

private:
    void findPlayer();
    void call(const QString &method);

    QDBusServiceWatcher m_watcher;
    QString m_service;
    QString m_title;
    QString m_artist;
    QString m_artUrl;
    QString m_identity;
    bool m_playing = false;
};
