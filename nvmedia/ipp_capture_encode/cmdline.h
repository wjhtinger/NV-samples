/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _CMDLINE_H_
#define _CMDLINE_H_

#include <stdio.h>
#include "img_dev.h"
#include "nvmedia_image.h"


#ifdef __cplusplus
extern "C" {
#endif

#define MAX_STRING_SIZE                 256

typedef enum
{
    NVMEDIA_NOACPLUGIN,
    NVMEDIA_SIMPLEACPLUGIN,
    NVMEDIA_NVACPLUGIN
}NvMediaACPluginType;

typedef struct {
  char  name[MAX_STRING_SIZE];
  char  description[MAX_STRING_SIZE];
  char  board[MAX_STRING_SIZE];
  char  inputDevice[MAX_STRING_SIZE];
  char  inputFormat[MAX_STRING_SIZE];
  char  surfaceFormat[MAX_STRING_SIZE];
  char  resolution[MAX_STRING_SIZE];
  char  interface[MAX_STRING_SIZE];
  NvU32 i2cDevice;
  NvU32 csiLanes;
  NvU32 embeddedDataLinesTop;
  NvU32 embeddedDataLinesBottom;
  NvU32 desAddr;
  NvU32 brdcstSerAddr;
  NvU32 serAddr[NVMEDIA_MAX_AGGREGATE_IMAGES];
  NvU32 brdcstSensorAddr;
  NvU32 sensorAddr[NVMEDIA_MAX_AGGREGATE_IMAGES];
} CaptureConfigParams;

typedef struct {
    NvU32                       outputWidth;
    NvU32                       outputHeight;
    char                        configFile[MAX_STRING_SIZE];
    NvU32                       configCaptureSetUsed;
    NvMediaBool                 displayEnabled;
    NvS32                       displayId;
    NvU32                       windowId;
    NvU32                       depth;
    NvMediaBool                 positionSpecifiedFlag;
    NvMediaRect                 position;
    NvU32                       logLevel;
    CaptureConfigParams        *captureConfigCollection;
    NvU32                       captureConfigSetsNum;
    NvU32                       imagesNum;
    NvMediaACPluginType         pluginFlag;
    NvMediaBool                 timedRun;
    NvU32                       runningTime;
    NvMediaBool                 disableInteractiveMode;
    ExtImgDevMapInfo            camMap;
    NvMediaBool                 useVirtualChannels;
    NvMediaBool                 slaveTegra;
    NvU32                       skipInitialFramesCount;

    // encode related elements //
    NvBool                      enableEncode;
    NvU32                       encodePreset;
    unsigned int                videoCodec;
    NvU32                       frameRateNum;
    NvU32                       frameRateDen;
    NvMediaBool                 enableLimitedRGB;
    NvU32                       cbrEncodedDataRateMbps;
    NvU32                       qpI;
    NvU32                       qpP;
    NvMediaBool                 losslessH265Compression;
    char                        encodeOutputFileName[MAX_STRING_SIZE];
    NvMediaBool                 enableExtSync;
    float                       dutyRatio;
} TestArgs;


int
ParseArgs (
    int argc,
    char *argv[],
    TestArgs *testArgs);

void
PrintUsage (void);

int
ParseNextCommand (
    FILE *file,
    char  *inputLine,
    char **inputTokens,
    int   *tokensNum);

#ifdef __cplusplus
}
#endif

#endif  // _CMDLINE_H_
