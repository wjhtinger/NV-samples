/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   GL consumer header file
//

#ifndef _NVMEDIA_GL_CONSUMER_H_
#define _NVMEDIA_GL_CONSUMER_H_

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl31.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "nvmedia.h"
#include "eglstrm_setup.h"
#include "egl_utils.h"
#include "interop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    EGLDisplay                eglDisplay;
    EglStreamClient          *eglStrmCtx;
    EglUtilState             *eglUtil;
    NvU32                     width;
    NvU32                     height;
    NvU32                     frameCount;
    NvU32                     ippNum;
    pthread_t                 glConsumerThread;
    NvMediaBool              *consumerExited;
    NvMediaBool              *glConsumerInitDone;
    NvMediaBool              *quit;
    NvMediaBool              *producerExited;
    EGLSyncKHR                sync[NVMEDIA_MAX_AGGREGATE_IMAGES];
    NvMediaBool                fifoMode;

} GLConsumerCtx;

//
// Math/matrix operations
//

void
EGLUtilMatrixIdentity(
    float m[16]);

void
EGLUtilMatrixMultiply(
    float m0[16], float m1[16]);

void
EGLUtilMatrixOrtho(
    float m[16],
    float l, float r, float b, float t, float n, float f);

unsigned int
EGLUtilLoadShaderSrcStrings(
    const char* vertSrc, int vertSrcSize,
    const char* fragSrc, int fragSrcSize,
    unsigned char link,
    unsigned char debugging);

void *GlConsumerProc(void *data);
// Intialize GL Consumer context
GLConsumerCtx*
GlConsumerInit(NvMediaBool *consumerDone,
               EglStreamClient *streamClient,
               EglUtilState *state,
               InteropContext *interopCtx);

NvMediaStatus GlConsumerFini(GLConsumerCtx *ctx);
void GlConsumerStop(GLConsumerCtx *ctx);
NvMediaStatus GlConsumerFlush(EGLStreamKHR eglStream, EglUtilState *state);

#ifdef __cplusplus
}
#endif

#endif //_NVMEDIA_GL_CONSUMER_H_
