/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVMEDIA_IPP_MAIN_H_
#define _NVMEDIA_IPP_MAIN_H_

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

#include "nvcommon.h"
#include "nvmedia.h"
#include "nvmedia_image.h"
#include "nvmedia_isp.h"
#include "thread_utils.h"
#include "img_dev.h"


#define MAX_STRING_SIZE                 256
#define MAX_CONFIG_SECTIONS             128


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
  NvU32 inputOrder;
} CaptureConfigParams;  //对应-cf文件中的每个配置组，比如：-c dvp-ov10635-yuv422-1280x800-ab 

typedef struct {
    NvMediaSurfaceType          outputSurfType;
    NvU32                       outputSurfAttributes;
    NvMediaImageAdvancedConfig  outputSurfAdvConfig;
    NvU32                       outputWidth;
    NvU32                       outputHeight;
    char                        configFile[MAX_STRING_SIZE];
    NvU32                       config2DSetUsed;
    NvU32                       configCaptureSetUsed;
    NvU32                       displayId;
    NvMediaVideoOutputDevice    displayDevice;
    NvMediaBool                 positionSpecifiedFlag;
    NvMediaRect                 position;
    NvU32                       logLevel;
    CaptureConfigParams        *captureConfigCollection;
    NvU32                       captureConfigSetsNum;
    NvMediaBool                 useAggregationFlag;
    NvU32                       imagesNum;
    NvMediaBool                 timingFlag;
    NvMediaBool                 timingNoLockFlag;
    NvMediaBool                 useOffsetsFlag;
    NvMediaISPSelect            ispSelect;
    char                       *ispConfigFile;
    NvMediaSurfaceType          ispOutType;
    NvMediaBool                 showTimeStamp;
    NvMediaBool                 showMetadataFlag;
    NvMediaBool                 timedRun;
    NvU32                       runningTime;
    NvMediaBool                 disableInteractiveMode;
    NvMediaBool                 slaveTegra;
    NvMediaBool                 fifoMode;
    ExtImgDevMapInfo            camMap;
    NvMediaBool                 enableExtSync;
    float                       dutyRatio;
} TestArgs;

#define IsFailed(result)    result != NVMEDIA_STATUS_OK
#define IsSucceed(result)   result == NVMEDIA_STATUS_OK


#ifdef __cplusplus
}
#endif

#endif // _NVMEDIA_IPP_MAIN_H_
