/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __CMD_LINE_H__
#define __CMD_LINE_H__

#include <stdio.h>

#include "nvcommon.h"
#include "nvmedia_isp.h"

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
  int   i2cDevice;
  NvU32 csiLanes;
  NvU32 embeddedDataLinesTop;
  NvU32 embeddedDataLinesBottom;
  NvU32 max9271_address;
  NvU32 max9286_address;
  NvU32 sensor_address;
} CaptureConfigParams;

typedef struct {
    char                        configFile[MAX_STRING_SIZE];
    NvU32                       configCaptureSetUsed;
    NvU32                       logLevel;
    CaptureConfigParams        *captureConfigCollection;
    NvU32                       captureConfigSetsNum;
    NvMediaBool                 useAggregationFlag;
    NvU32                       imagesNum;
    NvMediaBool                 ispMvFlag;
    NvMediaSurfaceType          ispMvSurfaceType;
    NvMediaISPSelect            ispSelect;
    NvMediaSurfaceType          ispOutType;
    NvMediaACPluginType         pluginFlag;
    NvMediaBool                 timedRun;
    NvU32                       runningTime;
    NvMediaBool                 disableInteractiveMode;
    NvMediaBool                 saveIspMvFlag;
    char                        *saveIspMvPrefix;
    NvMediaBool                 saveMetadataFlag;
    char                        *saveMetadataPrefix;
    char                        *inputFileName;
    char                        *outputFilePrefix;
    NvMediaRawPixelOrder        inputPixelOrder;
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

#endif
