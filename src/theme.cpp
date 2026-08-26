/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "theme.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace
{
/* Reference loops in a hand-edited file should degrade to the fallback, not
   hang the daemon. */
constexpr int kMaxRefDepth = 8;

/* rgba(20, 25, 25, 0.5) — the form every swaync theme is written in. Qt's
   own colour parser does not accept it, and asking people to convert their
   palette to #rrggbbaa by hand is exactly the friction we are removing. */
QColor parseColor(const QString &spec)
{
    const QString s = spec.trimmed();

    static const QRegularExpression rgba(
        QStringLiteral("^rgba?\\(\\s*([0-9.]+)\\s*,\\s*([0-9.]+)\\s*,\\s*([0-9.]+)\\s*"
                       "(?:,\\s*([0-9.]+)\\s*)?\\)$"),
        QRegularExpression::CaseInsensitiveOption);

    const auto m = rgba.match(s);
    if (m.hasMatch()) {
        const int r = qBound(0, int(m.captured(1).toDouble()), 255);
        const int g = qBound(0, int(m.captured(2).toDouble()), 255);
        const int b = qBound(0, int(m.captured(3).toDouble()), 255);
        /* CSS alpha is 0..1; accept 0..255 too, since people do write it. */
        qreal a = m.captured(4).isEmpty() ? 1.0 : m.captured(4).toDouble();
        if (a > 1.0) {
            a /= 255.0;
        }
        return QColor::fromRgbF(r / 255.0, g / 255.0, b / 255.0, qBound(0.0, a, 1.0));
    }

    const QColor c(s);
    return c.isValid() ? c : QColor();
}
} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    /* Editors that write via rename break a plain path watch, so re-arm on
       every event and debounce — a save can fire several times. */
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        QTimer::singleShot(80, this, [this, path] {
            watchFile(path);
            reload();
        });
    });
    reload();
}

QStringList Theme::searchPaths()
{
    QStringList paths;
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    paths << cfg + QStringLiteral("/glassosd/themes");
    for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        paths << d + QStringLiteral("/glassosd/themes");
    }
    return paths;
}

void Theme::watchFile(const QString &path)
{
    if (!m_watcher->files().contains(path) && QFile::exists(path)) {
        m_watcher->addPath(path);
    }
}

void Theme::reload()
{
    m_values = {};
    m_name.clear();
    m_path.clear();
    m_loaded = false;

    auto cfg = KSharedConfig::openConfig(QStringLiteral("glassosdrc"));
    cfg->reparseConfiguration();
    const KConfigGroup g(cfg, QStringLiteral("Appearance"));
    const QString requested = g.readEntry("ThemeFile", QString());

    QStringList candidates;
    if (!requested.isEmpty()) {
        /* An absolute path is taken literally so a theme can live in a
           dotfiles repo without being copied into place. */
        if (QFileInfo(requested).isAbsolute()) {
            candidates << requested;
        } else {
            for (const QString &dir : searchPaths()) {
                candidates << dir + QLatin1Char('/') + requested + QStringLiteral(".json");
                candidates << dir + QLatin1Char('/') + requested;
            }
        }
    } else {
        /* No ThemeFile set: an unnamed theme.json is still honoured, so
           dropping one file in place is enough to retheme. */
        const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        candidates << cfgDir + QStringLiteral("/glassosd/theme.json");
    }

    for (const QString &path : std::as_const(candidates)) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError) {
            /* Name the file and the offset. A theme that silently does
               nothing is the single most annoying failure mode here. */
            qWarning("glassosd: %s is not valid JSON at offset %d: %s",
                     qUtf8Printable(path), err.offset, qUtf8Printable(err.errorString()));
            continue;
        }
        if (!doc.isObject()) {
            qWarning("glassosd: %s must contain a JSON object", qUtf8Printable(path));
            continue;
        }
        m_values = doc.object();
        m_path = path;
        m_name = QFileInfo(path).completeBaseName();
        m_loaded = true;
        watchFile(path);
        qInfo("glassosd: theme '%s' loaded from %s (%lld keys)",
              qUtf8Printable(m_name), qUtf8Printable(path),
              static_cast<long long>(m_values.size()));
        break;
    }

    ++m_revision;
    Q_EMIT changed();
}

QJsonValue Theme::lookup(const QString &key, int depth) const
{
    if (m_values.isEmpty() || depth > kMaxRefDepth) {
        return {};
    }

    QJsonValue v;
    /* Flat "a.b" wins over nested {a:{b:…}} so a theme can override one leaf
       without restating the branch it lives in. */
    if (m_values.contains(key)) {
        v = m_values.value(key);
    } else {
        const QStringList parts = key.split(QLatin1Char('.'));
        QJsonValue cur = m_values;
        for (const QString &p : parts) {
            if (!cur.isObject()) {
                return {};
            }
            const QJsonObject o = cur.toObject();
            if (!o.contains(p)) {
                return {};
            }
            cur = o.value(p);
        }
        v = cur;
    }

    if (v.isString()) {
        const QString s = v.toString();
        if (s.startsWith(QLatin1Char('@')) && s.size() > 1) {
            return lookup(s.mid(1), depth + 1);
        }
    }
    return v;
}

QColor Theme::color(const QString &key, const QColor &fallback) const
{
    const QJsonValue v = lookup(key);
    if (!v.isString()) {
        return fallback;
    }
    const QColor c = parseColor(v.toString());
    if (!c.isValid()) {
        qWarning("glassosd: theme key '%s' is not a colour: %s",
                 qUtf8Printable(key), qUtf8Printable(v.toString()));
        return fallback;
    }
    return c;
}

qreal Theme::num(const QString &key, qreal fallback) const
{
    const QJsonValue v = lookup(key);
    return v.isDouble() ? v.toDouble() : fallback;
}

QString Theme::str(const QString &key, const QString &fallback) const
{
    const QJsonValue v = lookup(key);
    return v.isString() ? v.toString() : fallback;
}

bool Theme::flag(const QString &key, bool fallback) const
{
    const QJsonValue v = lookup(key);
    return v.isBool() ? v.toBool() : fallback;
}
