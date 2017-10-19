/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __GRP_ACTIVATION_H__
#define __GRP_ACTIVATION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"
#include "i2cCommands.h"

typedef struct {
    /* grp activation context */
    NvThread                   *grpActThread;
    NvQueue                    *threadQueue;
    NvMediaBool                 exitedFlag;
    volatile NvMediaBool       *quit;
    I2cCommands                *parsedCommands;
    I2cGroups                   allGroups;

    /* grp activation params */
    NvU32                      *currentFrame;
    NvU32                       i2cDeviceNum;

} NvGrpActivationContext;

NvMediaStatus
GrpActivationInit(NvMainContext *mainCtx);

NvMediaStatus
GrpActivationFini(NvMainContext *mainCtx);

NvMediaStatus
GrpActivationProc(NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif // __GRP_ACTIVATION_H__
