/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _MAIN_H
#define _MAIN_H

#include "nvmedia.h"
#include "nvcommon.h"
#include "nvmedia_ipp.h"
#include "nvmedia_icp.h"
#include "nvmedia_isc.h"
#include "nvmedia_image.h"
#include "img_dev.h"
#include "thread_utils.h"
#include "buffer_utils.h"

#define MAX_STRING_SIZE 256

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void                    *ctx; //pointer to IPPCtx
    NvU32                   threadId;
} ThreadData;

typedef struct {
    NvMediaBool                         quit;
    NvMediaBool                         showTimeStamp;
    NvMediaDevice                       *device;
    NvU32                               imagesNum;

    // ICP
    NvMediaICPSettings                  icpSettings;
    NvMediaSurfaceType                  inputSurfType;
    NvMediaICPSurfaceFormat             inputSurfFormat;
    NvMediaImageAdvancedConfig          inputSurfAdvConfig;
    NvMediaIPPRawCompressionFormat      rawCompressionFormat;
    NvU32                               inputSurfAttributes;
    NvU32                               embeddedLinesTop;
    NvU32                               embeddedLinesBottom;
    NvU32                               inputWidth;
    NvU32                               inputHeight;
    NvU32                               rawBytesPerPixel;

    // ISC
    ExtImgDevice                        *extImgDevice;

    // IPP
    NvMediaIPPManager                   *ippManager;
    NvMediaIPPPipeline                  *ippPipeline[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    NvMediaIPPComponent                 *controlAlgorithmComponent[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    NvMediaIPPComponent                 *iscComponent[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    NvMediaIPPComponent                 *outputComponent[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    NvMediaIPPComponent                 *icpComponent;
    NvThread                            *getOutputThread[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    ThreadData                          getOutputThreadData[NVMEDIA_MAX_PIPELINES_PER_MANAGER];
    NvU32                               ippPipelineNum;

    // Display
    BufferPool                          *displayPool;
    NvMediaIDP                          *display;
    NvMediaBool                         displayEnabled;
    NvU32                               displayId;
    NvU32                               windowId;
    NvU32                               depth;
    NvU32                               displayCameraId;
    NvU32                               displayCpuBufferSize;
    NvMutex                             *displayCycleMutex;
    NvU8                                *displayCpuBuffer;

    // Image Writing
    char                                filename[MAX_STRING_SIZE];
    char                                imageFileName[MAX_AGGREGATE_IMAGES][MAX_STRING_SIZE];
    NvU32                               writeImageBufferSize;
    NvU8                                *writeImageBuffer[MAX_AGGREGATE_IMAGES];
    NvMediaBool                         saveEnabled;
} IPPCtx;

typedef struct {
    char    name[MAX_STRING_SIZE];
    char    description[MAX_STRING_SIZE];
    char    board[MAX_STRING_SIZE];
    char    inputDevice[MAX_STRING_SIZE];
    char    inputFormat[MAX_STRING_SIZE];
    char    surfaceFormat[MAX_STRING_SIZE];
    char    resolution[MAX_STRING_SIZE];
    char    interface[MAX_STRING_SIZE];
    NvU32   i2cDevice;
    NvU32   csiLanes;
    NvU32   embeddedDataLinesTop;
    NvU32   embeddedDataLinesBottom;
    NvU32   desAddr;
    NvU32   brdcstSerAddr;
    NvU32   serAddr[NVMEDIA_MAX_AGGREGATE_IMAGES];
    NvU32   brdcstSensorAddr;
    NvU32   sensorAddr[NVMEDIA_MAX_AGGREGATE_IMAGES];
    NvU32   inputOrder;
    NvMediaBool   enableExtSync;
    float   dutyRatio;
} CaptureConfigParams;

typedef struct {
    // Capture configuration
    char                    configFile[MAX_STRING_SIZE];
    NvU32                   numConfigs;
    NvU32                   configId;
    NvU32                   imagesNum;
    CaptureConfigParams     *captureConfigs;

    // Display configuration
    NvMediaBool             displayEnabled;
    NvU32                   displayId;
    NvU32                   windowId;
    NvU32                   depth;

    // Image recording
    NvMediaBool             saveEnabled;
    char                    filename[MAX_STRING_SIZE];

    // Options
    NvMediaBool             showTimeStamp;
    NvU32                   logLevel;
    NvMediaBool             enableExtSync;
    float                   dutyRatio;
} TestArgs;

#ifdef __cplusplus
}
#endif

#endif // _MAIN_H_

