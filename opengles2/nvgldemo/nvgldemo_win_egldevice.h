/* Copyright (c) 2014 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef __NVGLDEMO_WIN_EGLDEVICE_H
#define __NVGLDEMO_WIN_EGLDEVICE_H

#ifdef NVGLDEMO_HAS_DEVICE

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#if defined(__INTEGRITY)
#include <stdbool.h>
#else
typedef enum {
    false=0,
    true=1
} bool;
#endif

typedef enum {
    DRM_PLANE_TYPE_PRIMARY,
    DRM_PLANE_TYPE_OVERLAY,
    DRM_PLANE_TYPE_CURSOR
} drmPlaneType;

// Platform-specific state info
struct NvGlDemoPlatformState
{
    // Input - Device Instance index
    int      curDevIndx;
    // Input - Connector Index
    int      curConnIndx;
};


// EGLOutputLayer window List
struct NvGlDemoWindowDevice
{
    bool enflag;
    EGLint                  index;
    EGLStreamKHR            stream;
    EGLSurface              surface;
};

// EGLOutputDevice
struct NvGlOutputDevice
{
    bool enflag;
    EGLint               index;
    EGLDeviceEXT         device;
    EGLDisplay           eglDpy;
    EGLint               layerCount;
    EGLint               layerDefault;
    EGLint               layerIndex;
    EGLint               layerUsed;
    EGLOutputLayerEXT*   layerList;
    struct NvGlDemoWindowDevice*     windowList;
};

// Parsed DRM info structures
typedef struct {
    bool         valid;
    unsigned int     crtcMask;
    int          crtcMapping;
} NvGlDemoDRMConn;

typedef struct {
    EGLint       layer;
    unsigned int modeX;
    unsigned int modeY;
    bool         mapped;
    bool         used;
} NvGlDemoDRMCrtc;

typedef struct {
    EGLint       layer;
    unsigned int     crtcMask;
    bool         used;
    drmPlaneType planeType;
} NvGlDemoDRMPlane;

// DRM+EGLDesktop desktop class
struct NvGlDemoDRMDevice
{
    int              fd;
    const char* drmName;
    drmModeRes*      res;
    drmModePlaneRes* planes;

    int              connDefault;
    bool         isPlane;
    int             curConnIndx;
    int             currCrtcIndx;
    int             currPlaneIndx;

    unsigned int    currPlaneAlphaPropID;

    NvGlDemoDRMConn*       connInfo;
    NvGlDemoDRMCrtc*       crtcInfo;
    NvGlDemoDRMPlane*      planeInfo;
};

struct PropertyIDAddress {
    const char *name;
    uint32_t *ptr;
};

// EGL Device internal api
static bool NvGlDemoInitEglDevice(void);
static bool NvGlDemoCreateEglDevice(EGLint devIndx);
static bool NvGlDemoCreateSurfaceBuffer(void);
static void NvGlDemoResetEglDeviceLyrLst(struct NvGlOutputDevice *devOut);
static void NvGlDemoResetEglDevice(void);
static void NvGlDemoTermWinSurface(void);
static void NvGlDemoTermEglDevice(void);
static void NvGlDemoResetEglDeviceFnPtr(void);

// DRM Device internal api
static bool NvGlDemoInitDrmDevice(void);
static bool NvGlDemoCreateDrmDevice( EGLint devIndx );
static bool NvGlDemoSetDrmOutputMode( void );
static void NvGlDemoResetDrmDevice(void);
static void NvGlDemoResetDrmConcetion(void);
static void NvGlDemoTermDrmDevice(void);
static void NvGlDemoResetDrmDeviceFnPtr(void);

// Module internal api
static void NvGlDemoResetModule(void);
#endif // NVGLDEMO_HAS_DEVICE

#endif // __NVGLDEMO_WIN_EGLDEVICE_H


