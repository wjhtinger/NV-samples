/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
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
#include "img_producer.h"
#include "cuda_consumer.h"

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
    interopCtx->fifoMode = NVMEDIA_TRUE;
    interopCtx->savetofile = testArgs->saveEnabled;
    strncpy(interopCtx->filename, testArgs->filename, MAX_STRING_SIZE - 1);
    interopCtx->filename[MAX_STRING_SIZE - 1] = '\0';

    ippCtx->interopCtx = (void *)interopCtx;
    /* Create egl Util, producer, consumer*/
    EglUtilOptions options;

    memset(&options,0,sizeof(EglUtilOptions));
    options.windowOffset[0] = 0;
    options.windowOffset[1] = 0;
    options.windowSize[0]   = interopCtx->width;
    options.windowSize[1]   = interopCtx->height;
    options.displayId       = ippCtx->displayId;
    interopCtx->eglUtil = EGLUtilInit(&options);
    if(!interopCtx->eglUtil) {
        LOG_ERR("%s: failed to initialize egl \n");
        return NVMEDIA_STATUS_ERROR;
    }

    if(EGLUtilInit_dGPU(interopCtx->eglUtil)) {
        LOG_ERR("Failed in EGLUtilInit_dGPU \n");
        return 1;
    }

    LOG_DBG("display_dGPU %d \n", interopCtx->eglUtil->display_dGPU);
    // Stream Init
    EglUtilState *eglUtil = interopCtx->eglUtil;
    interopCtx->eglStrmCtx = EGLStreamInit(eglUtil->display,
                                interopCtx->ippNum,interopCtx->fifoMode, eglUtil->display_dGPU);
    if(!interopCtx->eglStrmCtx) {
        LOG_ERR("%s: failed to create egl stream ctx \n");
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

/*to use iGPU instead of dGPU enable this code
    eglUtil->display_dGPU = eglUtil->display;
    interopCtx->eglStrmCtx->display_dGPU = interopCtx->eglStrmCtx->display;
    for(i = 0; i < interopCtx->ippNum; i++) {
        interopCtx->eglStrmCtx->eglStream_dGPU[i] = interopCtx->eglStrmCtx->eglStream[i];
    }
*/
    interopCtx->eglProdSurfaceType = NvMediaSurfaceType_Image_RAW;
    for(i = 0; i < interopCtx->ippNum; i++) {
        interopCtx->outputComponent[i] = ippCtx->outputComponent[i];
    }

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    InteropFini(interopCtx);
    return(status);
}

NvMediaStatus InteroppostSurface (void* data, NvMediaImage *image,
                                  NvMediaImage **retimage,
                                  NvU32 ippNum)
{
    InteropContext  *interopCtx = (InteropContext *)data;
    NvMediaStatus status;
    status = ImageProducerProc(interopCtx->producerCtx, image, retimage, ippNum);

    return status;
}

NvMediaStatus InteropProc (void* data, TestArgs *args)
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

    interopCtx->consumerCtx = CudaConsumerInit(&interopCtx->consumerExited,
                                             interopCtx->eglStrmCtx,
                                             args,
                                             interopCtx);
    if(!interopCtx->consumerCtx) {
        LOG_ERR("%s: Failed to Init CudaConsumer", __func__);
        goto failed;
    }

    interopCtx->producerExited = NVMEDIA_FALSE;
    interopCtx->producerCtx = ImageProducerInit(interopCtx->device,
                                                interopCtx->eglStrmCtx,
                                                interopCtx->width,
                                                interopCtx->height,
                                                interopCtx,
                                                args);
    if(!interopCtx->producerCtx)
    {
        LOG_ERR("%s: Failed to Init Image Producer", __func__);
        goto failed;
    }

    while (!(*interopCtx->quit) && !(interopCtx->consumerInitDone)) {
        usleep(1000);
        LOG_DBG("Waiting for consumer init to happen\n");
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
    if(interopCtx->consumerInitDone) {
        CudaConsumerStop(interopCtx->consumerCtx);

        CudaConsumerFini(interopCtx->consumerCtx);
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

    return NVMEDIA_STATUS_OK;
}

