/**************************************************************************//**
 * @file main.c
 * @brief MPI server setup
 *
 * This file demonstrate the server side setup for an MPI server-client
 * intercommunicator using MPI_Open_port and MPI_Comm_accept. Data is
 * exchanged using non-blocking MPI_Isend and MPI_Irecv.
 * @author Daniel Queteschiner
 * @date June 2019
 *//*
 * Copyright (c) 2019 Daniel Queteschiner.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpi.h>
#if defined (_WIN32) || defined (_WIN64)
#include <Windows.h>
#include <tchar.h>
#include <ShlObj.h>
#endif

typedef short image_datatype;           // char // short // double
#define IMAGE_DATA_SCALE 32767.0        // (127.) // (32767) // (1.0)
#define MPI_IMAGE_DATATYPE MPI_SHORT    // MPI_SHORT // MPI_SHORT // MPI_DOUBLE

#define SIZE_X 512
#define SIZE_Y 512
#define SIZE_Y_HALF (SIZE_Y >> 1)

#define MPI_TAG_MESSAGE_QUIT 0
#define MPI_TAG_IMAGE_DATA   1

#define PROGRAMM_DURATION 15.0

static MPI_Request recv_disconnect_request = MPI_REQUEST_NULL;
static MPI_Request send_image_data_request = MPI_REQUEST_NULL;

/* ------------------------------------------------------------------------- */

int  mpiConnect(char* port_name, MPI_Comm* comm);
void mpiDisconnect(const char* port_name, MPI_Comm* comm);
int  mpiIsIntercommAlive(const char* port_name, MPI_Comm* comm);

/* ------------------------------------------------------------------------- */

