/*
    SPDX-FileCopyrightText: 2026 glassosd contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "capslockwatcher.h"

/*
    Note for future work: KModifierKeyInfo::setKeyLocked() cannot be used to
    self-test this path. org_kde_kwin_keystate is a read-only protocol — it
    reports state and has no set request — so the call is a silent no-op under
    Wayland. Verified: two consecutive toggle attempts both logged
    "off -> on", i.e. the first never took. Caps lock needs a real keypress to
    test end to end.
*/

CapsLockWatcher::CapsLockWatcher(QObject *parent)
    : QObject(parent)
{
    /* Seed from the current state so that if the provider replays the initial
       value on connect we do not pop an OSD at login, while a genuine first
       press still differs from the seed and gets through. */
    m_state[Qt::Key_CapsLock] = m_info.isKeyLocked(Qt::Key_CapsLock);
    m_state[Qt::Key_NumLock] = m_info.isKeyLocked(Qt::Key_NumLock);

    /* knowsKey is the honest health check for the provider: false means the
       org_kde_kwin_keystate binding never came up, which isKeyLocked() alone
       cannot distinguish from "caps lock is simply off". */
    qWarning("glassosd: capslock known=%s locked=%s | numlock known=%s locked=%s",
             m_info.knowsKey(Qt::Key_CapsLock) ? "yes" : "no",
             m_state[Qt::Key_CapsLock] ? "yes" : "no",
             m_info.knowsKey(Qt::Key_NumLock) ? "yes" : "no",
             m_state[Qt::Key_NumLock] ? "yes" : "no");

    connect(&m_info, &KModifierKeyInfo::keyLocked, this, [this](Qt::Key key, bool locked) {
        if (key != Qt::Key_CapsLock && key != Qt::Key_NumLock) {
            return; // scroll lock deliberately not surfaced
        }
        if (m_state.value(key) == locked) {
            return;
        }
        m_state[key] = locked;
        Q_EMIT lockChanged(static_cast<int>(key), locked);
    });
}
