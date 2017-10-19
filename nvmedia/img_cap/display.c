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

#include "display.h"

static NvU32
_DisplayThreadFunc(void* data)
{
    NvDisplayContext *displayCtx = (NvDisplayContext *)data;
    NvMediaImage *image = NULL;
    NvMediaImage *releaseFrames[DISPLAY_QUEUE_SIZE] = {0};
    NvMediaImage **releaseList=&releaseFrames[0];
    NvMediaStatus status;

    while (!(*displayCtx->quit)) {

        image = NULL;
        while (NvQueueGet(displayCtx->inputQueue, &image, DISPLAY_DEQUEUE_TIMEOUT) !=
           NVMEDIA_STATUS_OK) {
            LOG_DBG("%s: Display input queue empty\n", __func__);
            if (*displayCtx->quit)
                goto loop_done;
        }

        /* Display to screen */
        if (displayCtx->displayEnabled) {
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
                if (NvQueuePut((NvQueue *)image->tag,
                               (void *)&image,
                               0) != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to put image back in queue\n", __func__);
                    *displayCtx->quit = NVMEDIA_TRUE;
                    goto loop_done;
                }
                releaseList++;
            }
            image=NULL;
        }

    loop_done:
        if (image) {
            if (NvQueuePut((NvQueue *)image->tag,
                           (void *)&image,
                           0) != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to put image back in queue\n", __func__);
                *displayCtx->quit = NVMEDIA_TRUE;
            }
            image = NULL;
        }
    }

    LOG_INFO("%s: Display thread exited\n", __func__);
    displayCtx->exitedFlag = NVMEDIA_TRUE;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
DisplayInit(NvMainContext *mainCtx)
{
    NvDisplayContext   *displayCtx  = NULL;
    TestArgs           *testArgs = mainCtx->testArgs;
    NvMediaIDPDeviceParams outputs[MAX_OUTPUT_DEVICES];
    int outputDevicesNum = 0;
    NvMediaBool isDisplayCreated = NVMEDIA_FALSE;
    NvU32 i=0;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvMediaBool isDisplayIdProvided;
    NvU32 displayId;
    NvU32 windowId;
    NvU32 depth;

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
    displayCtx->displayEnabled = testArgs->displayEnabled;
    displayCtx->positionSpecifiedFlag = testArgs->positionSpecifiedFlag;
    displayCtx->exitedFlag = NVMEDIA_TRUE;
    isDisplayIdProvided = testArgs->displayId.isUsed;
    displayId = testArgs->displayId.uIntValue;
    windowId = testArgs->windowId.uIntValue;
    depth = testArgs->depth.uIntValue;
    if (displayCtx->positionSpecifiedFlag) {
        memcpy(&displayCtx->dstRect, &testArgs->position, sizeof(NvMediaRect));
    }

    /* Create Display Input Queue */
    if (IsFailed(NvQueueCreate(&displayCtx->inputQueue,
                               DISPLAY_QUEUE_SIZE,
                               sizeof(NvMediaImage *)))) {
        LOG_ERR("%s: Failed to create queue\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    if (displayCtx->displayEnabled) {
        status = NvMediaIDPQuery(&outputDevicesNum, outputs);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed querying for available displays\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }

        if (outputDevicesNum > 0 && !isDisplayIdProvided) {
            displayCtx->idpCtx = NvMediaIDPCreate(outputs[0].displayId,                 // Display ID
                                                  windowId,                             // Window ID
                                                  NvMediaSurfaceType_Image_RGBA,        // Surface type
                                                  NULL,                                 // Display prefs
                                                  outputs[0].enabled);                  // Already created
            if (!displayCtx->idpCtx) {
                LOG_ERR("%s: Failed to create NvMedia display\n", __func__);
                status = NVMEDIA_STATUS_ERROR;
                goto failed;
            }
            isDisplayCreated = NVMEDIA_TRUE;
        } else {
            for (i = 0; i < (NvU32)outputDevicesNum; i++) {
                if (outputs[i].displayId == displayId) {
                    displayCtx->idpCtx = NvMediaIDPCreate(displayId,
                                                          windowId,
                                                          NvMediaSurfaceType_Image_RGBA,
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
        }

        if (!isDisplayCreated) {
            LOG_ERR("%s: No display created\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }

        NvMediaIDPSetDepth(displayCtx->idpCtx, depth);
    }

    return NVMEDIA_STATUS_OK;
failed:
    LOG_ERR("%s: Failed to initialize Display\n", __func__);
    return status;
}

NvMediaStatus
DisplayFini(NvMainContext *mainCtx)
{
    NvDisplayContext *displayCtx = NULL;
    NvMediaStatus status;
    NvMediaImage *releaseFrames[DISPLAY_QUEUE_SIZE] = { 0 };
    NvMediaImage **releaseList = &releaseFrames[0];
    NvMediaImage *image = NULL;

    if (!mainCtx)
        return NVMEDIA_STATUS_OK;

    displayCtx = mainCtx->ctxs[DISPLAY_ELEMENT];
    if (!displayCtx)
        return NVMEDIA_STATUS_OK;

    /* Wait for display thread to exit */
    while (!displayCtx->exitedFlag) {
        LOG_DBG("%s: Waiting for display thread to quit\n", __func__);
    }

    *displayCtx->quit = NVMEDIA_TRUE;

    /* Destroy thread */
    if (displayCtx->displayThread) {
        status = NvThreadDestroy(displayCtx->displayThread);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to destroy display thread \n", __func__);
        }
    }

    /* Flush the display */
    if (displayCtx->displayEnabled) {
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
                image = *releaseList;
                if (NvQueuePut((NvQueue *)image->tag,
                               (void *)&image,
                               0) != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to put image back in queue\n", __func__);
                    break;
                }
                releaseList++;
            }

            /* Destroy IDP context */
            if (displayCtx->idpCtx)
                NvMediaIDPDestroy(displayCtx->idpCtx);
        }
    }

    /* Flush and destroy the input queue */
    if (displayCtx->inputQueue) {
        LOG_DBG("%s: Flushing the Display input queue\n", __func__);
        while (IsSucceed(NvQueueGet(displayCtx->inputQueue, &image, 0))) {
            if (image) {
                if (NvQueuePut((NvQueue *)image->tag,
                               (void *)&image,
                               0) != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to put image back in queue\n", __func__);
                    break;
                }
                image = NULL;
            }
        }
        NvQueueDestroy(displayCtx->inputQueue);
    }

    image=NULL;
    free(displayCtx);

    LOG_INFO("%s: DisplayFini done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
DisplayProc(NvMainContext *mainCtx)
{
    NvDisplayContext *displayCtx = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if (!mainCtx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }
    displayCtx = mainCtx->ctxs[DISPLAY_ELEMENT];

    /* Create thread to display images */
    if (displayCtx->displayEnabled) {
        displayCtx->exitedFlag = NVMEDIA_FALSE;
        status = NvThreadCreate(&displayCtx->displayThread,
                                &_DisplayThreadFunc,
                                (void *)displayCtx,
                                NV_THREAD_PRIORITY_NORMAL);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to create display Thread\n",
                    __func__);
            displayCtx->exitedFlag = NVMEDIA_TRUE;
        }
    }
    return status;
}
