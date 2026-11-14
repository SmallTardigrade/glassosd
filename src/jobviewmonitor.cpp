/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "jobviewmonitor.h"

#include <QDebug>
#include <QSocketNotifier>
#include <QSet>

#include <dbus/dbus.h>

namespace
{
/* The calls are addressed to plasmashell's unique name, not to a name we could
   match on, so the interface is what selects them. The reply has no interface
   at all; plasmashell's replies are the narrowest rule that still contains it,
   and the filter below discards all but the ones that answer a requestView. */
constexpr const char *kRequestRule = "type='method_call',interface='org.kde.JobViewServerV2',member='requestView'";
constexpr const char *kJobRule = "type='method_call',interface='org.kde.JobViewV3'";
constexpr const char *kLauncherRule = "type='signal',interface='com.canonical.Unity.LauncherEntry',member='Update'";
constexpr const char *kReplyRule = "type='method_return',sender='org.kde.JobViewServer'";

/* The keys update() is read for. Anything else a client puts in the dictionary
   is dropped on the way in, so what we hold is bounded by this list and not by
   what some session client chooses to send. */
const QSet<QString> &wantedKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("title"),          QStringLiteral("percent"),        QStringLiteral("processedBytes"),
        QStringLiteral("totalBytes"),     QStringLiteral("speed"),          QStringLiteral("descriptionValue1"),
        QStringLiteral("descriptionValue2"), QStringLiteral("descriptionLabel1"), QStringLiteral("descriptionLabel2"),
        QStringLiteral("destUrl"),        QStringLiteral("processedFiles"), QStringLiteral("totalFiles"),
        QStringLiteral("elapsedTime"),    QStringLiteral("suspended"),      QStringLiteral("infoMessage"),
    };
    return keys;
}

const QSet<QString> &launcherKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("progress"),
        QStringLiteral("progress-visible"),
        QStringLiteral("proxied-for"),
    };
    return keys;
}

constexpr int kMaxString = 512;

QVariant basic(DBusMessageIter *iter)
{
    switch (dbus_message_iter_get_arg_type(iter)) {
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH: {
        const char *v = nullptr;
        dbus_message_iter_get_basic(iter, &v);
        return QString::fromUtf8(v ? v : "").left(kMaxString);
    }
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t v = 0;
        dbus_message_iter_get_basic(iter, &v);
        return qulonglong(v);
    }
    case DBUS_TYPE_INT32: {
        dbus_int32_t v = 0;
        dbus_message_iter_get_basic(iter, &v);
        return qlonglong(v);
    }
    case DBUS_TYPE_UINT64: {
        dbus_uint64_t v = 0;
        dbus_message_iter_get_basic(iter, &v);
        return qulonglong(v);
    }
    case DBUS_TYPE_INT64: {
        dbus_int64_t v = 0;
        dbus_message_iter_get_basic(iter, &v);
        return qlonglong(v);
    }
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t v = FALSE;
        dbus_message_iter_get_basic(iter, &v);
        return bool(v);
    }
    case DBUS_TYPE_DOUBLE: {
        double v = 0;
        dbus_message_iter_get_basic(iter, &v);
        return v;
    }
    default:
        return QVariant();
    }
}

/* An a{sv} at the iterator, reduced to the keys we read. */
QVariantMap readProps(DBusMessageIter *iter, const QSet<QString> &wanted = wantedKeys())
{
    QVariantMap out;
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY) {
        return out;
    }
    DBusMessageIter arr;
    dbus_message_iter_recurse(iter, &arr);
    int guard = 0;
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY && guard++ < 256) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&arr, &entry);
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
            const char *k = nullptr;
            dbus_message_iter_get_basic(&entry, &k);
            const QString key = QString::fromUtf8(k ? k : "");
            if (wanted.contains(key) && dbus_message_iter_next(&entry)
                && dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
                DBusMessageIter var;
                dbus_message_iter_recurse(&entry, &var);
                const QVariant v = basic(&var);
                if (v.isValid()) {
                    out.insert(key, v);
                }
            }
        }
        dbus_message_iter_next(&arr);
    }
    return out;
}

