QT += core gui widgets network

TARGET = system_watch
TEMPLATE = app

CONFIG += c++11

win32 {
    LIBS += -lpdh
    LIBS += -luser32 -lole32 -lwbemuuid -loleaut32 -luuid
    LIBS += -liphlpapi
    LIBS += -lws2_32
    LIBS += -ldxgi
}

QMAKE_CXXFLAGS += /Zc:strictStrings-

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    overlay.cpp \
    systemmonitor.cpp \
    gamelibrary.cpp \
    trayicon.cpp \
    hotkeyfilter.cpp

HEADERS += \
    mainwindow.h \
    overlay.h \
    systemmonitor.h \
    gamelibrary.h \
    trayicon.h \
    hotkeyfilter.h

RESOURCES += style.qrc

greaterThan(QT_MAJOR_VERSION, 4) {
    QT += widgets
}

RC_FILE = res/icon.rc

win32 {
    CONFIG(debug, debug|release) {
        QMAKE_POST_LINK += $$escape_expand(\n\t) if not exist "$$shell_path($$OUT_PWD)\debug\tools" mkdir "$$shell_path($$OUT_PWD)\debug\tools"
        QMAKE_POST_LINK += $$escape_expand(\n\t) copy /Y "$$shell_path($$PWD)\tools\PresentMon-2.6.0-x64.exe" "$$shell_path($$OUT_PWD)\debug\tools\PresentMon-2.6.0-x64.exe"
        QMAKE_POST_LINK += $$escape_expand(\n\t) copy /Y "$$shell_path($$PWD)\tools\PresentMon-LICENSE.txt" "$$shell_path($$OUT_PWD)\debug\tools\PresentMon-LICENSE.txt"
    }
    CONFIG(release, debug|release) {
        QMAKE_POST_LINK += $$escape_expand(\n\t) if not exist "$$shell_path($$OUT_PWD)\release\tools" mkdir "$$shell_path($$OUT_PWD)\release\tools"
        QMAKE_POST_LINK += $$escape_expand(\n\t) copy /Y "$$shell_path($$PWD)\tools\PresentMon-2.6.0-x64.exe" "$$shell_path($$OUT_PWD)\release\tools\PresentMon-2.6.0-x64.exe"
        QMAKE_POST_LINK += $$escape_expand(\n\t) copy /Y "$$shell_path($$PWD)\tools\PresentMon-LICENSE.txt" "$$shell_path($$OUT_PWD)\release\tools\PresentMon-LICENSE.txt"
    }
}
