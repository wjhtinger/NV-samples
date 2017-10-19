/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "log_utils.h"
#include "interop.h"
#include "gl_consumer.h"
#include "img_producer.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

NvMediaStatus
InteropInit (
    InteropContext  *interopCtx,
    IPPCtx *ippCtx,
    TestArgs *testArgs)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU32 i;
    if (!interopCtx || !ippCtx) {
        LOG_ERR("%s: Bad parameter", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    /*Initializing Interop Params*/
    interopCtx->ippNum = ippCtx->imagesNum;
    interopCtx->width  = ippCtx->inputWidth;
    interopCtx->height = ippCtx->inputHeight;
    interopCtx->device = ippCtx->device;
    interopCtx->quit   = &ippCtx->quit;
    interopCtx->showTimeStamp = ippCtx->showTimeStamp;
    interopCtx->showMetadataFlag = ippCtx->showMetadataFlag;
    interopCtx->fifoMode = testArgs->fifoMode;
    interopCtx->producerExited = NVMEDIA_TRUE;
    interopCtx->consumerExited = NVMEDIA_TRUE;
    /* Create egl Util, producer, consumer*/
    EglUtilOptions options;

    memset(&options,0,sizeof(EglUtilOptions));
    options.windowOffset[0] = 0;
    options.windowOffset[1] = 0;
    // using full screen
    options.windowSize[0]   = 0;
    options.windowSize[1]   = 0;
    options.displayId       = ippCtx->displayId;
    interopCtx->eglUtil = EGLUtilInit(&options);
    if(!interopCtx->eglUtil) {
        LOG_ERR("%s: failed to initialize egl \n");
        return NVMEDIA_STATUS_ERROR;
    }

    // Stream Init
    EglUtilState *eglUtil = interopCtx->eglUtil;
    interopCtx->eglStrmCtx = EGLStreamInit(eglUtil->display,
                        interopCtx->ippNum,interopCtx->fifoMode);
    if(!interopCtx->eglStrmCtx) {
        LOG_ERR("%s: failed to create egl stream ctx \n");
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    interopCtx->eglProdSurfaceType = NvMediaSurfaceType_Image_YUV_420;
    for(i = 0; i < interopCtx->ippNum; i++) {
        interopCtx->outputComponent[i] = ippCtx->outputComponent[i];
    }

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    InteropFini(interopCtx);
    return(status);
}

NvMediaStatus InteropProc (void* data)
{
    InteropContext  *interopCtx = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    if(!data) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return status;
    }
    interopCtx = data;
    interopCtx->consumerInitDone = NVMEDIA_FALSE;
    interopCtx->consumerExited = NVMEDIA_FALSE;

    interopCtx->consumerCtx = GlConsumerInit(&interopCtx->consumerExited,
                                             interopCtx->eglStrmCtx,
                                             interopCtx->eglUtil,
                                             interopCtx);
    if(!interopCtx->consumerCtx) {
        LOG_ERR("%s: Failed to Init glConsumer", __func__);
        goto failed;
    }

    interopCtx->producerExited = NVMEDIA_FALSE;
    interopCtx->eglProdSurfaceType = NvMediaSurfaceType_Image_YUV_420;
    interopCtx->producerCtx = ImageProducerInit(interopCtx->device,
                                                interopCtx->eglStrmCtx,
                                                interopCtx->width,
                                                interopCtx->height,
                                                interopCtx);
    if(!interopCtx->producerCtx)
    {
        LOG_ERR("%s: Failed to Init Image Producer", __func__);
        goto failed;
    }

    while (!(*interopCtx->quit) && !(interopCtx->consumerInitDone)) {
        usleep(1000);
        LOG_DBG("Waiting for consumer init to happen\n");
    }

    if(IsFailed(NvThreadCreate(&interopCtx->getOutputThread,
                               (void *)&ImageProducerProc,
                               (void *)interopCtx->producerCtx,
                               NV_THREAD_PRIORITY_NORMAL))) {
        interopCtx->producerExited = NVMEDIA_TRUE;
        goto failed;
    }

    return NVMEDIA_STATUS_OK;
failed:
    LOG_ERR("%s: InteropProc Failed", __func__);
    interopCtx->producerExited = NVMEDIA_TRUE;
    interopCtx->consumerExited = NVMEDIA_TRUE;
    return status;

}
NvMediaStatus
InteropFini (
    InteropContext  *interopCtx)
{
    while(!interopCtx->producerExited ||
          !interopCtx->consumerExited) {
        LOG_DBG("%s: Waiting for threads to quit\n", __func__);
        usleep(100);
    }
    // Gl Consumer Fini
    if(interopCtx->consumerCtx) {
        GlConsumerStop(interopCtx->consumerCtx);
        if(IsFailed(GlConsumerFini(interopCtx->consumerCtx))) {
            LOG_ERR("%s: GlConsumerFini failed \n", __func__);
        }
    }

    // Image Producer Fini
    if(interopCtx->producerCtx) {
        if(IsFailed(ImageProducerFini(interopCtx->producerCtx))) {
            LOG_ERR("%s: ImageProducerFini failed \n", __func__);
        }
    }

    // Stream Fini
    if(interopCtx->eglStrmCtx) {
        if(IsFailed(EGLStreamFini(interopCtx->eglStrmCtx))) {
            LOG_ERR("%s: EGLStreamFini failed \n", __func__);
        }
    }

    EGLUtilDeinit(interopCtx->eglUtil);
    if(interopCtx->getOutputThread) {
        NvThreadDestroy(interopCtx->getOutputThread);
    }

    free(interopCtx);
    return NVMEDIA_STATUS_OK;
}

