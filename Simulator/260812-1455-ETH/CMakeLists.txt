cmake_minimum_required(VERSION 3.13)
project(ps_tracker_ui VERSION 1.0.0 LANGUAGES CXX)

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

find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
    set(QT_VERSION_MAJOR 5)
endif()

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        alg/tracker_app.cpp
        alg/tracker_app.h
        alg/tracker3d.cpp
        alg/tracker3d.h
        tracker_plot.cpp
        tracker_plot.h
        frame_protocol.h
        eth_reader.cpp
        eth_reader.h
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

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

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

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(${PROJECT_NAME})
endif()
