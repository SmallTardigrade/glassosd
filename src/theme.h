/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    A JSON theme file, in the spirit of swaync's style.css.

    QML cannot consume GTK CSS, so this is not a CSS parser. What it is, is the
    part of a swaync style.css that people actually edit: the @define-color
    block at the top, plus the radii and the font. Every literal in Style.qml
    is looked up here first and falls back to its built-in value, so a theme
    file overrides only the keys it names — the same partial-override model CSS
    gives you, without pretending to be a cascade.

    Keys are dotted paths into the JSON, so both of these work:

        { "text": { "primary": "#f4f6f8" } }
        { "text.primary": "#f4f6f8" }

    Values may reference other keys with a leading '@', matching the way
    @define-color values get reused in a swaync stylesheet:

        { "accent": "#3daee9", "slider.fill": "@accent" }
*/
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>

class QFileSystemWatcher;

class Theme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /* The name of the loaded theme, or an empty string when no file was
       found and every value is coming from the built-in defaults. */
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);

    QString name() const { return m_name; }
    bool loaded() const { return m_loaded; }

    /* Style.qml calls these for every value. The fallback is the built-in,
       so an absent or partial theme file is not an error condition. */
    Q_INVOKABLE QColor color(const QString &key, const QColor &fallback) const;
    Q_INVOKABLE qreal num(const QString &key, qreal fallback) const;
    Q_INVOKABLE QString str(const QString &key, const QString &fallback) const;
    Q_INVOKABLE bool flag(const QString &key, bool fallback) const;

    /* Where a theme may be installed, most specific first. Exposed so
       `glassosdctl themes` can list what is available without duplicating the
       search order in shell. */
    static QStringList searchPaths();

    void reload();

Q_SIGNALS:
    void changed();

private:
    QJsonValue lookup(const QString &key, int depth = 0) const;
    void watchFile(const QString &path);

    QJsonObject m_values;
    QString m_name;
    QString m_path;
    bool m_loaded = false;
    QFileSystemWatcher *m_watcher = nullptr;
};
