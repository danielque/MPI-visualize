/**************************************************************************//**
 * @file mainwindow.cpp
 * @brief Main window and MPI client setup
 *
 * This file demonstrate the client side setup for an MPI server-client
 * intercommunicator using MPI_Comm_connect. Data is exchanged using
 * non-blocking MPI_Isend and MPI_Irecv.
 * @author Daniel Queteschiner
 * @date June 2019
 *//*
 * Copyright (c) 2019 Daniel Queteschiner.
 *****************************************************************************/

#include <iostream>
#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setGeometry(400, 250, 542, 390);

    intercomm = MPI_COMM_NULL;
    connected = 0;

#if defined (_WIN32) || defined (_WIN64)
    QString fileName("C:/mpiportname.txt");
#elif defined (__linux__ )
    QString fileName("/tmp/mpiportname.txt");
#endif

    QFile inputFile(fileName);
    if (inputFile.open(QIODevice::ReadOnly))
    {
        QTextStream in(&inputFile);
        if (!in.atEnd())
        {
            port_name = in.readLine();
        }
        inputFile.close();

        MPI_Init(0,0);

        // must only be called after the MPI_Comm_accept call has been made by the MPI job acting as the server
        if (MPI_Comm_connect(port_name.toStdString().c_str(), MPI_INFO_NULL, 0, MPI_COMM_SELF, &intercomm) != MPI_SUCCESS)
        {
            std::cerr << "Failed to connected to mpi port" << std::endl << std::flush;
        }
        else
        {
            connected = 1;
        }
    }

    image_data = new image_datatype[SIZE_X * SIZE_Y];

    setupColorMapDemo(ui->customPlot);
    setWindowTitle("QCustomPlot: " + demoName);
    statusBar()->clearMessage();
    ui->customPlot->replot();}

/* ------------------------------------------------------------------------- */

MainWindow::~MainWindow()
{
    int initialized = 0;
    MPI_Initialized(&initialized);

    if (initialized)
    {
        if (connected)
        {
            std::cout << "Sending disconnect message" << std::endl << std::flush;
            int message_type = 1;
            MPI_Request request;
            MPI_Isend(&message_type, 1, MPI_INT, 0, MPI_TAG_MESSAGE_QUIT, intercomm, &request);

            receivePendingMessages();

            std::cout << "Disconnecting ..." << std::endl << std::flush;
            MPI_Comm_disconnect(&intercomm);
            connected = 0;
            std::cout << "Disconnected" << std::endl << std::flush;
        }
        std::cout << "Finalizing MPI ..." << std::endl << std::flush;
        MPI_Finalize();
        std::cout << "MPI finalized" << std::endl << std::flush;
    }
    delete ui;
    delete[] image_data;
}

/* ------------------------------------------------------------------------- */

void MainWindow::receivePendingMessages()
{
    while (true)
    {
        int messageAvailable = 0;
        MPI_Status status;
        if (MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, intercomm, &messageAvailable, &status) != MPI_SUCCESS)
        {
            std::cerr << "Error probing for MPI message, process exiting!" << std::endl << std::flush;
            MPI_Abort(intercomm, 1);
            break;
        }

        if (messageAvailable)
        {
            int count;
            int err = MPI_Get_count(&status, MPI_IMAGE_DATATYPE, &count);

            if(count == MPI_UNDEFINED || err != MPI_SUCCESS)
            {
                err = MPI_Get_count(&status, MPI_INT, &count);
                if(count != MPI_UNDEFINED && err == MPI_SUCCESS)
                {
                    std::cout << "Receiving queued int message ..." << std::endl << std::flush;
                    int *buf = new int[count];
                    MPI_Recv(buf, count, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, intercomm, &status);
                    delete [] buf;
                }
                else
                {
                    MPI_Abort(intercomm, -12);
                }
            }
            else if(count != MPI_UNDEFINED && err == MPI_SUCCESS)
            {
                std::cout << "Receiving queued double message ..." << std::endl << std::flush;
                image_datatype *buf = new image_datatype[count];
                MPI_Recv(buf, count, MPI_IMAGE_DATATYPE, MPI_ANY_SOURCE, MPI_ANY_TAG, intercomm, &status);
                delete [] buf;
            }
            else
            {
                MPI_Abort(intercomm, -13);
            }
        }
        else
        {
            break;
        }
    }
}