QString str(const char *s)
{
    return QString::fromUtf8(s ? s : "").left(kMaxString);
}

DBusHandlerResult handleMessage(DBusConnection *, DBusMessage *msg, void *user)
{
    auto *self = static_cast<JobViewMonitor *>(user);
    const int type = dbus_message_get_type(msg);
    DBusMessageIter iter;
    const bool hasArgs = dbus_message_iter_init(msg, &iter);

    if (type == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        const char *member = dbus_message_get_member(msg);
        if (!member) {
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (dbus_message_has_interface(msg, "org.kde.JobViewServerV2") && !qstrcmp(member, "requestView")) {
            if (hasArgs && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
                self->onRequestView(str(dbus_message_get_sender(msg)), dbus_message_get_serial(msg),
                                    basic(&iter).toString());
            }
        } else if (dbus_message_has_interface(msg, "org.kde.JobViewV3")) {
            const QString path = str(dbus_message_get_path(msg));
            if (!qstrcmp(member, "update") && hasArgs) {
                self->onUpdate(path, readProps(&iter));
            } else if (!qstrcmp(member, "terminate") && hasArgs
                       && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_UINT32) {
                const quint32 code = basic(&iter).toUInt();
                QString message;
                if (dbus_message_iter_next(&iter) && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
                    message = basic(&iter).toString();
                }
                self->onTerminate(path, code, message);
            }
        }
    } else if (type == DBUS_MESSAGE_TYPE_SIGNAL) {
        if (hasArgs && dbus_message_has_interface(msg, "com.canonical.Unity.LauncherEntry")
            && dbus_message_has_member(msg, "Update") && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
            const QString uri = basic(&iter).toString();
            if (dbus_message_iter_next(&iter)) {
                self->onLauncherEntry(str(dbus_message_get_sender(msg)), uri, readProps(&iter, launcherKeys()));
            }
        }
    } else if (type == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        if (hasArgs && dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH) {
            self->onRequestReply(str(dbus_message_get_destination(msg)), dbus_message_get_reply_serial(msg),
                                 basic(&iter).toString());
        }
    }

    /* A monitor may not reply to anything, so every message is consumed. */
    return DBUS_HANDLER_RESULT_HANDLED;
}
} // namespace

JobViewMonitor::JobViewMonitor(QObject *parent)
    : QObject(parent)
{
}

JobViewMonitor::~JobViewMonitor()
{
    if (m_conn) {
        dbus_connection_remove_filter(m_conn, &handleMessage, this);
        dbus_connection_close(m_conn);
        dbus_connection_unref(m_conn);
    }
}

bool JobViewMonitor::start()
{
    DBusError err;
    dbus_error_init(&err);

    /* Private, because BecomeMonitor makes the connection receive-only. */
    m_conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        qWarning("glassosd: job progress: cannot open session bus: %s", err.message);
        dbus_error_free(&err);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(m_conn, FALSE);

    DBusMessage *call = dbus_message_new_method_call("org.freedesktop.DBus",
                                                     "/org/freedesktop/DBus",
                                                     "org.freedesktop.DBus.Monitoring",
                                                     "BecomeMonitor");
    const char *rules[] = {kRequestRule, kJobRule, kReplyRule, kLauncherRule};
    DBusMessageIter args;
    dbus_message_iter_init_append(call, &args);
    DBusMessageIter arr;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &arr);
    for (const char *rule : rules) {
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &rule);
    }
    dbus_message_iter_close_container(&args, &arr);
    dbus_uint32_t flags = 0;
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &flags);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_conn, call, 2000, &err);
    dbus_message_unref(call);
    if (dbus_error_is_set(&err)) {
        qWarning("glassosd: job progress: BecomeMonitor refused: %s", err.message);
        dbus_error_free(&err);
        return false;
    }
    if (reply) {
        dbus_message_unref(reply);
    }

    if (!dbus_connection_add_filter(m_conn, &handleMessage, this, nullptr)) {
        qWarning("glassosd: job progress: could not install dbus filter");
        return false;
    }

    int fd = -1;
    if (!dbus_connection_get_unix_fd(m_conn, &fd) || fd < 0) {
        qWarning("glassosd: job progress: no usable fd for the monitor connection");
        return false;
    }
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &JobViewMonitor::pump);

    pump();
    return true;
}

