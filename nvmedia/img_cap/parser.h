/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __PARSER_H__
#define __PARSER_H__

#include "nvcommon.h"
#include "nvmedia.h"
#include "nvmedia_image.h"

#include "config_parser.h"
#include "misc_utils.h"
#include "log_utils.h"

#define MAX_CONFIG_SECTIONS             128
#define MAX_STRING_SIZE                 256

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

NvMediaStatus
ParseConfigFile(char *configFile,
                NvU32 *captureConfigSetsNum,
                CaptureConfigParams **captureConfigCollection);
#endif
