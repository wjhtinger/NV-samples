/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ipp.h"
#include "eglstrm_setup.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

static EGLStreamKHR
EGLStreamCreate(EGLDisplay display,
                NvBool fifoMode)
{
#ifdef EGL_NV_stream_metadata
    static const EGLint streamAttrMailboxMode[] = { EGL_METADATA0_SIZE_NV, 32*1024,
                                                    EGL_METADATA1_SIZE_NV, 16*1024,
                                                    EGL_METADATA2_SIZE_NV, 16*1024,
                                                    EGL_METADATA3_SIZE_NV, 16*1024, EGL_NONE };

    static const EGLint streamAttrFIFOMode[] = { EGL_STREAM_FIFO_LENGTH_KHR, 4,
                                                 EGL_METADATA0_SIZE_NV, 32*1024,
                                                 EGL_METADATA1_SIZE_NV, 16*1024,
                                                 EGL_METADATA2_SIZE_NV, 16*1024,
                                                 EGL_METADATA3_SIZE_NV, 16*1024, EGL_NONE };
#else
    static const EGLint streamAttrMailboxMode[] = { EGL_NONE };
    static const EGLint streamAttrFIFOMode[] = { EGL_STREAM_FIFO_LENGTH_KHR, 4,
                                                 EGL_NONE };

#endif //EGL_NV_stream_metadata

    return(eglCreateStreamKHR(display,
                fifoMode ? streamAttrFIFOMode : streamAttrMailboxMode));
}

EglStreamClient*
EGLStreamInit(EGLDisplay display,
                        NvU32 numOfStreams,
              NvBool fifoMode,
              EGLDisplay display_dGPU) {
    NvU32 i;
    EglStreamClient *client = NULL;
    EGLNativeFileDescriptorKHR fileDescriptor;

    client = malloc(sizeof(EglStreamClient));
    if (!client) {
        LOG_ERR("%s:: failed to alloc memory\n", __func__);
        return NULL;
    }

    client->numofStream = numOfStreams;
    client->display = display;
    client->display_dGPU = display_dGPU;
    client->fifoMode = fifoMode;

    for(i=0; i< numOfStreams; i++) {
        // Create with FIFO mode
        client->eglStream_dGPU[i] = EGL_NO_STREAM_KHR;
        client->eglStream_dGPU[i] = EGLStreamCreate(display_dGPU, fifoMode);
        if (client->eglStream_dGPU[i] == EGL_NO_STREAM_KHR) {
            LOG_ERR("%s: Couldn't create EGL StreamfordGPU .\n", __func__);
            goto fail;
        }

        fileDescriptor = eglGetStreamFileDescriptorKHR(display_dGPU, client->eglStream_dGPU[i]);
        if(fileDescriptor == EGL_NO_FILE_DESCRIPTOR_KHR) {
            LOG_ERR("%s: Cannot get EGL file descriptor for streamindex %d\n", __func__, i);
            eglDestroyStreamKHR(display_dGPU, client->eglStream_dGPU[i]);
            goto fail;
        }

        client->eglStream[i] = EGL_NO_STREAM_KHR;
        client->eglStream[i] = eglCreateStreamFromFileDescriptorKHR(display,
                                                fileDescriptor);
        if (client->eglStream[i] == EGL_NO_STREAM_KHR) {
            LOG_ERR("%s: Couldn't create EGL Stream from fd with index %d.\n", __func__, i);
            goto fail;
        }
        close(fileDescriptor);

        if(!eglStreamAttribKHR(client->display, client->eglStream[i], EGL_CONSUMER_LATENCY_USEC_KHR, 16000)) {
            LOG_ERR("EGLStreamSetAttr: eglStreamAttribKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
            goto fail;
        }
        if(!eglStreamAttribKHR(client->display, client->eglStream[i], EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, 16000)) {
            LOG_ERR("EGLStreamSetAttr: eglStreamAttribKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
            goto fail;
        }

        if(!eglStreamAttribKHR(client->display_dGPU, client->eglStream_dGPU[i], EGL_CONSUMER_LATENCY_USEC_KHR, 16000)) {
            LOG_ERR("EGLStreamSetAttr: eglStreamAttribKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
            goto fail;
        }
        if(!eglStreamAttribKHR(client->display_dGPU, client->eglStream_dGPU[i], EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, 16000)) {
            LOG_ERR("EGLStreamSetAttr: eglStreamAttribKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
            goto fail;
        }
    }
    return client;
fail:
    EGLStreamFini(client);
    return NULL;
}

NvMediaStatus EGLStreamFini(EglStreamClient *client) {
    NvU32 i;
    if(client) {
        for(i=0; i<client->numofStream; i++) {
            if(client->eglStream[i]) {
                eglDestroyStreamKHR(client->display, client->eglStream[i]);
            }
            if(client->eglStream_dGPU[i]) {
                if(!eglDestroyStreamKHR(client->display_dGPU, client->eglStream_dGPU[i])) {
                    LOG_ERR("Error occured in eglDestroyStreamKHR\n");
                }
            }
        }
        free(client);
    }
    return NVMEDIA_STATUS_OK;
}