int main(int argc, char** argv)
{
    int world_size;
    int world_rank;
    char port_name[MPI_MAX_PORT_NAME] = {0};
    MPI_Comm intercomm = MPI_COMM_NULL;
    int open_port = 0;
    int connected = 0;

    const int nx = SIZE_X;
    const int ny = SIZE_Y_HALF;
    double* image_part_base = 0;
    image_datatype* image_part = 0;
    image_datatype* image_data = 0;

    // initialize the MPI environment
    MPI_Init(&argc, &argv);

    // get the number of processes
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // get the rank of the process
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    if (world_rank == 0)
    {
        // parse command line arguments
        for (int iarg = 1; iarg < argc; ++iarg)
        {
            if (strcmp(argv[iarg], "--version") == 0)
            {
                printf("mpi_compute_19.06.1\n");
            }
            else if (strcmp(argv[iarg], "--help") == 0)
            {
                printf("run with 2 processes, i.e. mpirun -np 2\n"
                       "use '--openport' to connect with visualization program\n");
            }
            else if (strcmp(argv[iarg], "--openport") == 0)
            {
                open_port = 1;
            }
            else
            {
                printf("unknown option\n");
            }
        }
    }

    if (world_size != 2)
    {
        printf("demo must run with 2 procs!\n"); fflush(stdout);
        MPI_Finalize();
        return 0;
    }

    // open port for intercommunication
    if (world_rank == 0 && open_port)
    {
        connected = mpiConnect(port_name, &intercomm);
    }

    image_part_base = (double*)malloc(sizeof(double) * SIZE_X * SIZE_Y_HALF);
    image_part = (image_datatype*)malloc(sizeof(image_datatype) * SIZE_X * SIZE_Y_HALF);
    image_data = (image_datatype*)malloc(sizeof(image_datatype) * SIZE_X * SIZE_Y);

    // compute base data
    {
        double x, y, z, r;
        int xIndex, yIndex;

        for (yIndex = 0; yIndex < ny; ++yIndex)
        {
            for (xIndex = 0; xIndex < nx; ++xIndex)
            {
                x = (1.0 * xIndex)                   / SIZE_X * 8.0 - 4.0; // scale to [-4,4]
                y = (yIndex + 1.0 * ny * world_rank) / SIZE_Y * 8.0 - 4.0; // scale to [-4,4]
                r = 3.0 * sqrt(x * x + y * y) + 1e-2;
                z = 2.0 * x * (cos(r + 2.) / r - sin(r + 2.) / r);
                image_part_base[xIndex + yIndex * nx] = z;
            }
        }
    }

    {
        int frames = 0;
        int sent_frames = 0;
        double time, start_time, end_time, last_send_time;

        start_time = MPI_Wtime();  // get current time
        MPI_Bcast(&start_time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD); // sync time on all processes
        time = start_time;
        last_send_time = start_time;
        end_time = start_time + PROGRAMM_DURATION;

        // main loop
        while (time < end_time)
        {
            // compute data
            double time_factor = IMAGE_DATA_SCALE * fabs(sin(time - start_time));
            int xIndex, yIndex;

            for (yIndex = 0; yIndex < ny; ++yIndex)
            {
                for (xIndex = 0; xIndex < nx; ++xIndex)
                {
                    image_part[xIndex + yIndex * nx] = (image_datatype)(image_part_base[xIndex + yIndex * nx] * time_factor);
                }
            }

            // time for intercommunication?
            if (time - last_send_time > 0.03333) // ~30fps should be enough for visualization
            {
                // collect image data
                MPI_Gather(image_part, SIZE_X * SIZE_Y_HALF, MPI_IMAGE_DATATYPE,
                           image_data, SIZE_X * SIZE_Y_HALF, MPI_IMAGE_DATATYPE,
                           0, MPI_COMM_WORLD);

                // send data to visualization program
                if (world_rank == 0 && connected)
                {
                    printf("updated time: %lf\n", time - start_time); fflush(stdout);

                    // update connection status
                    connected = mpiIsIntercommAlive(port_name, &intercomm);

                    if (connected)
                    {
                        // wait for previous data to be received
                        MPI_Wait(&send_image_data_request, MPI_STATUS_IGNORE);

                        // update connection status
                        connected = mpiIsIntercommAlive(port_name, &intercomm);

                        if (connected)
                        {
                            // send new data
                            MPI_Isend(image_data, SIZE_X * SIZE_Y, MPI_IMAGE_DATATYPE, 0, MPI_TAG_IMAGE_DATA, intercomm, &send_image_data_request);
                            ++sent_frames;
                        }
                    }
                }

                last_send_time = time;
            }

            if (world_rank == 0)
            {
                time = MPI_Wtime();
            }
            MPI_Bcast(&time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

            ++frames;
        } // end loop

        if (world_rank == 0 && connected)
        {
            printf("Waiting for last image send to be received ...\n"); fflush(stdout);
            MPI_Wait(&send_image_data_request, MPI_STATUS_IGNORE);
        }

        printf("%d: end time = %lf, frames %d (sent %d), FPS %lf, sFPS %lf\n",
               world_rank, time,
               frames, sent_frames,
               frames / (time - start_time), sent_frames / (time - start_time));
        fflush(stdout);
    }

    // disconnect
    if (world_rank == 0 && connected)
    {
        connected = mpiIsIntercommAlive(port_name, &intercomm);

        if (connected)
        {
            int message = 1;
            printf("Sending disconnection message\n"); fflush(stdout);
            MPI_Ssend(&message, 1, MPI_INT, 0, MPI_TAG_MESSAGE_QUIT, intercomm);
            connected = mpiIsIntercommAlive(port_name, &intercomm);

            if (connected)
            {
                mpiDisconnect(port_name, &intercomm);
            }
        }
    }

    // finalize the MPI environment.
    printf("Finalizing MPI ...\n"); fflush(stdout);
    MPI_Finalize();
    printf("MPI finalized\n"); fflush(stdout);

    free(image_part_base);
    free(image_part);
    free(image_data);

    return 0;
}

/* ------------------------------------------------------------------------- */

int mpiConnect(char* port_name, MPI_Comm* comm)
{
    // write port name to file
    FILE* port_file = 0;
#if defined (_WIN32) || defined (_WIN64)
    int csidl = CSIDL_APPDATA;
    TCHAR path[_MAX_PATH];
    WIN32_FIND_DATA findFileData;
    HANDLE hFindFile = INVALID_HANDLE_VALUE;
    TCHAR appName[] = TEXT("mpi-server-client");
    TCHAR file_name[_MAX_PATH] = {0};

    if (FAILED(SHGetFolderPath(NULL, csidl | CSIDL_FLAG_CREATE, NULL, 0, path)))
    {
        printf("Failed to get known folder path (%x)\n", csidl);
    }
    else
    {
        _tcscat(path, TEXT("\\"));
        _tcscat(path, appName);
    }

    hFindFile = FindFirstFile(path, &findFileData);

    if (hFindFile == INVALID_HANDLE_VALUE)
    {
        if (!CreateDirectory(path, NULL))
        {
            printf("Failed to create directory %ls", path); fflush(stdout);
            return 0;
        }
    }
    else
    {
        FindClose(hFindFile);
    }

    _tcscpy(file_name, path);
    _tcscat(file_name, TEXT("\\"));
    _tcscat(file_name, TEXT("mpiportname.txt"));
    port_file = _tfopen(file_name, TEXT("wt"));
#elif defined (__linux__ )
    char file_name[] = "/tmp/mpiportname.txt";
    port_file = fopen(file_name, "wt");
#endif

    if (port_file)
    {
        // open port
        printf("Opening port for intercomm\n"); fflush(stdout);
        MPI_Open_port(MPI_INFO_NULL, port_name);

        fprintf(port_file, "%s", port_name); fflush(stdout);
        fclose(port_file);
    }
    else
    {
        printf("Failed to open file\n"); fflush(stdout);
        return 0;
    }

    // accept connection from client (blocks until client called MPI_Comm_connect)
    printf("Waiting for intercomm ...\n");  fflush(stdout);
    MPI_Comm_accept(port_name, MPI_INFO_NULL, 0, MPI_COMM_SELF, comm);

    if (*comm == MPI_COMM_NULL)
    {
        printf("Error: no intercommunicator!\n"); fflush(stdout);
        return 0;
    }

    printf("Intercomm accepted\n");
    return 1;
}

/* ------------------------------------------------------------------------- */

void mpiDisconnect(const char* port_name, MPI_Comm* comm)
{
    if (*comm == MPI_COMM_NULL)
    {
        printf("Communicator is already NULL!\n"); fflush(stdout);
        return;
    }

    if (send_image_data_request != MPI_REQUEST_NULL)
    {
        printf("Cancelling send_image_data request!\n"); fflush(stdout);
        MPI_Cancel(&send_image_data_request);
        MPI_Request_free(&send_image_data_request);
    }
    if (recv_disconnect_request != MPI_REQUEST_NULL)
    {
        printf("Cancelling recv_disconnect request!\n"); fflush(stdout);
        MPI_Cancel(&recv_disconnect_request);
        MPI_Request_free(&recv_disconnect_request);
    }

    printf("Disconnecting ...\n"); fflush(stdout);
    MPI_Comm_disconnect(comm);
    *comm = MPI_COMM_NULL;
    printf("Disconnected\n"); fflush(stdout);

    MPI_Close_port(port_name);
    printf("Port closed\n"); fflush(stdout);
}

/* ------------------------------------------------------------------------- */

int mpiIsIntercommAlive(const char* port_name, MPI_Comm* comm)
{
    MPI_Comm intercomm = *comm;

    if (intercomm == MPI_COMM_NULL)
    {
        printf("Intercommunicator is NULL!\n"); fflush(stdout);
        return 0;
    }
    else
    {
        int flag;

        if (recv_disconnect_request == MPI_REQUEST_NULL)
        {
            // wait for a quit message
            static int message = -1;

            if (MPI_Irecv(&message, 1, MPI_INT, 0, MPI_TAG_MESSAGE_QUIT, intercomm, &recv_disconnect_request) != MPI_SUCCESS)
            {
                printf("MPI_Irecv communication failed!\n"); fflush(stdout);
                *comm = MPI_COMM_NULL;
                MPI_Abort(MPI_COMM_WORLD, -1);
            }

            return 1;
        }

        if (MPI_Test(&recv_disconnect_request, &flag, MPI_STATUS_IGNORE) != MPI_SUCCESS)
        {
            printf("MPI_Test communication failed!\n"); fflush(stdout);
            *comm = MPI_COMM_NULL;
            return 0;
        }

        if (flag) // received quit message
        {
            printf("Received disconnect message from client program'\n"); fflush(stdout);
            mpiDisconnect(port_name, comm);
            return 0;
        }
    }

    return 1;
}
