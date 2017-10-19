/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __CAPTURE_STATUS_H__
#define __CAPTURE_STATUS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"

typedef struct {
    /* capture status context */
    NvThread                   *capStatusThread;
    NvQueue                    *threadQueue;
    NvMediaBool                 exitedFlag;
    volatile NvMediaBool       *quit;
    I2cCommands                *parsedCommands;
    I2cGroups                   allGroups;

    /* capture status params */
    NvU32                      *currentFrame;
} NvCaptureStatusContext;

NvMediaStatus
CaptureStatusInit(NvMainContext *mainCtx);

NvMediaStatus
CaptureStatusFini(NvMainContext *mainCtx);

NvMediaStatus
CaptureStatusProc(NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif // __CAPTURE_STATUS_H__
