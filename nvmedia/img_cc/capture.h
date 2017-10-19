/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
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

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"
#include "parser.h"
#include "nvmedia_isc.h"
#include "nvmedia_icp.h"

#define CAPTURE_INPUT_QUEUE_SIZE             5     /* min no. of buffers needed to capture without any frame drops */
#define CAPTURE_DEQUEUE_TIMEOUT              1000
#define CAPTURE_ENQUEUE_TIMEOUT              100
#define CAPTURE_FEED_FRAME_TIMEOUT           100
#define CAPTURE_GET_FRAME_TIMEOUT            500
#define CAPTURE_MAX_RETRY                    10

typedef struct {
    NvMediaICPEx               *icpExCtx;
    NvQueue                    *inputQueue;
    NvQueue                    *outputQueue;
    volatile NvMediaBool       *quit;
    NvMediaBool                 exitedFlag;
    NvMediaICPSettings         *settings;
    NvMediaICPInterfaceFormat  interfaceFormat;

    /* capture params */
    NvU32                       width;
    NvU32                       height;
    NvU32                       virtualChannelIndex;
    NvU32                       currentFrame;
    NvU32                       numFramesToSkip;
    NvU32                       numFramesToCapture;
    NvU32                       numFramesToWait;
    NvU32                       numMiniburstFrames;
    NvU32                       numBuffers;

    /* input and surface params */
    NvMediaICPInputFormat       inputFormat;
    NvMediaICPSurfaceFormat     surfFormat;
    NvMediaSurfaceType          surfType;
    NvU32                       surfAttributes;
    NvMediaImageAdvancedConfig  surfAdvConfig;
    NvU32                       rawBytesPerPixel;
} CaptureThreadCtx;

typedef struct {
    /* capture context */
    NvThread                   *captureThread[NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS];
    CaptureThreadCtx            threadCtx[NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS];
    NvMediaICPEx               *icpExCtx;
    NvMediaICPSettingsEx        icpSettingsEx;
    NvMediaDevice              *device;
    NvMediaISCRootDevice       *iscCtx;
    CaptureConfigParams         captureParams;
    SensorInfo                 *sensorInfo;
    MapInfo                    *camMap;

    /* General Variables */
    volatile NvMediaBool       *quit;
    TestArgs                   *testArgs;
    NvU32                       numSensors;
    NvU32                       numVirtualChannels;
    NvU32                       i2cDeviceNum;
    NvU32                       inputQueueSize;
    I2cCommands                 parsedCommands;
    I2cCommands                 settingsCommands;
    CalibrationParameters       calParams;
    NvMediaICPInterfaceType     interfaceType;
    NvU32                       crystalFrequency;
    NvMediaBool                 useNvRawFormat;
} NvCaptureContext;

NvMediaStatus
CaptureInit(NvMainContext *mainCtx);

NvMediaStatus
CaptureFini(NvMainContext *mainCtx);

NvMediaStatus
CaptureProc(NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif

