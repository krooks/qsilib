
TEMPLATE = lib
TARGET = qsilib
DEPENDPATH += .
INCLUDEPATH += .

CONFIG += staticlib

HEADERS += qserial.h siproto.h \
    siproto_p.h
SOURCES += qserial.cpp siproto.cpp crc529.c
