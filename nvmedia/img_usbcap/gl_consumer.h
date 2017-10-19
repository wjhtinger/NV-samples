/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _GL_CONSUMER_H_
#define _GL_CONSUMER_H_
#include "egl_utils.h"
#include "eglstrm_setup.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct {
    EGLStreamKHR              eglStream;
    EGLDisplay                eglDisplay;
    EglUtilState             *state;
    NvU32                     width;
    NvU32                     height;
    NvBool                    surfTypeIsRGBA;
    char                     *outputFrames;
    NvU32                     frameCount;

    pthread_t                 procThread;
    GLboolean                 procThreadExited;
    volatile NvBool          *quit;
    volatile NvBool          *consumerDone;

} GLConsumerTestArgs;

NvMediaStatus glConsumerInit(volatile NvBool *consumerDone, GLConsumerTestArgs *ctx, EglStreamClient *streamClient,EglUtilState *state, InteropArgs *args);
void glConsumerFini(GLConsumerTestArgs *ctx);
void glConsumerStop(GLConsumerTestArgs *ctx);
void glConsumerFlush(GLConsumerTestArgs *ctx);

#endif
