/**************************************************************************//**
 * @file main.cpp
 * @brief main program
 *
 * This file sets up the Qt application and main window.
 * @author Daniel Queteschiner
 * @date June 2019
 *//*
 * Copyright (c) 2019 Daniel Queteschiner.
 *****************************************************************************/

#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    return a.exec();
}
