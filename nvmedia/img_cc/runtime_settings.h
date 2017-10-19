/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __RUNTIME_SETTINGS_H__
#define __RUNTIME_SETTINGS_H__

#include "main.h"
#include "sensor_info.h"
#include "thread_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int                         argc;
    char                        *argv[50];
    NvU32                       numFrames;
    I2cCommands                 *cmds;
    char                        outputFileName[MAX_STRING_SIZE];
} RuntimeSettings;

typedef struct {
    NvThread                   *runtimeSettingsThread;
    NvMediaBool                 exitedFlag;
    volatile NvMediaBool       *quit;
    RuntimeSettings            *rtSettings;
    NvU32                       numRtSettings;
    NvU32                       currentRtSettings;
    NvU32                      *currentFrame;
    CalibrationParameters      *calParam;
} NvRuntimeSettingsContext;

NvMediaStatus
RuntimeSettingsInit(NvMainContext *mainCtx);

NvMediaStatus
RuntimeSettingsFini(NvMainContext *mainCtx);

NvMediaStatus
RuntimeSettingsProc(NvMainContext *mainCtx);

#ifdef __cplusplus
}
#endif

#endif

