/*
    SPDX-FileCopyrightText: 2014 Martin Klapetek <mklapetek@kde.org>
    SPDX-FileCopyrightText: 2024 Jakob Petsovits <jpetso@petsovits.com>
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Translates raw org.kde.osdService calls into what a surface should show.

    This logic is a deliberate port of Osd::volumeChanged() and friends from
    plasma-workspace shell/osd.cpp (read in full at tag v6.7.4). It has to live
    here because the monitor only ever sees the *arguments* of those calls —
    icon selection and the human-readable strings are computed inside
    plasmashell and only surface on the osdProgress/osdText signals, which stop
    firing once the native OSD is disabled.
*/
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

class OsdModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(int value READ value NOTIFY changed)
    Q_PROPERTY(int maxValue READ maxValue NOTIFY changed)
    Q_PROPERTY(bool showingProgress READ showingProgress NOTIFY changed)
    Q_PROPERTY(bool iconDimmed READ iconDimmed NOTIFY changed)
    Q_PROPERTY(bool iconAccent READ iconAccent NOTIFY changed)

public:
    explicit OsdModel(QObject *parent = nullptr);

    bool active() const { return m_active; }
    QString iconName() const { return m_icon; }
    QString text() const { return m_text; }
    int value() const { return m_value; }
    int maxValue() const { return m_maxValue; }
    bool showingProgress() const { return m_showingProgress; }
    bool iconDimmed() const { return m_iconDimmed; }
    bool iconAccent() const { return m_iconAccent; }

public Q_SLOTS:
    void onOsdCall(const QString &member, const QVariantList &args);
    void onLockChanged(int key, bool locked);
    void onFnLockChanged(bool locked);
    void showProgress(const QString &icon, int value, int maxValue, const QString &extra = {});
    void showText(const QString &icon, const QString &text, bool dimIcon = false, bool accentIcon = false);
    void hide();

Q_SIGNALS:
    void changed();

private:
    void bump();

    QTimer m_timer;
    QString m_icon;
    QString m_text;
    int m_value = 0;
    int m_maxValue = 100;
    bool m_showingProgress = false;
    bool m_iconDimmed = false;
    bool m_iconAccent = false;
    bool m_active = false;
};