/* ------------------------------------------------------------------------- */

void MainWindow::setupColorMapDemo(QCustomPlot *customPlot)
{
  demoName = "Color Map Demo";

  // configure axis rect
  // this will also allow rescaling the color scale by dragging/zooming
  customPlot->setInteractions(QCP::iRangeDrag|QCP::iRangeZoom);
  customPlot->axisRect()->setupFullAxesBox(true);
  customPlot->xAxis->setLabel("x");
  customPlot->yAxis->setLabel("y");

  // set up the QCPColorMap:
  QCPColorMap *colorMap = new QCPColorMap(customPlot->xAxis, customPlot->yAxis);
  int nx = SIZE_X;
  int ny = SIZE_Y;
  // set the color map to have nx * ny data points
  colorMap->data()->setSize(nx, ny);
  // span the coordinate range -4..4 in both key (x) and value (y) dimensions
  colorMap->data()->setRange(QCPRange(-4, 4), QCPRange(-4, 4));

  // assign some data, by accessing the QCPColorMapData instance of the color map:
  double x, y, z;
  for (int xIndex=0; xIndex<nx; ++xIndex)
  {
    for (int yIndex=0; yIndex<ny; ++yIndex)
    {
      colorMap->data()->cellToCoord(xIndex, yIndex, &x, &y);
      double r = 3*qSqrt(x*x+y*y)+1e-2;
      z = 2*x*(qCos(r+2)/r-qSin(r+2)/r);
      colorMap->data()->setCell(xIndex, yIndex, z);
    }
  }
  colorMap->setInterpolate(false);

  // add a color scale:
  QCPColorScale *colorScale = new QCPColorScale(customPlot);
  // add it to the right of the main axis rect
  customPlot->plotLayout()->addElement(0, 1, colorScale);
  // scale shall be vertical bar with tick/axis labels right (actually atRight is already the default)
  colorScale->setType(QCPAxis::atRight);
  // associate the color map with the color scale
  colorMap->setColorScale(colorScale);
  colorScale->axis()->setLabel("Magnetic Field Strength");

  // set the color gradient of the color map to one of the presets:
  colorMap->setGradient(QCPColorGradient::gpPolar);
  // we could have also created a QCPColorGradient instance and added own colors to
  // the gradient, see the documentation of QCPColorGradient for what's possible.

  // rescale the data dimension (color) such that all data points
  // lie in the span visualized by the color gradient:
  colorMap->rescaleDataRange();

  // make sure the axis rect and color scale synchronize their bottom and top margins (so they line up):
  QCPMarginGroup *marginGroup = new QCPMarginGroup(customPlot);
  customPlot->axisRect()->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);
  colorScale->setMarginGroup(QCP::msBottom|QCP::msTop, marginGroup);

  // rescale the key (x) and value (y) axes so the whole color map is visible:
  customPlot->rescaleAxes();

  // setup a timer that repeatedly calls MainWindow::realtimeDataSlot
  connect(&dataTimer, SIGNAL(timeout()), this, SLOT(realtimeDataSlot()));
  dataTimer.start(5); // Interval 0 means to refresh as fast as possible
}

/* ------------------------------------------------------------------------- */

