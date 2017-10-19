/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "signal.h"
#include "err_handler.h"
#include "capture.h"
#include "img_dev.h"

static void
_ErrorNotificationHandler(void *context)
{
    NvMediaStatus status;
    NvErrHandlerContext *ctx = (NvErrHandlerContext *)context;

    status = NvQueuePut(ctx->threadQueue,
                        ctx,
                        QUEUE_ENQUEUE_TIMEOUT);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s Failed to put signal information on queue\n", __func__);
        *ctx->quit = NVMEDIA_TRUE;
    }
}

static NvU32
_ErrHandlerThreadFunc(void *data)
{
    NvErrHandlerContext *ctx = (NvErrHandlerContext *)data;
    NvMediaStatus status;
    NvU32 queueSize = 0, link = 0;
    siginfo_t info;

    while (!(*ctx->quit)) {
        // Check for outstanding errors
        status = NvQueueGetSize(ctx->threadQueue,
                                &queueSize);
        if (!queueSize) {
            usleep(1000);
            continue;
        }

        status = NvQueueGet(ctx->threadQueue,
                            &info,
                            QUEUE_DEQUEUE_TIMEOUT);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get signal info from queue\n", __func__);
            *ctx->quit = NVMEDIA_TRUE;
            goto done;
        }

        /* not check error type, only get the failed link */
        status = ExtImgDevGetError(ctx->extImgDevice, &link, NULL);
        if (status != NVMEDIA_STATUS_OK && status != NVMEDIA_STATUS_NOT_SUPPORTED) {
            LOG_ERR("%s: ConfigISCGetErrorStatus failed\n", __func__);
            *ctx->quit = NVMEDIA_TRUE;
            goto done;
        }
    }
done:
    LOG_INFO("%s: Error Handler thread exited\n", __func__);
    ctx->exitedFlag = NVMEDIA_TRUE;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ErrHandlerInit(NvMainContext *mainCtx)
{
    NvErrHandlerContext *errHandlerCtx  = NULL;
    NvCaptureContext *captureCtx = NULL;
    NvMediaStatus status;

   /* allocating error handler context */
    mainCtx->ctxs[ERR_HANDLER_ELEMENT]= malloc(sizeof(NvErrHandlerContext));
    if (!mainCtx->ctxs[ERR_HANDLER_ELEMENT]){
        LOG_ERR("%s: Failed to allocate memory for error handler context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    errHandlerCtx = mainCtx->ctxs[ERR_HANDLER_ELEMENT];
    memset(errHandlerCtx,0,sizeof(NvErrHandlerContext));
    captureCtx = mainCtx->ctxs[CAPTURE_ELEMENT];

    /* initialize context */
    errHandlerCtx->quit = &mainCtx->quit;
    errHandlerCtx->extImgDevice = captureCtx->extImgDevice;
    errHandlerCtx->exitedFlag = NVMEDIA_TRUE;

    /* Create error handler queue */
    status = NvQueueCreate(&errHandlerCtx->threadQueue,
                           MAX_ERROR_QUEUE_SIZE,
                           sizeof(NvErrHandlerContext *));
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create queue for error handler thread\n",
                __func__);
        goto failed;
    }

    /* Register callback function for errors */
    status = ExtImgDevRegisterCallback(errHandlerCtx->extImgDevice,
                                       36,
                                       _ErrorNotificationHandler,
                                       errHandlerCtx);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to register ISC callback function\n", __func__);
        goto failed;
    }

    return NVMEDIA_STATUS_OK;
failed:
    return status;
}

NvMediaStatus
ErrHandlerFini(NvMainContext *mainCtx)
{
    NvErrHandlerContext *errHandlerCtx = NULL;
    NvMediaStatus status;

    if (!mainCtx)
        return NVMEDIA_STATUS_OK;

    errHandlerCtx = mainCtx->ctxs[ERR_HANDLER_ELEMENT];
    if (!errHandlerCtx)
        return NVMEDIA_STATUS_OK;

    /* wait for thread to exit */
    while (!errHandlerCtx->exitedFlag) {
        LOG_DBG("%s: Waiting for error handler thread to quit\n",
                __func__);
    }

    /* Destroy the thread */
    if (errHandlerCtx->errHandlerThread) {
        status = NvThreadDestroy(errHandlerCtx->errHandlerThread);
        if (status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy error handler thread\n",
                    __func__);
    }

    /* Destroy error handler queue */
    if (errHandlerCtx->threadQueue) {
        NvQueueDestroy(errHandlerCtx->threadQueue);
    }

    free(errHandlerCtx);

    LOG_INFO("%s: ErrHandlerFini done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ErrHandlerProc(NvMainContext *mainCtx)
{
    NvErrHandlerContext *errHandlerCtx = NULL;
    NvMediaStatus status;
    errHandlerCtx = mainCtx->ctxs[ERR_HANDLER_ELEMENT];

    /* Create error handler thread */
    errHandlerCtx->exitedFlag = NVMEDIA_FALSE;
    status = NvThreadCreate(&errHandlerCtx->errHandlerThread,
                            &_ErrHandlerThreadFunc,
                            (void *)errHandlerCtx,
                            NV_THREAD_PRIORITY_NORMAL);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create error handler thread\n",
                __func__);
        errHandlerCtx->exitedFlag = NVMEDIA_TRUE;
    }
    return status;
}
