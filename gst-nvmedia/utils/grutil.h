/*
 * grutil.h
 *
 * Copyright (c) 2013-2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __GRUTIL_H
#define __GRUTIL_H

#ifdef NVMEDIATEST_KD_SUPPORT
#undef st_mtime
#include <KD/kd.h>
#include <KD/KHR_formatted.h>
#include <KD/NV_initialize.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GRUTIL_MAX_NAME 256

long long GrUtilSysTime(void);
#define SYSTIME (GrUtilSysTime)

// More complex functions have their own OS-specific implementation
#define GrUtilLog printf

//
// Window/graphics state
//

// Maximum number of attributes for EGL calls
#define MAX_ATTRIB 31

// Window System and EGL objects
typedef struct GrUtilPlatformState GrUtilPlatformState;
typedef struct {
    NativeDisplayType       nativeDisplay;
    NativeWindowType        nativeWindow;
    EGLDisplay              display;
    EGLSurface              surface;
    EGLConfig               config;
    EGLContext              context;
    EGLint                  width;
    EGLint                  height;
    GrUtilPlatformState*  platform;
} GrUtilState;

extern GrUtilState grUtilState;

// Top level initialization/termination functions
int
GrUtilInitialize(
    int* argc, char** argv, const char *appName,
    int glversion, int depthbits, int stencilbits);

int
GrUtilInitializeParsed(
    int* argc, char** argv, const char *appName,
    int glversion, int depthbits, int stencilbits);

void
GrUtilShutdown(void);

int
GrUtilCreatOutputSurface(void);

int
GrUtilCreateEGLContext(void);

// Window system specific functions
int
GrUtilDisplayInit(void);

void
GrUtilDisplayTerm(void);

int
GrUtilWindowInit(
    int* argc, char** argv,
    const char* appName);

void
GrUtilWindowTerm(void);


//
// Event handling
//

// Window system event callback types
typedef void (*GrUtilCloseCB)(void);
typedef void (*GrUtilResizeCB)(int w, int h);
typedef void (*GrUtilKeyCB)(char key, int state);
typedef void (*GrUtilPointerCB)(int x, int y);
typedef void (*GrUtilButtonCB)(int button, int state);

// Functions to set and trigger callbacks
void
GrUtilSetCloseCB(GrUtilCloseCB cb);

void
GrUtilSetResizeCB(GrUtilResizeCB cb);

void
GrUtilSetKeyCB(GrUtilKeyCB cb);

void
GrUtilSetPointerCB(GrUtilPointerCB cb);

void
GrUtilSetButtonCB(GrUtilButtonCB cb);

void
GrUtilCheckEvents(void);

// Run-time options
typedef struct {
    int windowSize[2];                      // Window size
    int windowOffset[2];                    // Window offset
    int windowOffsetValid;                  // Window offset was requested
    int desktopSize[2];                     // Root window size
    int displaySize[2];                     // Display size
    int displayRate;                        // Display refresh rate
    char displayName[GRUTIL_MAX_NAME];      // Display selection
    int displayLayer;                       // Display layer
    int displayBlend;                       // Display layer blending
    float displayAlpha;                     // Display constant blending alpha
    float displayColorKey[8];               // Display color key range
                                            // [0-3] RGBA LOW, [4-7] RGBA HIGH
    int msaa;                               // Multi-sampling
    int csaa;                               // Coverage sampling
    int buffering;                          // N-buffered swaps
} GrUtilOptions;

// Values for displayBlend option
typedef enum {
    GrUtilDisplayBlend_None = 0,   // No blending
    GrUtilDisplayBlend_ConstAlpha, // Constant value alpha blending value
    GrUtilDisplayBlend_PixelAlpha, // Per pixel alpha blending
    GrUtilDisplayBlend_ColorKey    // Color keyed blending
} GrUtilDisplayBlend;

extern GrUtilOptions grUtilOptions;

//
// Math/matrix operations
//

#define eps 1e-4

int
eq(float a, float b);

void
GrUtilMatrixIdentity(
    float m[16]);

int
GrUtilMatrixEquals(
    float a[16], float b[16]);

void
GrUtilMatrixTranspose(
    float m[16]);

void
GrUtilMatrixMultiply(
    float m0[16], float m1[16]);

void
GrUtilMatrixMultiply_4x4_3x3(
    float m0[16], float m1[9]);

void
GrUtilMatrixMultiply_3x3(
    float m0[9], float m1[9]);

void
GrUtilMatrixFrustum(
    float m[16],
    float l, float r, float b, float t, float n, float f);

void
GrUtilMatrixOrtho(
    float m[16],
    float l, float r, float b, float t, float n, float f);

void
GrUtilMatrixScale(
    float m[16], float x, float y, float z);

void
GrUtilMatrixTranslate(
    float m[16], float x, float y, float z);

void
GrUtilMatrixRotate_create3x3(
    float m[9],
    float theta, float x, float y, float z);

void
GrUtilMatrixRotate(
    float m[16], float theta, float x, float y, float z);

void
GrUtilMatrixRotate_3x3(
    float m[9], float theta, float x, float y, float z);

float
GrUtilMatrixDeterminant(
    float m[16]);

void
GrUtilMatrixInverse(
    float m[16]);

void
GrUtilMatrixCopy(
    float dest[16], float src[16]);

void
GrUtilMatrixVectorMultiply(
    float m[16], float v[4]);

void
GrUtilMatrixPrint(
    float a[16]);

//
// Shader setup
//

unsigned int
GrUtilLoadShaderSrcStrings(
    const char* vertSrc, int vertSrcSize,
    const char* fragSrc, int fragSrcSize,
    unsigned char link,
    unsigned char debugging);

#ifdef __cplusplus
}
#endif

#endif // __GRUTIL_H
