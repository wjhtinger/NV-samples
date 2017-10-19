/*
 * eglvideoproducer.h
 *
 * Copyright (c) 2013-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple cuda producer header file
//

#ifndef _CUDA_PRODUCER_H_
#define _CUDA_PRODUCER_H_
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <nvmedia.h>
#include <cudaEGL.h>
#include "log_utils.h"
#include "thread_utils.h"

typedef struct _test_cuda_producer_s
{
    //  Stream params
    char *fileName;
    FILE *fp;
    FILE *outFile;
    NvS64 fileSize;
    int   frameCount;
    int   inputUVOrderFlag;
    NvMediaBool isRgbA;
    NvMediaBool pitchLinearOutput;

    NvU32 width;
    NvU32 height;

    NvMediaDevice *device;

    CUcontext context;
    CUeglStreamConnection cudaConn;

    EGLStreamKHR eglStream;
    EGLDisplay eglDisplay;
    // Threading
    NvThread  *thread;
    volatile NvBool *producerFinished;
} test_cuda_producer_s;

#if defined(EGL_KHR_stream)
int CudaProducerInit(volatile NvBool *producerFinished, EGLDisplay eglDisplay, EGLStreamKHR eglStream, TestArgs *args);
#endif
void CudaProducerDeinit(void);
#endif

