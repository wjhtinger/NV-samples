/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _IPP_H_
#define _IPP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmdline.h"
#include "buffer_utils.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvmedia_ipp.h"
#include "nvmedia_icp.h"
#include "nvmedia_isc.h"
#include "surf_utils.h"
#include "thread_utils.h"
#include "img_dev.h"
#include "stream_top.h"
#include "main.h"

typedef struct {
    // capture sub system
    StreamConfigParams          captureSS;
    void                        *captureAndProcess;
    NvMediaBool                 useOffsetsFlag;
    NvU32                       totalFrameCount[MAX_NUM_OF_PIPES];
    NvU32                       totalFrameDrops[MAX_NUM_OF_PIPES];
    NvU64                       startStreamingTime_us;
    NvU64                       stopStreamingTime_us;
    NvU32                       imagesNum;
    NvMediaACPluginType         pluginFlag;
    volatile NvMediaBool        *pQuit;
} StreamCtx;

NvMediaStatus
StagingInit (NvMainContext *mainCtx);

NvMediaStatus
StagingStart (NvMainContext *mainCtx);

NvMediaStatus
StagingStop (NvMainContext *mainCtx);

NvMediaStatus StagingGetOutput (
        NvMainContext               *mainCtx,
        NvMediaIPPComponentOutput   *output,
        NvU32                       pipeNum,
        NvU32                       outputNum);

NvMediaStatus StagingPutOutput (
        NvMainContext               *mainCtx,
        NvMediaIPPComponentOutput   *output,
        NvU32                       pipeNum,
        NvU32                       outputNum);

NvMediaStatus
StagingFini (NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif // _IPP_H_
