/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __CMD_HANDLER_H__
#define __CMD_HANDLER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"

#define MAX_COMMAND_QUEUE_SIZE          20
#define QUEUE_ENQUEUE_TIMEOUT           100
#define QUEUE_DEQUEUE_TIMEOUT           100

typedef enum {
    RT_CAMERA_RESET = 0,
    RT_CAMERA_OFF,
    RT_CAMERA_ON,
    RT_CAMERA_TEMP,
} RtCommandType;

typedef struct {
    RtCommandType               type;
    NvU32                       cameraId;
} RtCommand;

typedef struct {
    /* cmd handler context */
    NvThread                   *cmdHandlerThread;
    NvQueue                    *threadQueue;
    NvMediaBool                 exitedFlag;
    volatile NvMediaBool       *quit;
} NvCmdHandlerContext;

NvMediaStatus
CmdHandlerInit(NvMainContext *mainCtx);

NvMediaStatus
CmdHandlerFini(NvMainContext *mainCtx);

NvMediaStatus
CmdHandlerProc(NvMainContext *mainCtx);

NvMediaStatus
CmdHandlerProcessRuntimeCommand(NvMainContext *mainCtx,
                                char *cmd);

#ifdef __cplusplus
}
#endif

#endif // __CMD_HANDLER_H__
