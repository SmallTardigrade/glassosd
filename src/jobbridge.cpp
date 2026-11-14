/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "jobbridge.h"

#include "notification.h"
#include "notificationserver.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>

namespace
{
/* KJob::KilledJobError. A job ends with this when the person stopped it — chose
   Cancel, or Skip on an "overwrite?" prompt — which is an answer, not a
   failure, so there is nothing to report. Real errors start at 100. */
constexpr quint32 killedJobError = 1;

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

/* The display name of whatever the job is working on: a path or a URL becomes
   its file name, anything else (an archive member, a label) is shown as sent. */
QString leafName(const QString &value)
{
    if (value.startsWith(QLatin1Char('/'))) {
        return QFileInfo(value).fileName();
    }
    const QUrl url(value);
    if (url.isValid() && !url.scheme().isEmpty() && url.scheme().size() > 1) {
        const QString name = url.fileName();
        return name.isEmpty() ? value : name;
    }
    return value;
}

QString sizeText(qlonglong bytes)
{
    return QLocale::system().formattedDataSize(bytes, 1);
}

/* "34.3 of 78.1 MiB": the unit is said once when both sides share it. */
QString progressText(qlonglong done, qlonglong total)
{
    const QString a = sizeText(done);
    const QString b = sizeText(total);
    const int sa = a.lastIndexOf(QLatin1Char(' '));
    const int sb = b.lastIndexOf(QLatin1Char(' '));
    if (sa > 0 && sb > 0 && a.mid(sa) == b.mid(sb)) {
        return QObject::tr("%1 of %2").arg(a.left(sa), b);
    }
    return QObject::tr("%1 of %2").arg(a, b);
}

/* "12 s", "3 min 20 s", "1 h 05 min": short enough for a line of a card. */
QString durationText(int seconds)
{
    if (seconds < 60) {
        return QObject::tr("%1 s").arg(qMax(1, seconds));
    }
    if (seconds < 600) {
        return QObject::tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60);
    }
    if (seconds < 3600) {
        return QObject::tr("%1 min").arg((seconds + 30) / 60);
    }
    return QObject::tr("%1 h %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60, 2, 10, QLatin1Char('0'));
}

QString filesText(qlonglong n)
{
    return n == 1 ? QObject::tr("1 file") : QObject::tr("%1 files").arg(QLocale::system().toString(n));
}

/* Long names are shortened in the middle, where an extension survives: a name
   with no spaces cannot wrap, so it would otherwise run out of the card. */
QString shortName(const QString &name)
{
    constexpr int keep = 34;
    if (name.size() <= keep) {
        return name;
    }
    const int tail = keep / 3;
    return name.left(keep - tail - 1) + QChar(0x2026) + name.right(tail);
}

/* The folder a job is writing into, shortened, or nothing. */
QString destFolder(const QVariantMap &props)
{
    const QString dest = QUrl(props.value(QStringLiteral("destUrl")).toString()).toLocalFile();
    return shortName(QFileInfo(dest).fileName());
}

/* "Copying" -> "Copied". The verbs KIO uses are fixed English strings in the
   sender; a title this does not recognise (another language, another verb)
   falls back to a wording that needs no grammar. */
QString pastTense(const QString &title)
{
    static const QHash<QString, QString> verbs = {
        {QStringLiteral("Copying"), QStringLiteral("Copied")},
        {QStringLiteral("Moving"), QStringLiteral("Moved")},
        {QStringLiteral("Deleting"), QStringLiteral("Deleted")},
        {QStringLiteral("Extracting"), QStringLiteral("Extracted")},
        {QStringLiteral("Compressing"), QStringLiteral("Compressed")},
        {QStringLiteral("Trashing"), QStringLiteral("Moved to trash")},
        {QStringLiteral("Restoring"), QStringLiteral("Restored")},
        {QStringLiteral("Downloading"), QStringLiteral("Downloaded")},
        {QStringLiteral("Uploading"), QStringLiteral("Uploaded")},
    };
    return verbs.value(title.section(QLatin1Char(' '), 0, 0));
}
} // namespace

JobBridge::JobBridge(NotificationServer *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &JobBridge::flush);
    if (m_server) {
        connect(m_server->model(), &NotificationModel::notificationClosed, this, &JobBridge::notificationClosed);
    }
}

