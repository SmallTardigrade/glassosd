/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "fnlockwatcher.h"

#include <QDebug>
#include <QDir>
#include <QFile>

namespace
{
constexpr int kPollMs = 400;
}

FnLockWatcher::FnLockWatcher(QObject *parent)
    : QObject(parent)
    , m_path(findPath())
{
    if (m_path.isEmpty()) {
        qWarning("glassosd: no fn_lock attribute found; Fn lock OSD disabled");
        return;
    }

    m_state = read();
    qWarning("glassosd: fn_lock at %s (locked=%s)", qPrintable(m_path), m_state ? "yes" : "no");

    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        const bool now = read();
        if (now != m_state) {
            m_state = now;
            Q_EMIT lockChanged(now);
        }
    });
    m_timer.start();
}

QString FnLockWatcher::findPath()
{
    /* The platform-device symlink is stable across boots; the full PCI path
       under /sys/devices is not. Glob rather than hardcode VPC2004 so this
       still works on other Lenovo models. */
    QDir dir(QStringLiteral("/sys/bus/platform/devices"));
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : entries) {
        const QString candidate = dir.filePath(e) + QStringLiteral("/fn_lock");
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool FnLockWatcher::read() const
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return m_state;
    }
    return f.readAll().trimmed() == "1";
}
