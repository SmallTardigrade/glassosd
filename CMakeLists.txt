cmake_minimum_required(VERSION 3.20)
project(glassosd VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick DBus Widgets)
find_package(KF6 6.0 REQUIRED COMPONENTS WindowSystem GuiAddons Config I18n GlobalAccel StatusNotifierItem IdleTime)
find_package(LayerShellQt REQUIRED)

# QtDBus exposes no BecomeMonitor / message-filter API, so the OSD monitor
# talks to libdbus-1 directly on its own private connection.
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS1 REQUIRED IMPORTED_TARGET dbus-1)

qt_standard_project_setup(REQUIRES 6.6)

# QML files live in qml/ rather than beside the sources.
qt_policy(SET QTP0004 NEW)

qt_add_executable(glassosd
    src/main.cpp
    src/osdmonitor.cpp
    src/osdmodel.cpp
    src/capslockwatcher.cpp
    src/fnlockwatcher.cpp
    src/notification.cpp
    src/notificationmodel.cpp
    src/notificationserver.cpp
    src/rules.cpp
    src/appsettings.cpp
    src/appearance.cpp
    src/mpriscontroller.cpp
    src/trayicon.cpp
    src/historymodel.cpp
    src/surfacewindow.cpp
    src/controller.cpp
)

target_compile_definitions(glassosd PRIVATE GLASSOSD_VERSION="${PROJECT_VERSION}")

# Must be set before qt_add_qml_module reads the file list.
set_source_files_properties(qml/Style.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(glassosd
    URI org.glassosd.ui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Style.qml
        qml/Icons.qml
        qml/GlassPanel.qml
        qml/OsdSurface.qml
        qml/NotificationStack.qml
        qml/NotificationCard.qml
        qml/HistoryPanel.qml
        qml/PanelButton.qml
        qml/Toggle.qml
        qml/CardButton.qml
    RESOURCES
        qml/icons/volume-muted.svg
        qml/icons/volume-low.svg
        qml/icons/volume-medium.svg
        qml/icons/volume-high.svg
        qml/icons/mic-on.svg
        qml/icons/mic-muted.svg
        qml/icons/brightness.svg
        qml/icons/keyboard-brightness.svg
        qml/icons/media.svg
        qml/icons/lock-caps.svg
        qml/icons/lock-num.svg
        qml/icons/lock-fn.svg
        qml/icons/dnd.svg
        qml/icons/close.svg
        qml/icons/settings.svg
        qml/icons/media-play.svg
        qml/icons/media-pause.svg
        qml/icons/media-next.svg
        qml/icons/media-prev.svg
    SOURCES
        src/osdmodel.h
        src/surfacewindow.h
        src/notificationmodel.h
        src/historymodel.h
        src/appsettings.h
        src/appearance.h
        src/mpriscontroller.h
    RESOURCE_PREFIX /qt/qml
)

# The generated qmltyperegistrations.cpp includes our headers by bare name.
target_include_directories(glassosd PRIVATE src)

target_link_libraries(glassosd PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus Qt6::Widgets
    KF6::WindowSystem KF6::GuiAddons KF6::ConfigCore KF6::I18n KF6::GlobalAccel KF6::StatusNotifierItem KF6::IdleTime
    LayerShellQt::Interface
    PkgConfig::DBUS1
)

include(GNUInstallDirs)

# The unit used to hardcode /usr/local/bin, which is wrong for every packaged
# install. Generate it from the real bindir instead.
configure_file(data/glassosd.service.in
               ${CMAKE_CURRENT_BINARY_DIR}/glassosd.service @ONLY)

# systemd user units live under prefix/lib, never lib64 — ask systemd itself
# rather than reusing CMAKE_INSTALL_LIBDIR, which is lib64 on Fedora.
pkg_get_variable(SYSTEMD_USER_UNIT_DIR systemd systemduserunitdir)
if(NOT SYSTEMD_USER_UNIT_DIR)
    set(SYSTEMD_USER_UNIT_DIR ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
endif()

install(TARGETS glassosd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS tools/glassosdctl DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/glassosd.service
        DESTINATION ${SYSTEMD_USER_UNIT_DIR})
