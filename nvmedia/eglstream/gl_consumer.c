/*
 * gl_consumer.c
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <gl_consumer.h>
#include <gl_rgbconsumer.h>
#include <gl_yuvconsumer.h>
#include <nvmedia.h>
#include <log_utils.h>
#include <misc_utils.h>

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

NvMediaStatus glConsumer_init(volatile NvBool *consumerDone, GLConsumer *ctx, EGLDisplay eglDisplay,
                              EGLStreamKHR eglStream, EglUtilState* state, TestArgs *args) {
    if(!ctx || !args) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if (!args->shaderType) { // RGB shader
        LOG_DBG("%s: using RGB shader \n", __func__);
        return glConsumerInit_rgb(consumerDone, ctx, eglDisplay, eglStream, state, args);
    } else {                 // YUV shader
        LOG_DBG("%s: using YUV shader \n", __func__);
        return glConsumerInit_yuv(consumerDone, ctx,eglDisplay, eglStream, state, args);
    }
}

void glConsumerCleanup(GLConsumer *ctx) {
    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return;
    }

    if (!ctx->shaderType) {  // RGB shader
        glConsumerCleanup_rgb(ctx);
    } else {                 // YUV shader
        glConsumerCleanup_yuv(ctx);
    }
}

void glConsumerStop(GLConsumer *ctx) {
    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return;
    }

    ctx->quit = GL_TRUE;
    if (ctx->procThread) {
        LOG_DBG("wait for GL consumer thread exit\n");
        NvThreadDestroy(ctx->procThread);
    }
}

void glConsumerFlush(GLConsumer *ctx) {

    eglStreamConsumerReleaseKHR(ctx->display, ctx->eglStream);
    while(eglStreamConsumerAcquireKHR(ctx->display, ctx->eglStream)) {
        eglStreamConsumerReleaseKHR(ctx->display, ctx->eglStream);
    }
}
