/*
 * screen_consumer.h
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   screen window consumer header file
//

#ifndef _SCREEN_CONSUMER_H_
#define _SCREEN_CONSUMER_H_

#include "eglstrm_setup.h"
#include "egl_utils.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cmdline.h>
#include "thread_utils.h"

typedef struct {
    GLboolean fifoMode;
    EGLStreamKHR eglStream;
    EGLDisplay display;
    GLboolean quit;
    GLboolean procThreadExited;
    NvThread  *procThread;
    volatile NvBool *consumerDone;
} ScreenConsumer;

NvMediaStatus screenConsumer_init(volatile NvBool *consumerDone, ScreenConsumer *ctx,
                                  EGLDisplay eglDisplay, EGLStreamKHR eglStream, TestArgs *args);
void screenConsumerStop(ScreenConsumer *ctx);

#endif
