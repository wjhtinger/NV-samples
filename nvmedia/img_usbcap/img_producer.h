/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __IMAGE_PRODUCER_H__
#define __IMAGE_PRODUCER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <pthread.h>
#include "log_utils.h"

#include "nvcommon.h"
#include "eglstrm_setup.h"
#include "nvmedia.h"
#include "nvmedia_image.h"
#include <nvmedia_eglstream.h>

typedef struct {
    NvMediaDevice              *device;
    char                       *inputImages;
    NvU32                       width;
    NvU32                       height;
    NvU32                       frameCount;
    NvMediaSurfaceType          surfaceType;

    //Buffer-pool
    NvQueue                    *inputQueue;

    //EGL params
    NvMediaEGLStreamProducer   *producer;
    EGLStreamKHR                eglStream;
    EGLDisplay                  eglDisplay;

    volatile NvBool            *producerStop;
    volatile NvBool            *quit;


} ImageProducerTestArgs;

#if defined(EGL_KHR_stream)
int ImageProducerInit(volatile NvBool *prodFinished,
                      ImageProducerTestArgs *imgProducer,
                      EglStreamClient *streamClient,
                      InteropArgs *args);
#endif
void ImageProducerFini(ImageProducerTestArgs *imgProducer);
void ImageProducerFlush(ImageProducerTestArgs *imgProducer);

#ifdef __cplusplus
}
#endif

#endif // __IMAGE_PRODUCER_H__
