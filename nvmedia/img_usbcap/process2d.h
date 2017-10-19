/* Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __PROCESS_2D_H__
#define __PROCESS_2D_H__

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

#include "main.h"
#include "nvmedia_2d.h"

typedef struct {
    /* 2D processing */
    NvMediaDevice              *device;
    NvMedia2D                  *i2d;
    NvMediaRect                *dstRect;
    NvMediaRect                *srcRect;
    NvMedia2DBlitParameters    *blitParams;

    NvQueue                    *processQueue;
    NvQueue                    *inputQueue;
    NvQueue                    *outputQueue;
    volatile NvBool            *quit;

    /* surface related params */
    NvU32                       width;
    NvU32                       height;
    char                        surfFmt[256];
    NvMediaSurfaceType          outputSurfType;
    NvU32                       outputSurfAttributes;
    NvMediaImageAdvancedConfig  outputSurfAdvConfig;
} NvProcess2DContext;

NvMediaStatus Process2DInit (NvMainContext *mainCtx);
void Process2DProc (void* data,void* user_data);
NvMediaStatus Process2DFini (NvMainContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // __PROCESS_2D_H__
