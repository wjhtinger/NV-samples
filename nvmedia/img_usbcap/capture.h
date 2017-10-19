/* Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __CAPTURE_H__
#define __CAPTURE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

#include "main.h"
#include "log_utils.h"
#include "usb_utils.h"


#define IMAGE_BUFFERS_POOL_SIZE      6
#define BUFFER_POOL_TIMEOUT          100
#define GET_FRAME_TIMEOUT            100
#define DEQUEUE_TIMEOUT              100
#define ENQUEUE_TIMEOUT              100
#define WAIT_FOR_IDLE_TIMEOUT        100

typedef struct {
    /* capture */
    NvMediaDevice               *device;
    UtilUsbSensor               *captureDevice;
    UtilUsbSensorConfig         config;
    NvQueue                     *inputQueue;
    NvQueue                     *outputQueue;
    volatile NvBool             *quit;

    /* processing params */
    NvU32                       width;
    NvU32                       height;
    char                        fmt[256];
    NvMediaSurfaceType          inputSurfType;
    NvU32                       inputSurfAttributes;
    NvMediaImageAdvancedConfig  inputSurfAdvConfig;
} NvCaptureContext;


NvMediaStatus CaptureInit(NvMainContext *mainCtx);
void CaptureProc (void* data,void* user_data);
NvMediaStatus CaptureFini (NvMainContext *ctx);


#ifdef __cplusplus
}
#endif

#endif // __CAPTURE_H__
