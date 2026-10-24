# 双电机 PID 调速 - Qt 上位机监控
QT       += core gui
QT       += charts
QT       += serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET   = MotorMonitor
TEMPLATE = app

CONFIG  += c++11

SOURCES += main.cpp \
           mainwindow.cpp

HEADERS += mainwindow.h

FORMS   += mainwindow.ui
