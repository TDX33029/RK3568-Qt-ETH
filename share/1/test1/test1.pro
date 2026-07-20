QT += widgets charts

CONFIG += c++17

INCLUDEPATH += ../chapter5/ps_tracker

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    ../chapter5/ps_tracker/tracker_app.c \
    ../chapter5/ps_tracker/tracker3d.c

HEADERS += \
    mainwindow.h \
    tracker_bridge.h \
    ../chapter5/ps_tracker/tracker_app.h \
    ../chapter5/ps_tracker/tracker3d.h

FORMS += \
    mainwindow.ui

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