void MainWindow::realtimeDataSlot()
{
    static MPI_Request request_image_data = MPI_REQUEST_NULL;
    static QTime time(QTime::currentTime());
    double key = time.elapsed()/1000.0; // time elapsed since start of demo, in seconds
    const int nx = SIZE_X;
    const int ny = SIZE_Y;
    static double maxvalue = -100000.;
    static double minvalue =  100000.;
    static int recvFrameCount=0;
    static int totalRecvFrameCount=0;
    static int skippedRecvFrame=0;

    int initialized = 1;
    MPI_Initialized(&initialized);


    if (initialized && connected)
    {
        int flag = 0;
        if (request_image_data != MPI_REQUEST_NULL)
            MPI_Test(&request_image_data, &flag, MPI_STATUS_IGNORE);
        if (flag) // received image_data
        {
            request_image_data = MPI_REQUEST_NULL;
            ++recvFrameCount;
            ++totalRecvFrameCount;

            {
                // get pointer to color map:
                QCPColorMap *colorMap = qobject_cast<QCPColorMap *>(ui->customPlot->plottable());
                QCPColorMapData *cmdata = colorMap->data();
                for (int xIndex=0; xIndex<nx; ++xIndex)
                {
                    for (int yIndex=0; yIndex<ny; ++yIndex)
                    {
                        cmdata->setCell(xIndex, yIndex, image_data[xIndex+nx*yIndex]/IMAGE_DATA_SCALE);

                        if(image_data[xIndex+nx*yIndex] > maxvalue) maxvalue = image_data[xIndex+nx*yIndex];
                        else if(image_data[xIndex+nx*yIndex] < minvalue) minvalue = image_data[xIndex+nx*yIndex];
                    }
                }
            }
        }

        MPI_Status status;
        int messageAvailable = 0;
        do
        {
            messageAvailable = 0;
            if (MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, intercomm, &messageAvailable, &status) != MPI_SUCCESS)
            {
                std::cerr << "Error probing for MPI message!" << std::endl;
                MPI_Abort(intercomm, -11);
            }

            if (messageAvailable)
            {
                if (status.MPI_TAG == MPI_TAG_IMAGE_DATA)
                {
                    if (request_image_data != MPI_REQUEST_NULL)
                    {
                        if(1)//(totalRecvFrameCount+skippedRecvFrame)%2==0)
                        {
                            ++skippedRecvFrame;
                            MPI_Cancel(&request_image_data);
                            MPI_Request_free(&request_image_data);
                        }
                        else
                        {
                            MPI_Wait(&request_image_data, &status);
                        }
                        request_image_data = MPI_REQUEST_NULL;
                    }
                    MPI_Irecv(&image_data, SIZE_X*SIZE_Y, MPI_IMAGE_DATATYPE, 0, MPI_TAG_IMAGE_DATA, intercomm, &request_image_data);
                }
                else // if (status.MPI_TAG == MPI_TAG_MESSAGE_QUIT)
                {
                    int message = -1;
                    MPI_Recv(&message, 1, MPI_INT, 0, MPI_TAG_MESSAGE_QUIT, intercomm, MPI_STATUS_IGNORE);
                    {
                        // close connections
                        std::cout << "Received disconnection message" << std::endl << std::flush;
                        std::cout << "Disconnecting ..." << std::endl << std::flush;
                        MPI_Comm_disconnect(&intercomm);
                        std::cout << "Disconnected" << std::endl << std::flush;
                        connected = 0;
                    }
                }
            }
        } while(messageAvailable && connected);
    }

    ui->customPlot->replot();

    // calculate frames per second:
    static double lastFpsKey = 0;
    static int frameCount = 0;
    ++frameCount;

    if (key-lastFpsKey > 2) // average fps over 2 seconds
    {
        ui->statusBar->showMessage(
              QString("%1 FPS, %2 rFPS, Total Data points: %3, Frame: %4, rFrames: %5, skipped %6, Min: %7, Max: %8")
              //.arg(port_name)
              .arg(frameCount/(key-lastFpsKey), 0, 'f', 0)
              .arg(recvFrameCount/(key-lastFpsKey), 0, 'f', 0)
              .arg(nx*ny)
              .arg(frameCount)
              .arg(totalRecvFrameCount)
              .arg(skippedRecvFrame)
              .arg(minvalue)
              .arg(maxvalue)
              , 0);
        lastFpsKey = key;
        frameCount = 0;
        recvFrameCount = 0;
    }
}

