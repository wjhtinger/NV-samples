/*
 * cuda_consumer.h
 *
 * Copyright (c) 2014-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   CUDA consumer header file
//

#ifndef _CUDA_CONSUMER_H_
#define _CUDA_CONSUMER_H_

#include "eglstrm_setup.h"
#include "cudaEGL.h"
#include "thread_utils.h"

typedef struct _test_cuda_consumer_s
{
    CUcontext context;
    CUeglStreamConnection cudaConn;
    FILE *outFile;
    EGLStreamKHR eglStream;
    EGLDisplay display;
    unsigned int frameCount;
    NvThread *procThread;
    NvBool procThreadExited;
    NvBool quit;
    volatile NvBool *consumerDone;
} test_cuda_consumer_s;


int cuda_consumer_init(volatile NvBool *consumerDone, test_cuda_consumer_s *cudaConsumer, EGLDisplay display, EGLStreamKHR eglStream, TestArgs *args);
void cuda_consumer_Deinit(test_cuda_consumer_s *cudaConsumer);

#endif

