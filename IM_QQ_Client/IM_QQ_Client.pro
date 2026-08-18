QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# 强制源码按 UTF-8 解析（Qt 5.14+ / Qt 6）
CODECFORSRC = UTF-8


# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    widget.cpp \
    logindialog.cpp \
    clickablelabel/clickablelabel.cpp \
    registerdialog/registerdialog.cpp \
    tcpclient/tcpclient.cpp \
    messagehandler/messagehandler.cpp

HEADERS += \
    widget.h \
    logindialog.h \
    clickablelabel/clickablelabel.h \
    registerdialog/registerdialog.h \
    tcpclient/tcpclient.h   \
    messagehandler/messagehandler.h

FORMS += \
    widget.ui \
    logindialog.ui \
    registerdialog/registerdialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
