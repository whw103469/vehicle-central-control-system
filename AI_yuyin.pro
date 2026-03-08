QT       += core gui network multimedia serialbus bluetooth dbus

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    localprocessor.cpp \
    cloudclient.cpp \
    speechcontroller.cpp \
    pocketsphinxwakedetector.cpp \
    musiccontroller.cpp \
    vehiclecontroller.cpp \
    ../Camera/videocapture.cpp \
    bluetoothcontroller.cpp \
    bluetoothdialog.cpp \
    cloudiotclient.cpp \
    sentinelh264streamer.cpp
    cloudossclient.cpp

HEADERS += \
    mainwindow.h \
    localprocessor.h \
    cloudclient.h \
    speechcontroller.h \
    pocketsphinxwakedetector.h \
    musiccontroller.h \
    vehiclecontroller.h \
    ../Camera/videocapture.h \
    bluetoothcontroller.h \
    bluetoothdialog.h \
    cloudiotclient.h \
    sentinelh264streamer.h
    cloudossclient.h

INCLUDEPATH += /usr/include/pocketsphinx \
               /usr/include/aarch64-linux-gnu/sphinxbase \
               /opt/voice/include \
               ../Camera

LIBS += -lasound -lpthread -lpocketsphinx -lsphinxbase -lsphinxad -lv4l2 -L/opt/voice/libs -lvosk -lavformat -lavcodec -lavutil -lswscale
QMAKE_RPATHDIR += /opt/voice/libs
