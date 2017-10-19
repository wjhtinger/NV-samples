/*
 * nvmimage_producer.h
 *
 * Copyright (c) 2014-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple image producer header file
//

#ifndef __NVMIMAGE_PRODUDER_H__
#define __NVMIMAGE_PRODUDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "buffer_utils.h"
#include "surf_utils.h"
#include "log_utils.h"
#include "misc_utils.h"

#include "nvcommon.h"
#include "eglstrm_setup.h"
#include "cmdline.h"
#include "nvmedia.h"
#include "nvmedia_image.h"
#include "nvmedia_isp.h"
#include "nvmedia_2d.h"
#include <nvmedia_eglstream.h>

#define AGGREAGATED_MAX_SIBLINGS_NUM    NVMEDIA_MAX_AGGREGATE_IMAGES


#define MAX_CONFIG_SECTIONS             128

#define IMAGE_BUFFERS_POOL_SIZE         4
#define BUFFER_POOL_TIMEOUT             100

typedef struct {
  char   name[MAX_STRING_SIZE];
  char   description[MAX_STRING_SIZE];
  NvU32  blendFunc;
  NvU32  alphaMode;
  NvU32  alphaValue;
  char   srcRect[MAX_STRING_SIZE];
  char   dstRect[MAX_STRING_SIZE];
  NvU32  colorStandard;
  NvU32  colorRange;
  char   cscMatrix[MAX_STRING_SIZE];
  NvU32  stretchFilter;
  NvU32  srcOverride;
  NvU32  srcOverrideAlpha;
  NvU32  dstTransform;
} ProcessorConfig;

typedef struct {

   //Output-Image Parameters
    NvMediaSurfaceType          outputSurfType;
    NvU32                       outputSurfAttributes;
    NvMediaImageAdvancedConfig  outputSurfAdvConfig;
    NvU32                       outputWidth;
    NvU32                       outputHeight;

   //Input-Image Parameters
    NvMediaSurfaceType          inputSurfType;
    NvU32                       inputSurfAttributes;
    NvMediaImageAdvancedConfig  inputSurfAdvConfig;
    NvU32                       inputWidth;
    NvU32                       inputHeight;
    NvMediaBool                 pitchLinearOutput;
    char                       *inputImages;

    NvU32                       loop;
    NvU32                       frameCount;
    NvBool                      eglOutput;

    //Image2D Params
    NvMediaDevice              *device;
    NvMedia2D                  *blitter;
    NvMedia2DBlitParameters    *blitParams;
    NvMediaRect                *dstRect;
    NvMediaRect                *srcRect;

    //Buffer-pool
    BufferPool                 *inputBuffersPool;
    BufferPool                 *outputBuffersPool;

    //EGL params
    NvMediaEGLStreamProducer   *producer;
    EGLStreamKHR                eglStream;
    EGLDisplay                  eglDisplay;

    // Threading
    NvThread                   *thread;

    NvBool                      metadataEnable;
    volatile NvBool            *producerStop;

} Image2DTestArgs;

#if defined(EGL_KHR_stream)
int Image2DInit(volatile NvBool *decodeFinished,
                EGLDisplay eglDisplay,
                EGLStreamKHR eglStream,
                TestArgs *args);
#endif
void Image2DDeinit(void);
void Image2DproducerStop(void);
void Image2DproducerFlush(void);

#ifdef __cplusplus
}
#endif

#endif // __NVMIMAGE_PRODUDER_H__
