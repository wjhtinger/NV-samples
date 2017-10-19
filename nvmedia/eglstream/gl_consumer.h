/*
 * gl_consumer.h
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
// DESCRIPTION:   GL consumer header file
//

#ifndef _GL_CONSUMER_H_
#define _GL_CONSUMER_H_

#include "eglstrm_setup.h"
#include "egl_utils.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cmdline.h>
#include "thread_utils.h"

typedef struct {
    EGLSyncKHR sync;
    GLboolean fifoMode;
    GLboolean quit;
    NvMediaSurfaceType consSurfaceType;
    EglUtilState* state;
    EGLStreamKHR eglStream;
    EGLDisplay display;
    int width;
    int height;
    GLboolean procThreadExited;
    NvThread  *procThread;
    volatile NvBool *consumerDone;
    GLboolean shaderType;
} GLConsumer;

NvMediaStatus glConsumer_init(volatile NvBool *consumerDone, GLConsumer *ctx, EGLDisplay eglDisplay,
                              EGLStreamKHR eglStream, EglUtilState* state, TestArgs *args);
void glConsumerCleanup(GLConsumer *ctx);
void glConsumerStop(GLConsumer *ctx);
void glConsumerFlush(GLConsumer *ctx);

#endif
