/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _CMDLINE_H_
#define _CMDLINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#include "nvcommon.h"
#include "nvmedia_idp.h"
#include "nvmedia.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "config_parser.h"
#include "img_dev.h"

#include "parser.h"

#define MAX_STRING_SIZE         256

typedef struct {
    NvMediaBool                 isUsed;
    union {
        NvU32                   uIntValue;
        float                   floatValue;
        char                    stringValue[MAX_STRING_SIZE];
    };
} CmdlineParameter;

typedef struct {
    NvMediaBool                 positionSpecifiedFlag;
    NvMediaRect                 position;
    CmdlineParameter            filePrefix;
    NvU32                       logLevel;
    NvMediaBool                 displayEnabled;
    CmdlineParameter            displayId;
    CmdlineParameter            windowId;
    CmdlineParameter            depth;
    CmdlineParameter            configFile;
    CmdlineParameter            config[NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS];
    CmdlineParameter            numFrames;
    NvMediaBool                 useAggregationFlag;
    NvU32                       numSensors;
    NvU32                       numVirtualChannels;
    CaptureConfigParams        *captureConfigCollection;
    NvU32                       captureConfigSetsNum;
    NvMediaBool                 slaveTegra;
    NvMediaBool                 useVirtualChannels;
    ExtImgDevMapInfo            camMap;
    NvMediaBool                 enableExtSync;
    float                       dutyRatio;
} TestArgs;

NvMediaStatus
ParseArgs(int argc,
          char *argv[],
          TestArgs *allArgs);

void
PrintUsage(void);

#ifdef __cplusplus
}
#endif

#endif
