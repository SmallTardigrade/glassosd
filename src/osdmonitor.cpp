/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "osdmonitor.h"

#include <QDebug>
#include <QSocketNotifier>

#include <dbus/dbus.h>

namespace
{
/* Only the interface matters — the calls we want are addressed to
   destination=org.kde.plasmashell rather than to the org.kde.osdService name,
   so matching on destination would miss them. */
constexpr const char *kMatchRule = "interface='org.kde.osdService'";

QVariant extractArg(DBusMessageIter *iter)
{
    switch (dbus_message_iter_get_arg_type(iter)) {
    case DBUS_TYPE_INT32: {
        dbus_int32_t v = 0;
        dbus_message_iter_get_basic(iter, &v);
        return QVariant::fromValue<int>(v);
    }
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t v = 0;
        dbus_message_iter_get_basic(iter, &v);
        return QVariant::fromValue<uint>(v);
    }
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t v = FALSE;
        dbus_message_iter_get_basic(iter, &v);
        return QVariant::fromValue<bool>(v);
    }
    case DBUS_TYPE_STRING: {
        const char *v = nullptr;
        dbus_message_iter_get_basic(iter, &v);
        return QString::fromUtf8(v ? v : "");
    }
    case DBUS_TYPE_STRUCT: {
        /* screenBrightnessChanged carries a QRect as (iiii); we do not use it,
           but it must still be consumed so later arguments stay aligned. */
        QVariantList inner;
        DBusMessageIter sub;
        dbus_message_iter_recurse(iter, &sub);
        while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
            inner << extractArg(&sub);
            dbus_message_iter_next(&sub);
        }
        return inner;
    }
    default:
        return QVariant();
    }
}
} // namespace

namespace
{
DBusHandlerResult handleMessage(DBusConnection *, DBusMessage *msg, void *user)
{
    auto *self = static_cast<OsdMonitor *>(user);

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!dbus_message_has_interface(msg, "org.kde.osdService")) {
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    const char *member = dbus_message_get_member(msg);
    if (!member) {
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    QVariantList args;
    DBusMessageIter iter;
    if (dbus_message_iter_init(msg, &iter)) {
        do {
            args << extractArg(&iter);
        } while (dbus_message_iter_next(&iter));
    }

    self->dispatchCall(QString::fromUtf8(member), args);

    /* Nothing else is listening on this connection, and a monitor may not
       reply to anything, so consuming the message here is correct. */
    return DBUS_HANDLER_RESULT_HANDLED;
}
} // namespace

OsdMonitor::OsdMonitor(QObject *parent)
    : QObject(parent)
{
}

OsdMonitor::~OsdMonitor()
{
    if (m_conn) {
        dbus_connection_remove_filter(m_conn, &handleMessage, this);
        dbus_connection_close(m_conn);
        dbus_connection_unref(m_conn);
    }
}

bool OsdMonitor::start()
{
    DBusError err;
    dbus_error_init(&err);

    /* Private, because BecomeMonitor makes the connection receive-only —
       it must not be shared with anything that needs to send. */
    m_conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        qWarning("glassosd: cannot open session bus: %s", err.message);
        dbus_error_free(&err);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(m_conn, FALSE);

    DBusMessage *call = dbus_message_new_method_call("org.freedesktop.DBus",
                                                     "/org/freedesktop/DBus",
                                                     "org.freedesktop.DBus.Monitoring",
                                                     "BecomeMonitor");
    const char *rules[] = {kMatchRule};
    const char **rulesPtr = rules;
    dbus_uint32_t flags = 0;
    DBusMessageIter args;
    dbus_message_iter_init_append(call, &args);
    DBusMessageIter arr;
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &rulesPtr[0]);
    dbus_message_iter_close_container(&args, &arr);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &flags);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_conn, call, 2000, &err);
    dbus_message_unref(call);
    if (dbus_error_is_set(&err)) {
        qWarning("glassosd: BecomeMonitor refused: %s", err.message);
        dbus_error_free(&err);
        return false;
    }
    if (reply) {
        dbus_message_unref(reply);
    }

    if (!dbus_connection_add_filter(m_conn, &handleMessage, this, nullptr)) {
        qWarning("glassosd: could not install dbus filter");
        return false;
    }

    int fd = -1;
    if (!dbus_connection_get_unix_fd(m_conn, &fd) || fd < 0) {
        qWarning("glassosd: no usable fd for the monitor connection");
        return false;
    }
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &OsdMonitor::pump);

    pump(); // drain anything already buffered
    return true;
}

void OsdMonitor::pump()
{
    dbus_connection_read_write(m_conn, 0);
    while (dbus_connection_dispatch(m_conn) == DBUS_DISPATCH_DATA_REMAINS) {
    }
}

