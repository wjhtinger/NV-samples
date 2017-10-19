/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __SAVE_H__
#define __SAVE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"
#include "surf_utils.h"

#define SAVE_QUEUE_SIZE                 3    /* min no. of buffers to be in circulation at any point */
#define SAVE_DEQUEUE_TIMEOUT            1000
#define SAVE_ENQUEUE_TIMEOUT            100

typedef struct {
    NvQueue                    *inputQueue;
    NvQueue                    *outputQueue;
    volatile NvMediaBool       *quit;
    NvMediaBool                 displayEnabled;
    NvMediaBool                 saveEnabled;
    NvMediaBool                 exitedFlag;

    /* save params */
    NvU32                       rawBytesPerPixel;
    NvU32                       pixelOrder;
    char                       *saveFilePrefix;
    NvU32                       numFramesToSave;
    NvU32                       virtualChannelIndex;

    /* Raw2Rgb conversion params */
    NvQueue                    *conversionQueue;
    NvMediaSurfaceType          surfType;
    NvU32                       surfAttributes;
    NvMediaImageAdvancedConfig  surfAdvConfig;
    NvU32                       width;
    NvU32                       height;
} SaveThreadCtx;

typedef struct {
    /* 2D processing */
    NvThread                   *saveThread[NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS];
    SaveThreadCtx               threadCtx[NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS];
    NvMediaDevice              *device;

    /* General processing params */
    volatile NvMediaBool       *quit;
    TestArgs                   *testArgs;
    NvMediaBool                 displayEnabled;
    NvU32                       numVirtualChannels;
} NvSaveContext;

NvMediaStatus
SaveInit(NvMainContext *mainCtx);

NvMediaStatus
SaveFini(NvMainContext *mainCtx);

NvMediaStatus
SaveProc(NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif // __SAVE_H__
