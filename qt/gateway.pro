QT       += core gui widgets network

CONFIG += c++14

TARGET = qt_gateway

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

# 交叉编译部署后运行方式(板上,网关同机):
#   ./qt_gateway 127.0.0.1 -platform linuxfb
#   或远程查看: ./qt_gateway 127.0.0.1 -platform vnc:port=5900
