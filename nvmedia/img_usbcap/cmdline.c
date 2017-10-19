/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmdline.h"

int ParseArgs (int argc,char **argv,TestArgs *args)
{
    int i = 0, err = 0;
    char firstAvailableCam[256];
    NvMediaBool bLastArg = NVMEDIA_FALSE;
    NvMediaBool bDataAvailable = NVMEDIA_FALSE;
    NvMediaBool captureDeviceProvided = NVMEDIA_FALSE;;
    NvMediaBool displayDeviceEnabled;
    NvMediaStatus rt;

    /* default params */
    strcpy(args->inpFmt,"yuyv");
    strcpy(args->surfFmt,"rgba");
    args->saveOutFile = NVMEDIA_FALSE;
    args->width = 960;
    args->height = 720;

    // The rest
    if(argc >= 2) {
        for (i = 1; i < argc; i++) {
            // check if this is the last argument
            bLastArg = ((argc - i) == 1);
            // check if there is data available to be parsed following the option
            bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');
            if (argv[i][0] == '-') {
                if(strcasecmp(&argv[i][1], "h") == 0) {
                    PrintUsage();
                    return 1;
                }  else if(strcasecmp(&argv[i][1], "v") == 0) {
                    args->logLevel = LEVEL_DBG;
                    if(bDataAvailable) {
                        args->logLevel = atoi(argv[++i]);
                        if(args->logLevel < LEVEL_ERR ||
                            args->logLevel > LEVEL_DBG) {
                            printf("Invalid logging level chosen (%d)\n",
                                   args->logLevel);
                            printf("Setting logging level to LEVEL_ERR (0)\n");
                            args->logLevel = LEVEL_ERR;
                        }
                    }
                    SetLogLevel(args->logLevel);
                }  else if (strcmp(&argv[i][1], "d") == 0) {
                    if (bDataAvailable) {
                        if((sscanf(argv[++i], "%u", &args->displayId) != 1)) {
                            LOG_ERR("Err: Bad display id: %s\n", argv[i]);
                            return 0;
                        }
                    rt = CheckDisplayDeviceID(args->displayId, &displayDeviceEnabled);
                    if (rt != NVMEDIA_STATUS_OK) {
                        LOG_ERR("Err: Chosen display (%d) not available\n", args->displayId);
                        return 0;
                    }
                    LOG_DBG("ParseArgs: Chosen display: (%d) device enabled? %d\n", args->displayId, displayDeviceEnabled);
                    } else {
                        LOG_ERR("Err: -d must be followed by display id\n");
                        return 0;
                    }
                } else if(strcasecmp(&argv[i][1], "s") == 0) {
                    if(bDataAvailable) {
                        ++i;
                        args->saveOutFile = NVMEDIA_TRUE;
                        args->outFileName = argv[i];
                    }
                }else if(strcasecmp(&argv[i][1], "dev") == 0) {
                    if(bDataAvailable) {
                        ++i;
                        strcpy(args->devPath, argv[i]);
                        captureDeviceProvided = NVMEDIA_TRUE;
                    }
                }else if(strcmp(&argv[i][1], "fr") == 0) {
                    if(bDataAvailable) {
                        if((sscanf(argv[++i], "%ux%u", &args->width,
                                &args->height) != 2)) {
                            LOG_ERR("ParseArgs: Bad output resolution: %s\n", argv[i]);
                            return 0;
                        }
                    }
                }else if(strcmp(&argv[i][1], "wpos") == 0) {
                    if(bDataAvailable) {
                        if((sscanf(argv[++i], "%u:%u", &args->windowOffset[0],
                                &args->windowOffset[1]) != 2)) {
                            LOG_ERR("ParseArgs: Bad window position specified : %s\n", argv[i]);
                            return 0;
                        }
                    }
                }else if(strcmp(&argv[i][1], "fmt") == 0) {
                    if(bDataAvailable) {
                        ++i;
                        strncpy(args->inpFmt,argv[i],MAX_STRING_SIZE);
                    }
                } else if(strcmp(&argv[i][1], "ot") == 0) {
                    if(bDataAvailable) {
                        ++i;
                        strncpy(args->surfFmt,argv[i],MAX_STRING_SIZE);
                    }
                }
            }
        }
    }

    if(!captureDeviceProvided) {
        err = UtilUsbSensorGetFirstAvailableCamera(firstAvailableCam);

        if(err) {
            printf("No capture devices found\n");
            return -1;
        }
        strcpy(args->devPath, firstAvailableCam);
    }
    return 0;
}

void
PrintUsage ()
{
    NvMediaVideoOutputDeviceParams videoOutputs[MAX_OUTPUT_DEVICES];
    NvMediaStatus rt;
    int outputDevicesNum, i;

    printf("Usage: nvmimg_usbcap [args]\n\n");
    printf("-h                    \tPrint usage\n");
    printf("-dev  [path]          \tDevice path (Default path is the first plugged in camera) \n");
    printf("                      \tAvailable V4l2 video Nodes:\n");
    UtilUsbSensorFindCameras();
    printf("-fr   [wxh]           \tResolution of the captured images (Default: 960x720) \n");
    printf("-ot   [type]          \tOutput surface type: 420p/rgba (Default: 420p) \n");
    printf("-wpos [x:y]           \tWindow start position (Default start position: 0:0 ) \n");
    printf("-s    [file]          \tSave the processed images to output file (RGBA only) \n");
    printf("-d    [id]            \tDisplay ID\n");
    printf("-v    [level]         \tVerbose\n\n");

    rt = GetAvailableDisplayDevices(&outputDevicesNum, &videoOutputs[0]);
    if(rt != NVMEDIA_STATUS_OK) {
        LOG_ERR("PrintUsage: Failed retrieving available video output devices\n");
        return;
    }

    LOG_MSG("\nAvailable display devices (%d):\n", outputDevicesNum);
    for(i = 0; i < outputDevicesNum; i++) {
        LOG_MSG("Display ID: %d\n", videoOutputs[i].displayId);
    }

}
