/*
 * nvmvideo_producer.h
 *
 * Copyright (c) 2013-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple video decoder header file
//

#ifndef _NVMVIDEO_PRODUCER_H_
#define _NVMVIDEO_PRODUCER_H_

#include <eglstrm_setup.h>
#include <nvmedia_eglstream.h>
#include <video_parser.h>
#include "misc_utils.h"
#include "thread_utils.h"

#define MAX_DECODE_BUFFERS          17
#define MAX_DISPLAY_BUFFERS         4
#define MAX_FRAMES                  (MAX_DECODE_BUFFERS + MAX_DISPLAY_BUFFERS)

#define READ_SIZE                   (32 * 1024)

#define MAX_RENDER_SURFACE          4

#define COPYFIELD(a,b,field)        (a)->field = (b)->field

typedef enum
{
    NV_TOP_FIELD          = 0x01,
    NV_BOTTOM_FIELD       = 0x02,
    NV_FRAME_PICTURE      = 0x03
} NvPicStruct;

typedef struct _frame_buffer_s
{
    int nRefs;
    //display info
    int             width;
    int             height;
    NvPicStruct     structure;          // top-field, bottom-field, frame, etc.
    NvBool          topFieldFirst;
    NvBool          repeatFirstField;
    NvBool          progressiveFrame;
    int             frameNum;
    int             index;
    void            *pUserData;         // user data (proprietary information)
    NvMediaVideoSurface *videoSurface;
} frame_buffer_s;

typedef struct _test_video_parser_s
{
    // Context
    NvVideoCompressionStd eCodec;
    video_parser_context_s *ctx;
    NVDSequenceInfo nvsi;
    NVDParserParams nvdp;

    //  Stream params
    char *filename;
    FILE *fp;

    // Decoder params
    int decodeWidth;
    int decodeHeight;
    int displayWidth;
    int displayHeight;
    NvMediaVideoDecoder *decoder;
    int decodeCount;
    NvBool stopDecoding;
    NvBool eglOutput;
    int loop;

    // Picture buffer params
    int             nBuffers;
    int             nPicNum;
    frame_buffer_s  RefFrame[MAX_FRAMES];

    // Display params
    NvMediaDevice *device;
    NvMediaVideoMixer *mixer;
    int lDispCounter;
    int deinterlace;
    float aspectRatio;

    NvMediaTime baseTime;
    double frameTimeUSec;

    // Rendering params
    int renderWidth;
    int renderHeight;
    NvMediaVideoSurface *renderSurfaces[MAX_RENDER_SURFACE];
    NvMediaVideoSurface *freeRenderSurfaces[MAX_RENDER_SURFACE];
    NvMediaSurfaceType surfaceType;
    NvMediaEGLStreamProducer *producer;
    EGLStreamKHR eglStream;
    EGLDisplay eglDisplay;
    // Threading
    NvThread *thread;
    volatile NvBool *decodeFinished;
} test_video_parser_s;

#if defined(EGL_KHR_stream)
int VideoDecoderInit(volatile NvBool *decodeFinished, EGLDisplay eglDisplay, EGLStreamKHR eglStream, TestArgs *args);
#endif
void VideoDecoderDeinit(void);
void VideoDecoderStop(void);
void VideoDecoderFlush(void);

#endif
