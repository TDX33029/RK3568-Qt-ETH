cmake_minimum_required(VERSION 3.16)

project(ps_tracker_ui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Qt 版本自动检测 ─────────────────────────────────────
# 优先 Qt6，找不到则降级到 Qt 5.15+（RK3568 常用 Qt5.15）
# 注意：find_package(QT NAMES ...) 写法在 CMake < 3.18 和 CMake >= 3.27 都有问题，
#       所以手动检测两轮。
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    message(STATUS "Using Qt6")
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
    set(QT_VERSION_MAJOR 5)
    message(STATUS "Using Qt5")
endif()

# ── 避免 AUTOMOC 预定义宏检测失败导致编译中断 ──────────
# 某些 MINIW + cmake 环境下 -dM -E 预处理可能找不到编译器路径。
# 关掉不影响 MOC 正确性，只是少了一小步缓存优化。
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
    set(CMAKE_AUTOMOC_MOC_PREDEFS OFF)
endif()

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

# 用普通 add_executable + AUTOMOC/AUTOUIC，Qt5/Qt6 都兼容
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Linux 需要 libm（sqrt/cos/sin 等数学函数）
if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

# ── 平台属性 ────────────────────────────────────────────
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

# ── 安装 ────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
