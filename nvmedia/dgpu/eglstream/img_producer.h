/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __IMG_PRODUCER_H__
#define __IMG_PRODUCER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <pthread.h>
#include "surf_utils.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "thread_utils.h"
#include "nvcommon.h"
#include "cmdline.h"
#include "eglstrm_setup.h"
#include "nvmedia.h"
#include "nvmedia_image.h"
#include "nvmedia_2d.h"
#include <nvmedia_eglstream.h>

#define IMAGE_BUFFERS_POOL_SIZE 6
#define ENQUEUE_TIMEOUT         100

typedef struct {

    NvMediaDevice               *device;
    NvMediaEGLStreamProducer    *producer;
    NvQueue                     *inputQueue;
    EGLStreamKHR                eglStream;
    EGLDisplay                  display;

    /* Params */
    char                        *inpFileName;
    NvU32                       width;
    NvU32                       height;
    NvU32                       frameCount;
    NvMediaSurfaceType          surfaceType;

    /* Threads */
    pthread_t                   procThread;
    volatile NvBool             *producerDone;

    /* Test mode */
    TestModeArgs                *testModeParams;
    NvMediaImage                *fileimage;
    NvBool                      useblitpath;
    NvMedia2D                   *blitter;
} ImageProducerCtx;

#if defined(EGL_KHR_stream)
int ImageProducerInit(volatile NvBool *prodFinished,
                      ImageProducerCtx *imgProducer,
                      EglStreamClient *streamClient,
                      TestArgs *args);
#endif
void ImageProducerFini(ImageProducerCtx *imgProducer);
void ImageProducerStop(ImageProducerCtx *imgProducer);
void ImageProducerFlush(ImageProducerCtx *imgProducer);

#ifdef __cplusplus
}
#endif

#endif // __IMG_PRODUCER_H__
