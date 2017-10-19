/*
 * eglstrm_setup.h
 *
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _EGLSTRM_SETUP_H_
#define _EGLSTRM_SETUP_H_

#include "nvcommon.h"
#include "egl_utils.h"

#define SOCK_PATH       "/tmp/nvmedia_egl_socket"

/* struct to give client params of the connection */
/* struct members are read-only to client */
typedef struct _EglStreamClient {
    EGLDisplay   display;
    EGLStreamKHR eglStream;
    NvBool       fifoMode;
} EglStreamClient;

void EGLStreamPrintStateInfo(EGLint streamState);

EglStreamClient *EGLStreamSingleProcInit(EGLDisplay display,
                                           NvBool fifoMode);
EglStreamClient *EGLStreamProducerProcInit(EGLDisplay display,
                                           NvBool fifoMode);
EglStreamClient *EGLStreamConsumerProcInit(EGLDisplay display,
                                           NvBool fifoMode);

void EGLStreamFini(EglStreamClient *client);

#endif /* _EGLSTRM_SETUP_H_ */
