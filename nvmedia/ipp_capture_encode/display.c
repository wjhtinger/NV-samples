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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "display.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "op_stream_handler.h"

#define DISPLAY_QUEUE_SIZE                  10
#define DISPLAY_BUFFER_CONTEXT_QUEUE_SIZE   5
#define DISPLAY_DEQUEUE_TIMEOUT             1000

static NvU32
_DisplayThreadFunc(void* data)
{
    NvDisplayContext *displayCtx = (NvDisplayContext *)data;
    NvMediaImage *image = NULL;
    OutputBufferContext *pBufferContext = NULL;
    NvMediaImage *releaseFrames[DISPLAY_QUEUE_SIZE] = {0};
    NvMediaImage **releaseList=&releaseFrames[0];
    NvMediaStatus status;

     while (!(*displayCtx->quit)) {

        image = NULL;
        pBufferContext = NULL;

        while (NvQueueGet(displayCtx->inputQueue, &pBufferContext, DISPLAY_DEQUEUE_TIMEOUT) !=
           NVMEDIA_STATUS_OK) {
            LOG_DBG("%s: Display input queue empty\n", __func__);
            if (*displayCtx->quit)
                goto loop_done;
        }

        image = pBufferContext->output.image;

        // push the buffer context to buffer context queue
        // we will extract the buffer context whenever the
        // corresponding image buffer is returned by display
        status = NvQueuePut(displayCtx->bufferContextQueue,
                            (void *)&pBufferContext,
                            0);

        if (NVMEDIA_STATUS_OK != status) {
            LOG_ERR("%s: error saving buffer context\n",
                    __func__);

            OpStreamHandlerPutBuffer(pBufferContext);
            pBufferContext = NULL;
            *displayCtx->quit = NVMEDIA_TRUE;
            goto loop_done;
        }

        /* Display to screen */
        releaseList = &releaseFrames[0];
        status = NvMediaIDPFlip(displayCtx->idpCtx,
                                image,
                                NULL,
                                displayCtx->positionSpecifiedFlag ? &displayCtx->dstRect : NULL,
                                releaseList,
                                NULL);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: NvMediaIDPFlip failed\n", __func__);
            goto loop_done;
        }

        while (*releaseList) {
            image = *releaseList;
            pBufferContext = NULL;
            status = NvQueueGet(displayCtx->bufferContextQueue,
                                (void *)&pBufferContext,
                                0);

            if ((NVMEDIA_STATUS_OK != status) ||
                (image != pBufferContext->output.image)) {

                LOG_ERR("%s: error in buffer context management\n",
                        __func__);

                *displayCtx->quit = NVMEDIA_TRUE;
                goto loop_done;
            }

            // return the buffer back to output stream handler
            status = OpStreamHandlerPutBuffer(pBufferContext);
            pBufferContext = NULL;

            if (NVMEDIA_STATUS_OK != status) {
                LOG_ERR("%s: Failed to put image back in queue\n", __func__);
                *displayCtx->quit = NVMEDIA_TRUE;
            }

            releaseList++;
        }
    }

