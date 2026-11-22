QT += widgets dbus concurrent
CONFIG += c++17
TARGET = oryon-monitor
SOURCES += main.cpp telemetry.cpp
HEADERS += telemetry.h
INCLUDEPATH += /usr/include/KF6/KGlobalAccel /usr/include/KF6/KWindowSystem
LIBS += -lKF6GlobalAccel -lKF6WindowSystem