void JobBridge::jobStarted(const QString &path, const QString &appId)
{
    Job job;
    job.appId = appId;
    job.startedMs = nowMs();
    m_jobs.insert(path, job);
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void JobBridge::jobUpdated(const QString &path, const QVariantMap &props)
{
    auto it = m_jobs.find(path);
    if (it == m_jobs.end()) {
        return;
    }
    for (auto p = props.constBegin(); p != props.constEnd(); ++p) {
        it->props.insert(p.key(), p.value());
    }
    const double speed = props.value(QStringLiteral("speed")).toDouble();
    if (speed > 0) {
        it->smoothSpeed = it->smoothSpeed > 0 ? it->smoothSpeed * 0.8 + speed * 0.2 : speed;
    }
    it->dirty = true;
}

void JobBridge::jobEnded(const QString &path, quint32 code, const QString &message)
{
    auto it = m_jobs.find(path);
    if (it == m_jobs.end()) {
        return;
    }
    Job job = it.value();
    m_jobs.erase(it);
    if (m_jobs.isEmpty()) {
        m_timer.stop();
    }
    if (job.dismissed) {
        return;
    }
    const bool shown = job.notificationId != 0;
    if (code == killedJobError) {
        /* Stopped by the person, so the card has nothing left to say: take it
           away rather than leave a stale progress bar or a false "failed". */
        if (shown) {
            m_server->handleClose(job.notificationId);
        }
        return;
    }
    /* A job that finished before it earned a card never shows one — unless it
       failed, which is worth saying however briefly it ran. */
    if (!shown && code == 0) {
        return;
    }
    present(job, true, code, message);
}

void JobBridge::flush()
{
    const qint64 now = nowMs();
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        Job &job = it.value();
        if (job.dismissed || !job.dirty || now - job.startedMs < m_showAfterMs) {
            continue;
        }
        present(job, false);
    }
}

void JobBridge::notificationClosed(uint id, uint reason)
{
    if (reason == CloseReason::Closed) {
        return;
    }
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        if (it->notificationId == id) {
            it->dismissed = true;
            return;
        }
    }
}

int JobBridge::percentOf(const Job &job)
{
    int percent = job.props.value(QStringLiteral("percent"), -1).toInt();
    const qlonglong total = job.props.value(QStringLiteral("totalBytes")).toLongLong();
    if (percent < 0 && total > 0) {
        percent = int(job.props.value(QStringLiteral("processedBytes")).toLongLong() * 100 / total);
    }
    return qBound(0, percent < 0 ? 0 : percent, 100);
}

int JobBridge::secondsLeft(const Job &job)
{
    const qlonglong total = job.props.value(QStringLiteral("totalBytes")).toLongLong();
    const qlonglong done = job.props.value(QStringLiteral("processedBytes")).toLongLong();
    const qlonglong elapsedMs = job.props.value(QStringLiteral("elapsedTime")).toLongLong();
    const int percent = job.props.value(QStringLiteral("percent"), 0).toInt();
    /* The first seconds of a job say little about the rest of it. */
    if (elapsedMs < 2000 || percent < 1 || percent >= 100) {
        return -1;
    }
    if (total > done && done > 0 && job.smoothSpeed > 0) {
        return int((total - done) / job.smoothSpeed);
    }
    return int(elapsedMs / 1000.0 * (100 - percent) / percent);
}

/* "Copying to Documents": what is happening and where, in the line the eye
   lands on first. */
QString JobBridge::runningSummary(const Job &job)
{
    const QString title = job.props.value(QStringLiteral("title")).toString();
    const QString verb = title.isEmpty() ? QObject::tr("In progress") : title;
    const QString folder = destFolder(job.props);
    return folder.isEmpty() ? verb : QObject::tr("%1 to %2").arg(verb, folder);
}

