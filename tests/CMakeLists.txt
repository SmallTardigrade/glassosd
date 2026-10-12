# SPDX-FileCopyrightText: 2026 glassosd contributors
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

# Qt6::DBus for the headers alone: notification.h carries the freedesktop wire
# types, so anything that knows what a Notification is needs QDBusArgument to
# be declared. Nothing here opens a bus connection.

# Each test compiles the sources it exercises rather than linking a shared
# library of them. The application target is a QML module, and moving its
# sources out from under qt_add_qml_module to share them here would risk the
# type registration for no gain — four files compiled twice costs nothing.
function(glassosd_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})

    add_executable(${name} ${name}.cpp)
    foreach(src IN LISTS T_SOURCES)
        target_sources(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src/${src})
    endforeach()

    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE Qt6::Test Qt6::Gui Qt6::DBus ${T_LIBS})

    add_test(NAME ${name} COMMAND ${name})
    # A test that reaches for a display it cannot have fails for the wrong
    # reason. Nothing here opens a window; offscreen keeps it that way even
    # when the suite is run from inside a session.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60)
endfunction()

glassosd_test(tst_rules
    SOURCES rules.cpp
    LIBS KF6::ConfigCore)

glassosd_test(tst_soundplayer
    SOURCES soundplayer.cpp)

glassosd_test(tst_historymodel
    SOURCES historymodel.cpp
    LIBS Qt6::Qml KF6::ConfigCore)

glassosd_test(tst_notificationmodel
    SOURCES notificationmodel.cpp soundplayer.cpp
    LIBS Qt6::Qml Qt6::Quick KF6::WindowSystem KF6::IdleTime)

glassosd_test(tst_portalserver
    SOURCES portalserver.cpp portalbackend.cpp notificationserver.cpp notificationmodel.cpp
            historymodel.cpp rules.cpp soundplayer.cpp notification.cpp
    LIBS Qt6::Qml Qt6::Quick KF6::WindowSystem KF6::IdleTime KF6::ConfigCore)

glassosd_test(tst_portalrelay
    SOURCES portalrelay.cpp portalbackend.cpp notificationserver.cpp notificationmodel.cpp
            historymodel.cpp rules.cpp soundplayer.cpp notification.cpp
    LIBS Qt6::Qml Qt6::Quick KF6::WindowSystem KF6::IdleTime KF6::ConfigCore)

glassosd_test(tst_snoozestore
    SOURCES snoozestore.cpp)

# The relay's round-trip tests need a session bus of their own: on a desktop
# session's bus they would be trying to take org.freedesktop.Notifications
# from whatever daemon is showing the user's notifications. dbus-run-session
# provides a throwaway one, and the variable tells the test it is inside it.
# Without the tool, tst_portalrelay still runs the translation tests and
# skips the rest rather than failing a build machine that lacks dbus.
find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
    add_test(NAME tst_portalrelay_bus
             COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/dbus-test-session.conf
                     -- ${CMAKE_COMMAND} -E env GLASSOSD_PRIVATE_BUS=1
                     $<TARGET_FILE:tst_portalrelay>)
    set_tests_properties(tst_portalrelay_bus PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60)
endif()
