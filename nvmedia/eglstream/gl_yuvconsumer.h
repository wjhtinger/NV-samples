/*
 * gl_yuvconsumer.h
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _GL_YUVCONSUMER_H_
#define _GL_YUVCONSUMER_H_

#include "eglstrm_setup.h"
#include "egl_utils.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cmdline.h>
#include <pthread.h>

#include "gl_consumer.h"

NvMediaStatus glConsumerInit_yuv(volatile NvBool *consumerDone, GLConsumer *ctx, EGLDisplay eglDisplay,
                                 EGLStreamKHR eglStream, EglUtilState* state, TestArgs *args);
void glConsumerCleanup_yuv(GLConsumer *ctx);

#endif
