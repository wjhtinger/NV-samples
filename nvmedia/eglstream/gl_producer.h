/*
 * gl_producer.h
 *
 * Copyright (c) 2015-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   gl producer header file
//

#ifndef __GL_PRODUDER_H__
#define __GL_PRODUDER_H__

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <eglstrm_setup.h>
#include <stdio.h>
#include "thread_utils.h"
#include "misc_utils.h"

#define GEARS_WIDTH 800
#define GEARS_HEIGHT 480

// Camera orientation
#define VIEW_ROTX 20.0f
#define VIEW_ROTY 30.0f
#define VIEW_ROTZ  0.0f

// Distance to gears and near/far planes
#define VIEW_ZNEAR  5.0f
#define VIEW_ZGEAR 40.0f
#define VIEW_ZFAR  50.0f

#define FREE(x) \
    {\
      if (x) { \
         free(x); \
         x = NULL;\
      } \
    }
#define MEMSET  memset
#define MEMCPY  memcpy
#define POW     (float)pow
#define SQRT    (float)sqrt
#define ISQRT(val) ((float)1.0/(float)sqrt(val))
#define COS     (float)cos
#define SIN     (float)sin
#define ATAN2   (float)atan2
#define PI      (float)M_PI

#define CHECK_GL_ERROR() {\
    GLenum error = glGetError();\
    if(error != GL_NO_ERROR) {\
        printf("ogl error %s(%d): %d\n", __FILE__, __LINE__, error);\
        exit(-1);\
    } }

/* ------  globals ---------*/
// Vertex data describing the gears
typedef struct {
    int      teeth;
    GLfloat  *vertices;
    GLfloat  *normals;
    GLushort *frontbody;
    GLushort *frontteeth;
    GLushort *backbody;
    GLushort *backteeth;
    GLushort *outer;
    GLushort *inner;
} Gear;

typedef struct
{
    int             angle;
    volatile NvBool   *finishedFlag;
    int             loop;
    EGLDisplay      display;
    EGLSurface      surface;
    EGLContext      context;
    EGLConfig       config;
#if defined(EGL_KHR_stream)
    EGLStreamKHR    stream;
#endif
} gearsstate_t;

typedef struct
{
    gearsstate_t g_state;
    NvThread     *thread;
} GLTestArgs;

#if defined(EGL_KHR_stream)
int GearProducerInit(volatile NvBool *producerFinished,
                     EGLDisplay eglDisplay,
                     EGLStreamKHR eglStream,
                     TestArgs *args);
#endif

void GearProducerStop(void);
void GearProducerDeinit(void);
#endif

