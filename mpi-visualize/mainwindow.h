/**************************************************************************//**
 * @file mainwindow.h
 * @brief Main windows and MPI client setup
 *
 * This file contains the class declaration for the MainWindow class.
 * @author Daniel Queteschiner
 * @date June 2019
 *//*
 * Copyright (c) 2019 Daniel Queteschiner.
 *****************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <mpi.h>
#include "qcustomplot.h"

typedef short image_datatype;           // char // short // double
#define IMAGE_DATA_SCALE 32767.0        // (127.) // (32767) // (1.0)
#define MPI_IMAGE_DATATYPE MPI_SHORT    // MPI_SHORT // MPI_SHORT // MPI_DOUBLE

#define SIZE_X 512
#define SIZE_Y 512

#define MPI_TAG_MESSAGE_QUIT 0
#define MPI_TAG_IMAGE_DATA   1

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void setupColorMapDemo(QCustomPlot *customPlot);

private slots:
    void realtimeDataSlot();

private:
    void receivePendingMessages();

private:
    Ui::MainWindow *ui;
    QString demoName;
    QTimer dataTimer;
    image_datatype image_data[SIZE_X*SIZE_Y];
    MPI_Comm intercomm;
    QString port_name;
    int connected;
};

#endif // MAINWINDOW_H
