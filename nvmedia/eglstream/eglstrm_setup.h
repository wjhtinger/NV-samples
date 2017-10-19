/*
 * eglstrm_setup.h
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
// DESCRIPTION:   Common EGL stream functions header file
//

#ifndef _EGLSTRM_SETUP_H_
#define _EGLSTRM_SETUP_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <nvmedia.h>
#include "nvcommon.h"
#include "cmdline.h"
#include "egl_utils.h"

#define STANDALONE_NONE 0
#define STANDALONE_PRODUCER 1
#define STANDALONE_CONSUMER 2

#define MAX_STRING_SIZE     256
#define SOCK_PATH           "/tmp/nvmedia_egl_socket"

int eglSetupExtensions(void);
void PrintEGLStreamState(EGLint streamState);
EGLStreamKHR EGLStreamInit(EGLDisplay display, TestArgs *args);
void EGLStreamFini(EGLDisplay display,EGLStreamKHR eglStream);


extern NvMediaSurfaceType surfaceType;

#endif
