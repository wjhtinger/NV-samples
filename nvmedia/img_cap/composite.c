/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <limits.h>
#include <math.h>

#include "composite.h"
#include "save.h"
#include "display.h"

static NvMediaStatus
_CreateImageQueue(NvMediaDevice *device,
                  NvQueue **queue,
                  NvU32 queueSize,
                  NvU32 width,
                  NvU32 height,
                  NvMediaSurfaceType surfType,
                  NvU32 surfAttributes,
                  NvMediaImageAdvancedConfig *config)
{
    NvU32 j = 0;
    NvMediaImage *image = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if (NvQueueCreate(queue,
                      queueSize,
                      sizeof(NvMediaImage *)) != NVMEDIA_STATUS_OK) {
       LOG_ERR("%s: Failed to create image Queue \n", __func__);
       goto failed;
    }

    for (j = 0; j < queueSize; j++) {
        image = NvMediaImageCreate(device,                           // device
                                   surfType,                         // surface type
                                   NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE, // image class
                                   1,                                // images count
                                   width,                            // surf width
                                   height,                           // surf height
                                   surfAttributes,                   // attributes
                                   config);                          // config
        if (!image) {
            LOG_ERR("%s: NvMediaImageCreate failed for image %d",
                        __func__, j);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }

        image->tag = *queue;

        if (IsFailed(NvQueuePut(*queue,
                                (void *)&image,
                                NV_TIMEOUT_INFINITE))) {
            LOG_ERR("%s: Pushing image to image queue failed\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return status;
}

static NvU32
_CompositeThreadFunc(void *data)
{
    NvCompositeContext *compCtx= (NvCompositeContext *)data;
    NvMediaImage *imageIn[NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS] = {0};
    NvMediaImage *compImage = NULL;
    NvU32 i = 0;
    NvMediaStatus status;
    NvMediaRect dstRect;

    while (!(*compCtx->quit)) {
        /* Acquire all the images from capture queues */
        for (i = 0; i < compCtx->numVirtualChannels; i++) {
            imageIn[i] = NULL;
            while (NvQueueGet(compCtx->inputQueue[i],
                              (void *)&imageIn[i],
                              COMPOSITE_DEQUEUE_TIMEOUT) != NVMEDIA_STATUS_OK) {
                LOG_DBG("%s: Waiting for input image from queue %d\n", __func__, i);
                if (*compCtx->quit) {
                    goto loop_done;
                }
            }
        }

        /* Acquire image for storing composited images */
        while (NvQueueGet(compCtx->compositeQueue,
                          (void *)&compImage,
                          COMPOSITE_DEQUEUE_TIMEOUT) != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: compositeQueue is empty\n", __func__);
            if (*compCtx->quit)
                goto loop_done;
        }

        /* Blit all images to one image */
        for (i = 0; i < compCtx->numVirtualChannels; i++) {
            dstRect.x0 = imageIn[i]->width * i;
            dstRect.x1 = imageIn[i]->width * (i + 1);
            dstRect.y0 = 0;
            dstRect.y1 = imageIn[i]->height;

            status = NvMedia2DBlit(compCtx->i2d,
                                   compImage,
                                   &dstRect,
                                   imageIn[i],
                                   NULL,
                                   &compCtx->blitParams);
            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: NvMedia2DBlit failed\n", __func__);
                *compCtx->quit = NVMEDIA_TRUE;
                goto loop_done;
            }
        }

        /* Put composited image onto output queue */
        while (NvQueuePut(compCtx->outputQueue,
                          (void *)&(compImage),
                          COMPOSITE_ENQUEUE_TIMEOUT) != NVMEDIA_STATUS_OK) {
            LOG_DBG("%s: Waiting to acquire bufffer\n", __func__);
            if (*compCtx->quit)
                goto loop_done;
        }
        compImage = NULL;

    loop_done:
        for (i = 0; i < compCtx->numVirtualChannels; i++) {
            if (imageIn[i]) {
                if (NvQueuePut((NvQueue *)imageIn[i]->tag,
                               (void *)&imageIn[i],
                               0) != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to put the image back to queue\n", __func__);
                }
            }
            imageIn[i] = NULL;
        }
        if (compImage) {
            if (NvQueuePut((NvQueue *) compImage->tag,
                           (void *) &compImage,
                           0) != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to put the image back to compositeQueue\n", __func__);
            }
            compImage = NULL;
        }
    }
    LOG_INFO("%s: Composite thread exited\n", __func__);
    compCtx->exitedFlag = NVMEDIA_TRUE;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CompositeInit(NvMainContext *mainCtx)
{
    NvCompositeContext *compCtx  = NULL;
    NvSaveContext   *saveCtx = NULL;
    TestArgs           *testArgs = mainCtx->testArgs;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU32 i = 0;
    NvU32 surfAttributes = 0;
    NvMediaImageAdvancedConfig surfAdvConfig;
    NvU32 width = 0, height = 0;

    /* allocating Composite context */
    mainCtx->ctxs[COMPOSITE_ELEMENT]= malloc(sizeof(NvCompositeContext));
    if (!mainCtx->ctxs[COMPOSITE_ELEMENT]){
        LOG_ERR("%s: Failed to allocate memory for composite context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    compCtx = mainCtx->ctxs[COMPOSITE_ELEMENT];
    memset(compCtx,0,sizeof(NvCompositeContext));
    saveCtx = mainCtx->ctxs[SAVE_ELEMENT];

    /* initialize context */
    compCtx->quit      =  &mainCtx->quit;
    compCtx->numVirtualChannels = testArgs->numVirtualChannels;
    compCtx->displayEnabled = testArgs->displayEnabled;
    compCtx->exitedFlag = NVMEDIA_TRUE;
    memset(&surfAdvConfig, 0 , sizeof(NvMediaImageAdvancedConfig));

    /* Create NvMedia Device */
    compCtx->device = NvMediaDeviceCreate();
    if (!compCtx->device) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("%s: Failed to create NvMedia device\n", __func__);
        goto failed;
    }

    /* create i2d handle */
    compCtx->i2d = NvMedia2DCreate(compCtx->device);
    if (!compCtx->i2d) {
        LOG_ERR("%s: Failed to create NvMedia 2D i2d\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    /* Create input Queues */
    for (i = 0; i < compCtx->numVirtualChannels; i++) {
        if (NvQueueCreate(&compCtx->inputQueue[i],
                          COMPOSITE_QUEUE_SIZE,
                          sizeof(NvMediaImage *)) != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to create Composite inputQueue %d\n",
                    __func__, i);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }

    /* Get the output width and height for composition */
    for (i = 0; i < compCtx->numVirtualChannels; i++) {
        width += saveCtx->threadCtx[i].width;
        height = (saveCtx->threadCtx[i].height > height)?
                      saveCtx->threadCtx[i].height : height;
    }

    surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_DISPLAY;

    /* Create compositeQueue for storing composited Images */
    status = _CreateImageQueue(compCtx->device,
                               &compCtx->compositeQueue,
                               COMPOSITE_QUEUE_SIZE,
                               width,
                               height,
                               NvMediaSurfaceType_Image_RGBA,
                               surfAttributes,
                               &surfAdvConfig);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: compositeQueue creation failed\n", __func__);
        goto failed;
    }

    LOG_DBG("%s: Composite Queue: %ux%u, images: %u \n",
        __func__, width, height, COMPOSITE_QUEUE_SIZE);

    return NVMEDIA_STATUS_OK;
failed:
    LOG_ERR("%s: Failed to initialize Composite\n", __func__);
    return status;
}

NvMediaStatus
CompositeFini(NvMainContext *mainCtx)
{
    NvCompositeContext *compCtx = NULL;
    NvMediaImage *image = NULL;
    NvMediaStatus status;
    NvU32 i;

    if (!mainCtx)
        return NVMEDIA_STATUS_OK;

    compCtx = mainCtx->ctxs[COMPOSITE_ELEMENT];
    if (!compCtx)
        return NVMEDIA_STATUS_OK;

    /* Wait for composite thread to exit */
    while (!compCtx->exitedFlag) {
        LOG_DBG("%s: Waiting for composite thread to quit\n", __func__);
    }

    *compCtx->quit = NVMEDIA_TRUE;

    /* Destroy thread */
    if (compCtx->compositeThread) {
        status = NvThreadDestroy(compCtx->compositeThread);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to destroy composite thread \n", __func__);
        }
    }

    /* Destroy the compositeQueue*/
    if (compCtx->compositeQueue) {
        while ((NvQueueGet(compCtx->compositeQueue,
                           &image,
                           0)) == NVMEDIA_STATUS_OK) {
            if (image) {
                NvMediaImageDestroy(image);
                image = NULL;
            }
        }
        LOG_DBG("%s: Destroying CompositeQueue \n", __func__);
        NvQueueDestroy(compCtx->compositeQueue);
    }

    /*Flush and destroy the input queues*/
    for (i = 0; i < compCtx->numVirtualChannels; i++) {
        if (compCtx->inputQueue[i]) {
            LOG_DBG("%s: Flushing the composite input queue %d", __func__, i);
            while (IsSucceed(NvQueueGet(compCtx->inputQueue[i],
                                        &image,
                                        COMPOSITE_DEQUEUE_TIMEOUT))) {
                if (image) {
                    if (NvQueuePut((NvQueue *)image->tag,
                                   (void *)&image,
                                   0) != NVMEDIA_STATUS_OK) {
                        LOG_ERR("%s: Failed to put image back in queue\n", __func__);
                        break;
                    }
                }
                image=NULL;
            }
            NvQueueDestroy(compCtx->inputQueue[i]);
        }
    }

    if (compCtx->i2d)
        NvMedia2DDestroy(compCtx->i2d);

    if (compCtx->device)
        NvMediaDeviceDestroy(compCtx->device);

    if (compCtx)
        free(compCtx);

    LOG_INFO("%s: CompositeFini done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CompositeProc(NvMainContext *mainCtx)
{
    NvCompositeContext *compCtx = NULL;
    NvDisplayContext   *displayCtx = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if (!mainCtx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }
    compCtx = mainCtx->ctxs[COMPOSITE_ELEMENT];
    displayCtx = mainCtx->ctxs[DISPLAY_ELEMENT];

    /* Create thread to blit images */
    if (compCtx->displayEnabled) {

        /*Setting the queues*/
        compCtx->outputQueue = displayCtx->inputQueue;

        compCtx->exitedFlag = NVMEDIA_FALSE;
        status = NvThreadCreate(&compCtx->compositeThread,
                                &_CompositeThreadFunc,
                                (void *)compCtx,
                                NV_THREAD_PRIORITY_NORMAL);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to create composite Thread\n",
                    __func__);
            compCtx->exitedFlag = NVMEDIA_TRUE;
        }
    }
    return status;
}
