cmake_minimum_required(VERSION 3.16)

# Project: PS Tracker UI (ported from Zynq ZCU102 to RK3568/Qt)
project(ps_tracker_ui VERSION 1.0.0 LANGUAGES CXX)

# Qt prefix: override with -DQT_PREFIX=... (or env QT_PREFIX) for cross-builds.
# Default points at the host x86-64 Qt for native builds only.
# For an aarch64/RK3568 build you MUST point this at an aarch64 Qt6
# (e.g. a Yocto SDK or one built against your target sysroot).
if(NOT DEFINED QT_PREFIX)
    if(DEFINED ENV{QT_PREFIX})
        set(QT_PREFIX "$ENV{QT_PREFIX}" CACHE PATH "Qt install prefix")
    else()
        set(QT_PREFIX "/home/ubuntu/Qt/6.5.3/gcc_64" CACHE PATH "Qt install prefix")
    endif()
endif()
list(APPEND CMAKE_PREFIX_PATH "${QT_PREFIX}")

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt5 / Qt6 auto-detection (both work on RK3568)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        tracker_app.cpp
        tracker_app.h
        tracker3d.cpp
        tracker3d.h
        tracker_plot.cpp
        tracker_plot.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(${PROJECT_NAME}
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(${PROJECT_NAME} SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(${PROJECT_NAME}
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# Core dependency
target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Math library: needed on Linux (libm) for sqrt/cos/sin used in tracker3d/tracker_app.
# On Windows/MinGW it is implicitly available.
if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

# ── Platform-specific bundle properties ──────────────────────
# Only apply WIN32_EXECUTABLE on Windows (hides the console window).
# Only apply MACOSX_BUNDLE on Apple platforms.
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.ps-tracker.ui
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
endif()

# ── Installation ─────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${PROJECT_NAME})
endif()
