/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"
#include "nvmedia.h"
#include "nvmedia_image.h"

#define DISPLAY_QUEUE_SIZE                 10
#define DISPLAY_DEQUEUE_TIMEOUT            1000
#define DISPLAY_ENQUEUE_TIMEOUT            100

typedef struct {
    /* Display context */
    NvMediaIDP                 *idpCtx;
    NvQueue                    *inputQueue;
    NvMediaDevice              *device;
    volatile NvMediaBool       *quit;
    NvThread                   *displayThread;

    /* Display related params */
    NvMediaBool                 exitedFlag;
    NvMediaBool                 displayEnabled;
    NvMediaBool                 positionSpecifiedFlag;
    NvMediaRect                 dstRect;
} NvDisplayContext;

NvMediaStatus
DisplayInit(NvMainContext *mainCtx);

NvMediaStatus
DisplayFini(NvMainContext *mainCtx);

NvMediaStatus
DisplayProc(NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif // __DISPLAY_H__
