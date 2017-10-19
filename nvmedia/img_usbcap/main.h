/* Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __MAIN_H__
#define __MAIN_H__

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

#include "cmdline.h"
#include "thread_utils.h"

enum {
    USB_CAPTURE = 0,
    PROCESS_2D,
    INTEROP,
    MAX_NUM_ELEMENTS,
};

typedef struct {
    void                        *ctxs[MAX_NUM_ELEMENTS];
    NvThread                    *threads[MAX_NUM_ELEMENTS];
    NvBool                      threadsExited[MAX_NUM_ELEMENTS];
    /*Configs*/
    NvU32                       height;
    NvU32                       width;
    volatile NvBool             quit;
    char                        inpFmt[MAX_STRING_SIZE];
    char                        surfFmt[MAX_STRING_SIZE];
    char                        *devPath;
    int                         windowOffset[2];
    NvU32                       displayId;
    NvBool                      saveOutFileFlag;
    char                        *outFileName;
} NvMainContext;

#define IsFailed(result)    result != NVMEDIA_STATUS_OK
#define IsSucceed(result)   result == NVMEDIA_STATUS_OK


#ifdef __cplusplus
}
#endif

#endif // __MAIN_H__
