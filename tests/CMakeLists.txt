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
    SOURCES portalserver.cpp notificationserver.cpp notificationmodel.cpp
            historymodel.cpp rules.cpp soundplayer.cpp notification.cpp
    LIBS Qt6::Qml Qt6::Quick KF6::WindowSystem KF6::IdleTime KF6::ConfigCore)
