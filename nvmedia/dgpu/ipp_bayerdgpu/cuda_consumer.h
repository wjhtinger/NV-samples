/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _CUDA_CONSUMER_H_
#define _CUDA_CONSUMER_H_

#include "cudaEGL.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "surf_utils.h"
#include "egl_utils.h"
#include "eglstrm_setup.h"
#include "cmdline.h"
#include <pthread.h>
#include "interop.h"

//#define TESTMODEPARAMS
typedef struct {
    void                    *ctx; //pointer to CudaConsumerCtx
    NvU32                   threadId;
} cudaThreadData;

typedef struct _CudaConsumerCtx
{
    CUcontext             context[NVMEDIA_MAX_AGGREGATE_IMAGES];
    CUeglStreamConnection cudaConn[NVMEDIA_MAX_AGGREGATE_IMAGES];
    EGLStreamKHR          eglStream[NVMEDIA_MAX_AGGREGATE_IMAGES];
    EGLDisplay            display;

    /* Params */
    char                  outFileName[NVMEDIA_MAX_AGGREGATE_IMAGES][MAX_STRING_SIZE];
    unsigned int          frameCount;

    /* Thread */
    NvThread              *procThread[NVMEDIA_MAX_AGGREGATE_IMAGES];
    NvBool                procThreadExited[NVMEDIA_MAX_AGGREGATE_IMAGES];
    cudaThreadData        threadinfo[NVMEDIA_MAX_AGGREGATE_IMAGES];
    NvMediaBool           *quit;
    NvMediaBool           *consumerDone;
#ifdef TESTMODEPARAMS
    /* Test mode */
    TestModeArgs          *testModeParams;
#endif
    NvU32                       ippNum;
    NvMediaBool              savetofile;
    NvMediaBool              checkcrc;
} CudaConsumerCtx;

#if defined(EGL_KHR_stream)
CudaConsumerCtx * CudaConsumerInit (NvMediaBool *consumerDone,
                      EglStreamClient *streamClient,
                      TestArgs *args, InteropContext *interopCtx);
#endif
void CudaConsumerFini (CudaConsumerCtx *cudaConsumer);
void CudaConsumerStop(CudaConsumerCtx *cudaConsumer);
#endif

