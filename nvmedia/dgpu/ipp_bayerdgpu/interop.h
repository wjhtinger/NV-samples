/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
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
#include "ipp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {

    // EGLStreams
    NvMediaDevice              *device;
    // Win Util
    EglUtilState               *eglUtil;
    EglStreamClient            *eglStrmCtx;
    void                       *producerCtx;
    void                       *consumerCtx;

    //EGLStreams Params
    NvMediaSurfaceType          eglProdSurfaceType;
    NvMediaBool                 producerExited;
    NvMediaBool                 consumerExited;
    NvMediaBool                 consumerInitDone;
    NvMediaBool                 interopExited;
    NvMediaBool                *quit;

    // General processing params
    NvU32                       width;
    NvU32                       height;
    NvU32                       ippNum;
    NvMediaIPPComponent        *outputComponent[NVMEDIA_MAX_AGGREGATE_IMAGES];
    NvMediaBool                 showTimeStamp;
    NvMediaBool                 showMetadataFlag;
    NvMediaBool                 fifoMode;
    NvMediaBool                 savetofile;
    char                        filename[MAX_STRING_SIZE];
} InteropContext;

/*  IPPInteropInit: Initiliaze context for  Interop
    gl consumer, producer*/
NvMediaStatus
InteropInit (InteropContext  *interopCtx, IPPCtx *ippCtx, TestArgs *testArgs);

/*  IPPInteropProc: Starts Interop process
    gl consumer, producer*/
NvMediaStatus
InteropProc (void* data,TestArgs *args);

/*  IPPInteropFini: clears context for  Interop
    gl consumer, producer*/
NvMediaStatus
InteropFini(InteropContext  *interopCtx);

NvMediaStatus InteroppostSurface (void* data, NvMediaImage *image,
                                  NvMediaImage **retimage, NvU32 ippNum);
#ifdef __cplusplus
}
#endif

#endif
