/*
 * screen_consumer.c
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple screen window consumer rendering sample app
//

#include <screen_consumer.h>
#include <nvmedia.h>
#include <log_utils.h>
#include <misc_utils.h>
#include "egl_utils.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

static NvMediaStatus drawQuad(ScreenConsumer *ctx)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    // Pull in a fresh texture.
    if (1/*eglStreamConsumerAcquireAttribEXT(ctx->display, ctx->eglStream, NULL)*/) {
        EGLuint64KHR consframe = -1ll;
        EGLuint64KHR prodframe = -1ll;
        if (eglQueryStreamu64KHR(ctx->display, ctx->eglStream, EGL_CONSUMER_FRAME_KHR, &consframe) &&
            eglQueryStreamu64KHR(ctx->display, ctx->eglStream, EGL_PRODUCER_FRAME_KHR, &prodframe)) {
            LOG_DBG("frames: %llu %llu\n", prodframe, consframe);
        }
        if (ctx->fifoMode) {
            EGLTimeKHR eglCurrentTime, eglCustomerTime;
            if (eglQueryStreamTimeKHR(ctx->display, ctx->eglStream, EGL_STREAM_TIME_NOW_KHR, &eglCurrentTime)) {
                if (eglQueryStreamTimeKHR(ctx->display, ctx->eglStream, EGL_STREAM_TIME_CONSUMER_KHR, &eglCustomerTime)) {
                    if (eglCustomerTime > eglCurrentTime)
                    {
                        NvMediaTime deltaTime;
                        deltaTime.tv_sec = (eglCustomerTime-eglCurrentTime)/1000000000LL;
                        deltaTime.tv_nsec = (eglCustomerTime-eglCurrentTime)%1000000000LL;
                        nanosleep(&deltaTime, NULL);
                    }
                }
            }
        }
    } else {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("Screen Consumer: eglStreamConsumerAcquireAttribEXT-fail\n");
    }

    return status;
}

static NvU32
procThreadFunc (
    void *data)
{
    ScreenConsumer *ctx = (ScreenConsumer *)data;
    //EGLAttrib attrib_list[16 * 2 + 1] = {EGL_NONE};

    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return 0;
    }

    LOG_DBG("Screen consumer thread is active\n");

    //eglStreamConsumerQNXScreenWindowEXT(ctx->display, ctx->eglStream, attrib_list);

    while(!ctx->quit) {
        EGLint streamState = 0;
        if(!eglQueryStreamKHR(
                ctx->display,
                ctx->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("Screen consumer: eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }
        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            LOG_DBG("Screen Consumer: - EGL_STREAM_STATE_DISCONNECTED_KHR received\n");
            ctx->quit = NV_TRUE;
            goto done;
        } else if(streamState != EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
           continue;
        }

        if(drawQuad(ctx)) {
            ctx->quit = NV_TRUE;
            goto done;
        }

    }
done:
    ctx->procThreadExited = GL_TRUE;
    *ctx->consumerDone = NV_TRUE;
    return 0;
}

NvMediaStatus screenConsumer_init(volatile NvBool *consumerDone, ScreenConsumer *ctx,
                                  EGLDisplay eglDisplay, EGLStreamKHR eglStream, TestArgs *args)
{
    memset (ctx, 0, sizeof(ScreenConsumer));

    ctx->fifoMode = args->fifoMode;
    ctx->display = eglDisplay;
    ctx->eglStream = eglStream;
    LOG_DBG("Main - screenConsumer_init\n");
    ctx->consumerDone = consumerDone;
    if (IsFailed(NvThreadCreate(&ctx->procThread, &procThreadFunc, (void *)ctx, NV_THREAD_PRIORITY_NORMAL))) {
        LOG_ERR("Screen consumer init: Unable to create process thread\n");
        ctx->procThreadExited = GL_TRUE;
        return NVMEDIA_STATUS_ERROR;
    }
    return NVMEDIA_STATUS_OK;
}

void screenConsumerStop(ScreenConsumer *ctx)
{
    ctx->quit = GL_TRUE;
    if (ctx->procThread) {
        LOG_DBG("wait for Screen consumer thread exit\n");
        NvThreadDestroy(ctx->procThread);
    }
}

