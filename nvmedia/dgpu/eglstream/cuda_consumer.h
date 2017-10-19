/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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

typedef struct _CudaConsumerCtx
{
    CUcontext             context;
    CUeglStreamConnection cudaConn;
    EGLStreamKHR          eglStream;
    EGLDisplay            display;

    /* Params */
    char                  *outFileName;
    unsigned int          frameCount;

    /* Thread */
    pthread_t             procThread;
    NvBool                procThreadExited;
    NvBool                quit;
    volatile NvBool       *consumerDone;

    /* Test mode */
    TestModeArgs          *testModeParams;

} CudaConsumerCtx;

#if defined(EGL_KHR_stream)
int CudaConsumerInit (volatile NvBool *consumerDone,
                      CudaConsumerCtx *cudaConsumer,
                      EglStreamClient *streamClient,
                      TestArgs *args);
#endif
void CudaConsumerFini (CudaConsumerCtx *cudaConsumer);
#endif

