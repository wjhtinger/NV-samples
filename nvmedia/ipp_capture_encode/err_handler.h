/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __ERR_HANDLER_H__
#define __ERR_HANDLER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"
#include "img_dev.h"

typedef struct {
    /* error handler context */
    NvThread                   *errHandlerThread;
    ExtImgDevice               *extImgDevice;
    NvQueue                    *errorQueue;
    volatile NvMediaBool       *quit;
} NvErrHandlerContext;

NvMediaStatus
ErrHandlerInit(NvMainContext *mainCtx);

NvMediaStatus
ErrHandlerFini(NvMainContext *mainCtx);

NvMediaStatus
ErrHandlerStart(NvMainContext *mainCtx);

void
ErrHandlerStop(NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif // __ERR_HANDLER_H__