void JobViewMonitor::pump()
{
    dbus_connection_read_write(m_conn, 0);
    while (dbus_connection_dispatch(m_conn) == DBUS_DISPATCH_DATA_REMAINS) {
    }
}

void JobViewMonitor::onRequestView(const QString &caller, quint32 serial, const QString &appId)
{
    if (caller.isEmpty()) {
        return;
    }
    /* A client that asks and never gets a reply (or a hostile one that asks
       forever) must not grow this without limit. Dropping everything is blunt,
       and right: a pending entry is only ever a few milliseconds old. */
    if (m_pending.size() >= maxPending) {
        m_pending.clear();
    }
    m_pending.insert({caller, serial}, appId);
}

void JobViewMonitor::onRequestReply(const QString &caller, quint32 replySerial, const QString &path)
{
    const auto key = qMakePair(caller, replySerial);
    auto it = m_pending.find(key);
    if (it == m_pending.end()) {
        return; // some other reply of plasmashell's
    }
    const QString appId = it.value();
    m_pending.erase(it);

    if (m_open.size() >= maxJobs) {
        return;
    }
    m_open.insert(path, true);
    Q_EMIT jobStarted(path, appId);
}

void JobViewMonitor::onUpdate(const QString &path, const QVariantMap &props)
{
    if (m_open.contains(path)) {
        Q_EMIT jobUpdated(path, props);
    }
}

void JobViewMonitor::onTerminate(const QString &path, quint32 code, const QString &message)
{
    if (m_open.remove(path)) {
        Q_EMIT jobEnded(path, code, message);
    }
}

void JobViewMonitor::onLauncherEntry(const QString &sender, const QString &appUri, const QVariantMap &props)
{
    /* plasmashell re-emits every KIO job as a launcher entry, marked as proxied
       for kuiserver. Those are already shown from the job itself. */
    if (props.contains(QStringLiteral("proxied-for"))) {
        return;
    }
    if (!props.contains(QStringLiteral("progress")) && !props.contains(QStringLiteral("progress-visible"))) {
        return;
    }
    const QString path = QStringLiteral("launcher:%1:%2").arg(sender, appUri);
    const bool visible = props.value(QStringLiteral("progress-visible"), m_launcher.contains(path)).toBool();
    const double fraction = qBound(0.0, props.value(QStringLiteral("progress"), m_launcher.value(path)).toDouble(), 1.0);

    if (!visible) {
        if (m_launcher.contains(path)) {
            /* Done if it got to the end, otherwise it was cleared or abandoned. */
            const bool completed = m_launcher.value(path) >= 0.99;
            m_launcher.remove(path);
            if (m_open.remove(path)) {
                Q_EMIT jobEnded(path, completed ? 0 : 1, QString());
            }
        }
        return;
    }

    if (!m_launcher.contains(path)) {
        if (m_open.size() >= maxJobs) {
            return;
        }
        QString id = appUri;
        id.remove(QStringLiteral("application://"));
        if (id.endsWith(QStringLiteral(".desktop"))) {
            id.chop(8);
        }
        m_open.insert(path, true);
        Q_EMIT jobStarted(path, id);
    }
    m_launcher.insert(path, fraction);
    Q_EMIT jobUpdated(path, {{QStringLiteral("percent"), int(fraction * 100 + 0.5)}});
}
