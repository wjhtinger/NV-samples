/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVMEDIA_INTEROP_H_
#define _NVMEDIA_INTEROP_H_

#include <nvcommon.h>
#include <nvmedia.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "egl_utils.h"
#include "eglstrm_setup.h"
#include "log_utils.h"

typedef enum
{
    EGLSTREAM_NVMEDIA_VIDEO = 0,
    EGLSTREAM_NVMEDIA_IMAGE = 1,
    EGLSTREAM_GL        = 2,
    EGLSTREAM_CUDA      = 3,

} ProdConsType;

typedef struct _InteropArgs {

    NvMediaDevice              *device;
    NvQueue                    *inputQueue;
    char                        *outfile;
    NvU32                       width;
    NvU32                       height;
    NvU32                       frameCount;
    ProdConsType                producer;
    ProdConsType                consumer;
    NvMediaSurfaceType          prodSurfaceType;
    NvBool                      surfTypeIsRGBA;
    NvBool                      fifoMode;
    volatile NvBool            *quit;

} InteropArgs;

typedef struct {

    // EGLStreams
    NvMediaDevice              *device;
    EglUtilState               *eglUtil ;
    EglStreamClient            *streamClient;
    void                       *producerCtxt;
    void                       *consumerCtxt;

    //EGLStreams Params
    InteropArgs                 interopParams;

    NvQueue                    *inputQueue;
    volatile NvBool             producerExited;
    volatile NvBool             consumerExited;
    NvBool                      interopExited;
    volatile NvBool            *quit;

    // General processing params
    NvU32                       width;
    NvU32                       height;
} NvInteropContext;

NvMediaStatus InteropInit(NvMainContext *mainCtx);
void InteropProc (void* data,void* user_data);
NvMediaStatus InteropFini (NvMainContext *ctx);


#endif
