/*
 * common.h
 *
 * Copyright (c) 2013-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __COMMON_H_
#define __COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "grutil.h"

#define DEFAULT_RENDER_WIDTH  800
#define DEFAULT_RENDER_HEIGHT 480
#define SOCK_PATH      "/tmp/nvmedia_egl_socket"

#if defined(EGL_KHR_stream)
extern EGLStreamKHR eglStream;
#endif

int  init(void);
int  init_CP (void);
void fini(void);
void drawQuad(void);
void cleanup(void);
void printFPS(void);
void closeCB(void);
void keyCB(char key, int state);
#endif