loop_done:

    if (pBufferContext) {
        status = OpStreamHandlerPutBuffer(pBufferContext);

        if (NVMEDIA_STATUS_OK != status) {
            LOG_ERR("%s: Failed to put image back in queue\n", __func__);
        }

        pBufferContext = NULL;
    }

    LOG_INFO("%s: Display thread exited\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
DisplayInit(NvMainContext   *mainCtx,
            NvQueue         *inputQueue)
{
    NvDisplayContext   *displayCtx  = NULL;
    TestArgs           *testArgs;
    NvMediaIDPDeviceParams outputs[MAX_OUTPUT_DEVICES];
    int outputDevicesNum = 0;
    NvMediaBool isDisplayCreated = NVMEDIA_FALSE;
    NvU32 i=0;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU32 displayId;
    NvU32 windowId;
    NvU32 depth;

    if ((NULL == mainCtx)       ||
        (NULL == mainCtx->testArgs)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    testArgs = mainCtx->testArgs;
    // no need to allocate display context if display is not enabled
    if (NVMEDIA_FALSE == testArgs->displayEnabled) {
        mainCtx->ctxs[DISPLAY_ELEMENT] = NULL;
        return NVMEDIA_STATUS_OK;
    }

    if (NULL == inputQueue) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    /* Allocating Display context */
    mainCtx->ctxs[DISPLAY_ELEMENT]= malloc(sizeof(NvDisplayContext));
    if (!mainCtx->ctxs[DISPLAY_ELEMENT]){
        LOG_ERR("%s: Failed to allocate memory for display context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    displayCtx = mainCtx->ctxs[DISPLAY_ELEMENT];
    memset(displayCtx,0,sizeof(NvDisplayContext));

    /* Initialize context */
    displayCtx->quit      =  &mainCtx->quit;
    displayCtx->positionSpecifiedFlag = testArgs->positionSpecifiedFlag;
    displayId = testArgs->displayId;
    windowId = testArgs->windowId;
    depth = testArgs->depth;
    if (displayCtx->positionSpecifiedFlag) {
        memcpy(&displayCtx->dstRect, &testArgs->position, sizeof(NvMediaRect));
    }

    displayCtx->inputQueue = inputQueue;

    // create the image buffer context queue bufferContextQueue
    if (IsFailed(NvQueueCreate(&displayCtx->bufferContextQueue,
                               DISPLAY_BUFFER_CONTEXT_QUEUE_SIZE,
                               sizeof(OutputBufferContext *)))) {
        LOG_ERR("%s: Failed to create buffer context queue\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    status = NvMediaIDPQuery(&outputDevicesNum, outputs);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed querying for available displays\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    for (i = 0; i < (NvU32)outputDevicesNum; i++) {
        if (outputs[i].displayId == displayId) {
            displayCtx->idpCtx = NvMediaIDPCreate(displayId,
                                                  windowId,
                                                  NvMediaSurfaceType_Image_YUV_420,
                                                  NULL,
                                                  outputs[i].enabled);
            if (!displayCtx->idpCtx) {
                LOG_ERR("%s: Failed to create NvMedia display\n", __func__);
                status = NVMEDIA_STATUS_ERROR;
                goto failed;
            }
            isDisplayCreated = NVMEDIA_TRUE;
            break;
        }
    }

    if (!isDisplayCreated) {
        LOG_ERR("%s: No display created\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    NvMediaIDPSetDepth(displayCtx->idpCtx, depth);

    return NVMEDIA_STATUS_OK;
failed:
    LOG_ERR("%s: Failed to initialize Display\n", __func__);
    return status;
}

static void
_destroyThread(
        NvDisplayContext *displayCtx)
{
    NvMediaStatus status;

    if (NULL == displayCtx) {
        return;
    }

    *(displayCtx->quit) = NVMEDIA_TRUE;

    /* Destroy thread */
    if (displayCtx->displayThread) {
        status = NvThreadDestroy(displayCtx->displayThread);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to destroy display thread \n", __func__);
        }
    }

    displayCtx->displayThread = NULL;
    return;
}


void
DisplayStop(NvMainContext *mainCtx)
{
    NvDisplayContext *displayCtx = NULL;

    if (!mainCtx)
        return;

    displayCtx = mainCtx->ctxs[DISPLAY_ELEMENT];
    if (!displayCtx)
        return;

    _destroyThread(displayCtx);

    return;
}

NvMediaStatus
DisplayFini(NvMainContext *mainCtx)
{
    NvDisplayContext *displayCtx = NULL;
    NvMediaStatus status;
    NvMediaImage *releaseFrames[DISPLAY_QUEUE_SIZE] = { 0 };
    NvMediaImage **releaseList = &releaseFrames[0];
    OutputBufferContext *pBufferContext;

    if (!mainCtx)
        return NVMEDIA_STATUS_OK;

    displayCtx = mainCtx->ctxs[DISPLAY_ELEMENT];
    if (!displayCtx)
        return NVMEDIA_STATUS_OK;

    *displayCtx->quit = NVMEDIA_TRUE;

    if (NULL != displayCtx->displayThread) {
        LOG_ERR("%s: Display thread active, invoke DisplayStop first\n",
                __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    _destroyThread(displayCtx);

    /* Flush the display */
    if (displayCtx->idpCtx) {
        status = NvMediaIDPFlip(displayCtx->idpCtx,
                                NULL,
                                NULL,
                                displayCtx->positionSpecifiedFlag ? &displayCtx->dstRect : NULL,
                                releaseList,
                                NULL);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: ImageDisplayFlip failed\n", __func__);
        }

        while (*releaseList) {
            pBufferContext = NULL;
            status = NvQueueGet(displayCtx->bufferContextQueue,
                                (void *)&pBufferContext,
                                0);

            if (NVMEDIA_STATUS_OK != status) {
                LOG_ERR("%s: error in buffer context management\n",
                        __func__);
            }

            if (NULL != pBufferContext) {
                OpStreamHandlerPutBuffer(pBufferContext);
            }

            releaseList++;
        }

        // destroy the buffer context queue
        NvQueueDestroy(displayCtx->bufferContextQueue);

        /* Destroy IDP context */
        if (displayCtx->idpCtx)
            NvMediaIDPDestroy(displayCtx->idpCtx);
    }

    // don't drain/destroy the input queue, the owner will
    // safely drain and destroy

    free(displayCtx);

    LOG_INFO("%s: DisplayFini done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
DisplayStart(NvMainContext *mainCtx)
{
    NvDisplayContext *displayCtx = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if (!mainCtx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }
    displayCtx = mainCtx->ctxs[DISPLAY_ELEMENT];

    if (NULL == displayCtx) {
        LOG_WARN("%s: Display is disabled\n",
                __func__);
        return status;
    }

    /* Create thread to display images */
    status = NvThreadCreate(&displayCtx->displayThread,
                            &_DisplayThreadFunc,
                            (void *)displayCtx,
                            NV_THREAD_PRIORITY_NORMAL);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create display Thread\n",
                __func__);
    }
    return status;
}