/* Three short lines, each one thing: what it is on now, how far, how fast. */
QString JobBridge::runningBody(const Job &job)
{
    const QVariantMap &props = job.props;
    QStringList lines;
    const QString name = shortName(leafName(props.value(QStringLiteral("descriptionValue1")).toString()));
    if (!name.isEmpty()) {
        lines << name.toHtmlEscaped();
    }

    const qlonglong total = props.value(QStringLiteral("totalBytes")).toLongLong();
    const qlonglong done = props.value(QStringLiteral("processedBytes")).toLongLong();
    const qlonglong files = props.value(QStringLiteral("processedFiles")).toLongLong();
    const qlonglong totalFiles = props.value(QStringLiteral("totalFiles")).toLongLong();

    QStringList how;
    how << QStringLiteral("%1%").arg(percentOf(job));
    if (total > 0) {
        how << progressText(done, total);
    } else if (files > 0 && totalFiles > 0) {
        how << QObject::tr("%1 of %2").arg(QLocale::system().toString(files), filesText(totalFiles));
    } else if (done > 0) {
        how << sizeText(done);
    }
    lines << how.join(QStringLiteral(" · "));

    QStringList pace;
    if (job.smoothSpeed > 0) {
        pace << QObject::tr("%1/s").arg(sizeText(qlonglong(job.smoothSpeed)));
    }
    const int left = secondsLeft(job);
    if (left >= 0) {
        pace << QObject::tr("%1 left").arg(durationText(left));
    }
    if (!pace.isEmpty()) {
        lines << pace.join(QStringLiteral(" · "));
    }
    return lines.join(QStringLiteral("<br>"));
}

/* "Copied to Documents": the outcome in the title. */
QString JobBridge::finishedSummary(const Job &job)
{
    const QString title = job.props.value(QStringLiteral("title")).toString();
    const QString past = pastTense(title);
    const QString folder = destFolder(job.props);
    if (past.isEmpty()) {
        return title.isEmpty() ? QObject::tr("Finished") : QObject::tr("Finished: %1").arg(title);
    }
    return folder.isEmpty() ? past : QObject::tr("%1 to %2").arg(past, folder);
}

/* How much and how long — not the last file it happened to touch. */
QString JobBridge::finishedBody(const Job &job)
{
    const QVariantMap &props = job.props;
    const qlonglong files = props.value(QStringLiteral("totalFiles")).toLongLong() > 0
        ? props.value(QStringLiteral("totalFiles")).toLongLong()
        : props.value(QStringLiteral("processedFiles")).toLongLong();
    qlonglong bytes = props.value(QStringLiteral("totalBytes")).toLongLong();
    if (bytes <= 0) {
        bytes = props.value(QStringLiteral("processedBytes")).toLongLong();
    }
    const qlonglong elapsedMs = props.value(QStringLiteral("elapsedTime")).toLongLong();

    QStringList what;
    if (files > 0) {
        what << filesText(files);
    }
    if (bytes > 0) {
        what << sizeText(bytes);
    }
    QStringList lines;
    if (!what.isEmpty()) {
        lines << what.join(QStringLiteral(" · "));
    }
    if (elapsedMs >= 1000) {
        lines << QObject::tr("took %1").arg(durationText(int((elapsedMs + 500) / 1000)));
    }
    return lines.join(QStringLiteral("<br>"));
}

void JobBridge::present(Job &job, bool final, quint32 code, const QString &message)
{
    job.dirty = false;

    QString summary = runningSummary(job);
    QString body = final ? QString() : runningBody(job);
    int percent = percentOf(job);

    QVariantMap hints;
    hints.insert(QStringLiteral("transient"), true);
    hints.insert(QStringLiteral("category"), QStringLiteral("transfer"));
    if (!job.appId.isEmpty()) {
        hints.insert(QStringLiteral("desktop-entry"), job.appId);
    }

    int timeout = 0; // a running job's card stays until it ends or is dismissed
    if (final) {
        if (code == 0) {
            percent = 100;
            summary = finishedSummary(job);
            body = finishedBody(job);
            hints.insert(QStringLiteral("category"), QStringLiteral("transfer.complete"));
            timeout = 4000;
        } else {
            const QString title = job.props.value(QStringLiteral("title")).toString();
            summary = title.isEmpty() ? QObject::tr("File operation failed") : QObject::tr("%1 failed").arg(title);
            body = message.isEmpty() ? QObject::tr("It did not finish.") : message.toHtmlEscaped();
            hints.insert(QStringLiteral("category"), QStringLiteral("transfer.error"));
            timeout = -1;
        }
    }
    if (!final || code == 0) {
        hints.insert(QStringLiteral("value"), percent);
    }

    /* The name is left empty on purpose: with a desktop entry the server fills
       in the application's own name ("Dolphin"), where the id would be shown
       as it stands ("ORG.KDE.DOLPHIN"). */
    const uint id = m_server->handleNotify(QString(), job.notificationId, QString(), summary, body, {}, hints, timeout);
    job.notificationId = id;
}
