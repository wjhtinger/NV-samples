/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVMEDIA_EGLSTRM_SETUP_H_
#define _NVMEDIA_EGLSTRM_SETUP_H_

#include <stdio.h>

#include "nvmedia_eglstream.h"
#include "nvmedia_ipp.h"
#include "egl_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* struct to give client params of the connection */
/* struct members are read-only to client */
typedef struct _EglStreamClient {
    EGLDisplay   display;
    EGLStreamKHR eglStream[NVMEDIA_MAX_AGGREGATE_IMAGES];
    NvBool       fifoMode;
    NvU32        numofStream;
} EglStreamClient;

EglStreamClient*
EGLStreamInit(EGLDisplay display,
                        NvU32 numOfStreams,
                        NvBool fifoMode);
NvMediaStatus
EGLStreamFini(EglStreamClient *client);

#ifdef __cplusplus
}
#endif

#endif /* _NVMEDIA_EGLSTRM_SETUP_H_*/
