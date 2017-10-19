/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __COMPOSITE_H__
#define __COMPOSITE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"
#include "nvmedia_2d.h"
#include "nvmedia_icp.h"

#define COMPOSITE_QUEUE_SIZE                 3     /* min no. of buffers to be in circulation at any point */
#define COMPOSITE_DEQUEUE_TIMEOUT            1000
#define COMPOSITE_ENQUEUE_TIMEOUT            100

typedef struct {
    /* composite context */
    NvQueue                    *inputQueue[NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS];
    NvQueue                    *outputQueue;
    NvQueue                    *compositeQueue;
    NvThread                   *compositeThread;
    NvMediaDevice              *device;
    NvMedia2D                  *i2d;
    NvMedia2DBlitParameters     blitParams;
    volatile NvMediaBool       *quit;
    NvMediaBool                 exitedFlag;

    /* General processing params */
    NvU32                       numVirtualChannels;
    NvMediaBool                 displayEnabled;
} NvCompositeContext;

NvMediaStatus
CompositeInit(NvMainContext *mainCtx);

NvMediaStatus
CompositeFini(NvMainContext *mainCtx);

NvMediaStatus
CompositeProc(NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif // __COMPOSITE_H__
