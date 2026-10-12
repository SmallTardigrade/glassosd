/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/*
    Reading and writing the small JSON files glassosd keeps its state in:
    history.json and snoozed.json. Each is the only copy of what it holds, so
    both operations are careful in the same two ways.

    Writes go to a new file that is renamed over the old one. Rewriting in
    place empties the file first, so a save cut short — power loss, a full
    disk, the session being killed mid-write — left half a file, and the next
    start read nothing from it.

    A file that does not parse is set aside as <name>.corrupt, and said so,
    before anything can write over it. Reading it as empty and carrying on
    meant the next save replaced it, and everything in it was gone without a
    word.
*/
#pragma once

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>

namespace JsonStore
{
/* The array stored at path. A missing file is an empty array, not an error. */
inline QJsonArray read(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error == QJsonParseError::NoError && doc.isArray()) {
        return doc.array();
    }
    /* An empty file is what an interrupted save used to leave behind; there
       is nothing in it to keep. */
    if (bytes.trimmed().isEmpty()) {
        return {};
    }

    const QString aside = path + QStringLiteral(".corrupt");
    QFile::remove(aside);
    const bool kept = QFile::copy(path, aside);
    qWarning("glassosd: %s could not be read (%s at byte %d)%s",
             qPrintable(path),
             error.error == QJsonParseError::NoError ? "not a list" : qPrintable(error.errorString()),
             error.offset,
             kept ? qPrintable(QStringLiteral("; the file has been kept as ") + aside)
                  : "; it could not be copied aside either");
    return {};
}

inline bool write(const QString &path, const QJsonArray &arr)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return f.commit();
}
} // namespace JsonStore
