cmake_minimum_required(VERSION 3.16)

project(ps_tracker_ui VERSION 1.0.0 LANGUAGES CXX)

# ── 静态编译选项 ─────────────────────────────────────────
# 启用后把所有运行时库（含 glibc）静态链接进可执行文件。
# 编译产物变大（50-100MB），但不依赖目标系统的 glibc 版本。
# ATK-3568 交叉编译时用 -DSTATIC_BUILD=ON 开启。
option(STATIC_BUILD "Fully static linking (including glibc) for portable binary" OFF)
if(STATIC_BUILD)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")
    message(STATUS "Static build enabled: -static-libgcc -static-libstdc++ -static")
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Qt 版本自动检测 ─────────────────────────────────────
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    message(STATUS "Using Qt6")
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
    set(QT_VERSION_MAJOR 5)
    message(STATUS "Using Qt5")
endif()

# 避免 AUTOMOC 预定义宏检测失败（某些 MinGW 环境问题）
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

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

# ── 平台属性 ────────────────────────────────────────────
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
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
