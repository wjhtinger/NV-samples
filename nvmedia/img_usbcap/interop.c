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

#include "main.h"
#include "capture.h"
#include "process2d.h"
#include "interop.h"
#include "img_producer.h"
#include "gl_consumer.h"

/* ------  globals ---------*/
#if defined(EGL_KHR_stream)
EGLStreamKHR eglStream = EGL_NO_STREAM_KHR;
#endif

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

NvMediaStatus InteropInit (NvMainContext *mainCtx)
{
    NvInteropContext *interopCtx  = NULL;
    NvProcess2DContext *proc2DCtx = NULL;

    /* Allocating Interop Context */
    mainCtx->ctxs[INTEROP]= malloc(sizeof(NvInteropContext));
    if(!mainCtx->ctxs[INTEROP]){
            LOG_ERR("%s: Failed to allocate memory for process2D context\n", __func__);
            return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    interopCtx = mainCtx->ctxs[INTEROP];
    memset(interopCtx,0,sizeof(NvInteropContext));
    proc2DCtx = mainCtx->ctxs[PROCESS_2D];

    /*Initializing Interop context*/
    interopCtx->width = mainCtx->width;
    interopCtx->height = mainCtx->height;
    interopCtx->quit =  &mainCtx->threadsExited[PROCESS_2D];
    interopCtx->device = proc2DCtx->device;

    /*Allocating Consumer and Producer Context*/
    GLConsumerTestArgs * glConsumer = malloc(sizeof(GLConsumerTestArgs));
    if(!glConsumer){
            LOG_ERR("%s: failed to create Consumer ctxt\n", __func__);
            return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }
    memset(glConsumer,0,sizeof(GLConsumerTestArgs));
    interopCtx->consumerCtxt = (void *)glConsumer;

    ImageProducerTestArgs *imgProducer = malloc(sizeof(ImageProducerTestArgs));
    if(!imgProducer){
            LOG_ERR("%s: failed to create Producer ctxt\n", __func__);
            return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }
    memset(imgProducer,0,sizeof(ImageProducerTestArgs));
    interopCtx->producerCtxt = (void *)imgProducer;


    /*Create Producer Queue*/
    if(IsFailed(NvQueueCreate(&interopCtx->inputQueue,
                              IMAGE_BUFFERS_POOL_SIZE,
                              sizeof(NvMediaImage *)))) {
            return NVMEDIA_STATUS_ERROR;
    }

    /*Initializing Params*/
    InteropArgs *interopParams = &interopCtx->interopParams;
    interopParams->device      = interopCtx->device;
    interopParams->inputQueue  = interopCtx->inputQueue;
    interopParams->width       = interopCtx->width;
    interopParams->height      = interopCtx->height;
    interopParams->fifoMode    = 0;
    interopParams->frameCount  = 0;
    interopParams->outfile     = (mainCtx->saveOutFileFlag)? mainCtx->outFileName : NULL ;
    interopParams->quit        = interopCtx->quit;
    interopParams->consumer    = EGLSTREAM_GL;

    /*Set producer and consumer surface Formats*/
    if(!strcasecmp(mainCtx->surfFmt,"420p")) {
            interopParams->surfTypeIsRGBA = NV_FALSE;
            interopParams->prodSurfaceType = NvMediaSurfaceType_Image_YUV_420;
    } else if(!strcasecmp(mainCtx->surfFmt,"rgba")) {
            interopParams->surfTypeIsRGBA = NV_TRUE;
            interopParams->prodSurfaceType = NvMediaSurfaceType_Image_RGBA;
    } else {
            LOG_ERR("Unrecognized output surface format specified: Using RGBA as output format\n");
            interopParams->surfTypeIsRGBA = NV_TRUE;
            interopParams->prodSurfaceType = NvMediaSurfaceType_Image_RGBA;
    }

    /* Init EGLStream */
    LOG_DBG("EglUtil init\n");
    EglUtilOptions options;
    memset(&options,0,sizeof(EglUtilOptions));
    options.windowOffset[0] = mainCtx->windowOffset[0];
    options.windowOffset[1] = mainCtx->windowOffset[1];
    options.windowSize[0]   = interopCtx->width;
    options.windowSize[1]   = interopCtx->height;
    options.displayId       = mainCtx->displayId;
    interopCtx->eglUtil = EGLUtilInit(&options);
    if (!interopCtx->eglUtil) {
            LOG_ERR("%s: failed to initialize egl \n");
            return NVMEDIA_STATUS_ERROR;
    }

    interopCtx->streamClient = EGLStreamSingleProcInit(interopCtx->eglUtil->display,
                                                       interopParams->fifoMode);
    if (!interopCtx->streamClient) {
            LOG_ERR("%s: failed to init EGLStream client\n", __func__);
            goto fail;
    }

    return NVMEDIA_STATUS_OK;

fail:
    LOG_DBG("%s: eglutil shut down\n", __func__);
    EGLStreamFini(interopCtx->streamClient);
    EGLUtilDeinit(interopCtx->eglUtil);

    return NVMEDIA_STATUS_NOT_INITIALIZED;

}

NvMediaStatus InteropFini (NvMainContext *mainCtx)
{
    NvInteropContext   *interopCtx = NULL;

    interopCtx = mainCtx->ctxs[INTEROP];
    NvMediaImage *image = NULL;

    if(interopCtx->inputQueue) {
        while(IsSucceed(NvQueueGet(interopCtx->inputQueue, &image, 0))) {
            if (image) {
                NvQueuePut((NvQueue *)image->tag,
                       (void *)&image,
                       ENQUEUE_TIMEOUT);
                image = NULL;
            }
        }
        LOG_DBG("\n Flushing the Interop input queue");
        NvQueueDestroy(interopCtx->inputQueue);
    }

    EGLStreamFini(interopCtx->streamClient);

    LOG_DBG("%s: eglutil shut down\n", __func__);
    EGLUtilDeinit(interopCtx->eglUtil);

    if(interopCtx->consumerCtxt)
        free(interopCtx->consumerCtxt);

    if(interopCtx->producerCtxt)
        free(interopCtx->producerCtxt);

    if(interopCtx)
        free(interopCtx);

    return NVMEDIA_STATUS_OK;
}

void InteropProc (void* data,void* user_data)
{
    NvInteropContext   *interopCtx = NULL;

    NvMainContext *mainCtx = (NvMainContext *) data;
    if(!mainCtx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return;
    }

    interopCtx = mainCtx->ctxs[INTEROP];
    ImageProducerTestArgs *imgProducer = (ImageProducerTestArgs *)interopCtx->producerCtxt;
    GLConsumerTestArgs *glConsumer = (GLConsumerTestArgs *)interopCtx->consumerCtxt;

    LOG_INFO(" interop thread is active\n");

    /* create Consumer*/
    if(glConsumerInit(&interopCtx->consumerExited, glConsumer, interopCtx->streamClient,
        interopCtx->eglUtil, &interopCtx->interopParams)) {
        LOG_ERR("%s: GLConsumer failed\n", __func__);
        return;
    }

    /* create Producer*/
    EGLint streamState = 0;
    /* wait till consumer is connected*/
    while(!(*interopCtx->quit) && streamState != EGL_STREAM_STATE_CONNECTING_KHR) {
        if(!eglQueryStreamKHR(
            interopCtx->streamClient->display,
            interopCtx->streamClient->eglStream,
            EGL_STREAM_STATE_KHR,
            &streamState)) {
            LOG_ERR("main: Before init producer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }
    }

    if (!ImageProducerInit(&interopCtx->producerExited, imgProducer, interopCtx->streamClient,
            &interopCtx->interopParams)) {
        LOG_ERR("%s: ImageProducer failed\n", __func__);
    }

    /* wait till consumer exits*/
    while(!interopCtx->consumerExited) {
        usleep(1000);
    }

    glConsumerStop(glConsumer);

    ImageProducerFlush(imgProducer);
    glConsumerFlush(glConsumer);

    ImageProducerFini(imgProducer);
    glConsumerFini(glConsumer);

    mainCtx->threadsExited[INTEROP] = NV_TRUE;
    LOG_DBG("\n Interop thread  finished \n");
}
