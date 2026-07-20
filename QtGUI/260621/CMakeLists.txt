cmake_minimum_required(VERSION 3.19)
project(260621 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(260621
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    simulation_controller.cpp
    simulation_controller.h
    tracker3d.cpp
    tracker3d.h
    tracker_app.cpp
    tracker_app.h
    trajectory_chart.cpp
    trajectory_chart.h
)

target_link_libraries(260621
    PRIVATE
        Qt::Core
        Qt::Widgets
)

install(TARGETS 260621
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET 260621
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
