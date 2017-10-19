/*
 * cuda_consumer.h
 *
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   CUDA consumer header file
//

#ifndef _CUDA_CONSUMER_H_
#define _CUDA_CONSUMER_H_

#include "cudaEGL.h"
#include "player-core-priv.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "result.h"

/* -----  Extension pointers  ---------*/
#if defined(EGL_KHR_stream) && defined(EGL_KHR_stream_consumer_gltexture)
#define EXTENSION_LIST(T) \
    T( PFNEGLCREATESTREAMKHRPROC,          eglCreateStreamKHR ) \
    T( PFNEGLDESTROYSTREAMKHRPROC,         eglDestroyStreamKHR ) \
    T( PFNEGLQUERYSTREAMKHRPROC,           eglQueryStreamKHR ) \
    T( PFNEGLQUERYSTREAMU64KHRPROC,        eglQueryStreamu64KHR ) \
    T( PFNEGLQUERYSTREAMTIMEKHRPROC,       eglQueryStreamTimeKHR ) \
    T( PFNEGLSTREAMATTRIBKHRPROC,          eglStreamAttribKHR ) \
    T( PFNEGLSTREAMCONSUMERACQUIREKHRPROC, eglStreamConsumerAcquireKHR ) \
    T( PFNEGLSTREAMCONSUMERRELEASEKHRPROC, eglStreamConsumerReleaseKHR ) \
    T( PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC, \
                        eglStreamConsumerGLTextureExternalKHR ) \
    T( PFNEGLGETSTREAMFILEDESCRIPTORKHRPROC, eglGetStreamFileDescriptorKHR) \
    T( PFNEGLCREATESTREAMFROMFILEDESCRIPTORKHRPROC, eglCreateStreamFromFileDescriptorKHR)


#define EXTLST_DECL(tx, x)  tx x = NULL;
#define EXTLST_EXTERN(tx, x) extern tx x;
#define EXTLST_ENTRY(tx, x) { (extlst_fnptr_t *)&x, #x },
#endif

#if defined(EXTENSION_LIST)
int EGLSetupExtensions (void);
int EGLStreamInit (void);
void EGLStreamFini (void);
#endif

typedef struct _test_cuda_consumer_s
{
    CUcontext context;
    CUeglStreamConnection cudaConn;
    FILE *outFile;
    guint frameCount;
    gboolean pitchLinearOutput;
    gboolean quit;
} test_cuda_consumer_s;

gboolean cuda_consumer_init (test_cuda_consumer_s *cudaConsumer, gboolean cuda_yuv_flag);
void cuda_consumer_deinit (test_cuda_consumer_s *cudaConsumer);
void* cudaConsumerProc (test_cuda_consumer_s *cudaConsumer);

#endif

