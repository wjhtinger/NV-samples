/*
 * egl_utils.h
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _TEST_EGL_SETUP_H
#define _TEST_EGL_SETUP_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES2/gl2ext_nv.h>
#include "nvcommon.h"
#include "nvmedia.h"

/* -----  Extension pointers  ---------*/
#define EXTENSION_LIST(T) \
    T( PFNEGLQUERYDEVICESEXTPROC,          eglQueryDevicesEXT ) \
    T( PFNEGLQUERYDEVICESTRINGEXTPROC,     eglQueryDeviceStringEXT ) \
    T( PFNEGLGETPLATFORMDISPLAYEXTPROC,    eglGetPlatformDisplayEXT ) \
    T( PFNEGLGETOUTPUTLAYERSEXTPROC,       eglGetOutputLayersEXT ) \
    T( PFNEGLSTREAMCONSUMEROUTPUTEXTPROC,  eglStreamConsumerOutputEXT) \
    T( PFNEGLCREATESTREAMKHRPROC,          eglCreateStreamKHR ) \
    T( PFNEGLDESTROYSTREAMKHRPROC,         eglDestroyStreamKHR ) \
    T( PFNEGLQUERYSTREAMKHRPROC,           eglQueryStreamKHR ) \
    T( PFNEGLQUERYSTREAMU64KHRPROC,        eglQueryStreamu64KHR ) \
    T( PFNEGLQUERYSTREAMTIMEKHRPROC,       eglQueryStreamTimeKHR ) \
    T( PFNEGLSTREAMATTRIBKHRPROC,          eglStreamAttribKHR ) \
    T( PFNEGLCREATESTREAMSYNCNVPROC,       eglCreateStreamSyncNV ) \
    T( PFNEGLCLIENTWAITSYNCKHRPROC,        eglClientWaitSyncKHR ) \
    T( PFNEGLSIGNALSYNCKHRPROC,            eglSignalSyncKHR ) \
    T( PFNEGLSTREAMCONSUMERACQUIREKHRPROC, eglStreamConsumerAcquireKHR ) \
    T( PFNEGLSTREAMCONSUMERRELEASEKHRPROC, eglStreamConsumerReleaseKHR ) \
    T( PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC, \
                        eglStreamConsumerGLTextureExternalKHR ) \
    T( PFNEGLGETSTREAMFILEDESCRIPTORKHRPROC, eglGetStreamFileDescriptorKHR) \
    T( PFNEGLCREATESTREAMFROMFILEDESCRIPTORKHRPROC, eglCreateStreamFromFileDescriptorKHR) \
    T( PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC, eglCreateStreamProducerSurfaceKHR ) \
    T( PFNEGLQUERYSTREAMMETADATANVPROC,    eglQueryStreamMetadataNV ) \
    T( PFNEGLSTREAMCONSUMERACQUIREATTRIBEXTPROC, eglStreamConsumerAcquireAttribEXT ) \
    T( PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALATTRIBSNVPROC, \
                        eglStreamConsumerGLTextureExternalAttribsNV )
#define EXTLST_DECL(tx, x)  tx x = NULL;
#define EXTLST_EXTERN(tx, x) extern tx x;
#define EXTLST_ENTRY(tx, x) { (extlst_fnptr_t *)&x, #x },

typedef struct {
    int windowSize[2];                      // Window size
    int windowOffset[2];                    // Window offset
    int displayId;
    int windowId;
    NvBool vidConsumer;
} EglUtilOptions;

typedef struct _EglUtilState {
    EGLDisplay              display;
    EGLSurface              surface;
    EGLConfig               config;
    EGLContext              context;
    EGLint                  width;
    EGLint                  height;
    EGLint                  xoffset;
    EGLint                  yoffset;
    EGLint                  displayId;
    EGLint                  windowId;
    NvBool                  vidConsumer;
    EGLDisplay              display_dGPU;
    EGLContext              context_dGPU;
} EglUtilState;

int EGLUtilCreateContext(EglUtilState *state);

EglUtilState *EGLUtilInit(EglUtilOptions *);
void EGLUtilDeinit(EglUtilState *state);
void EGLUtilDestroyContext(EglUtilState *state);
int EGLUtilInit_dGPU(EglUtilState *state);
int EGLUtilCreateContext_dGPU(EglUtilState *state);

NvBool WindowSystemInit(EglUtilState *state);
void WindowSystemTerminate(void);
int WindowSystemWindowInit(EglUtilState *state);
void WindowSystemWindowTerminate(EglUtilState *state);
NvBool WindowSystemEglSurfaceCreate(EglUtilState *state);
NvBool WindowSystemInit_dGPU(EglUtilState *state);
void WindowSystemTerminate_dGPU(void);
#endif /* _TEST_EGL_SETUP_H */
