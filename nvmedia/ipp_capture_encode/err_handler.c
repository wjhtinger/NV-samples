/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <unistd.h>
#include "signal.h"
#include "err_handler.h"
#include "capture.h"
#include "img_dev.h"
#include "log_utils.h"
#include "staging.h"

#define MAX_ERROR_QUEUE_SIZE            20
#define QUEUE_ENQUEUE_TIMEOUT           100

static void
_ErrorNotificationHandler(void *context)
{
    NvMediaStatus status;
    NvErrHandlerContext *ctx = (NvErrHandlerContext *)context;

    status = NvQueuePut(ctx->errorQueue,
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
    NvErrHandlerContext *pTempCtx;
    ExtImgDevFailureType errorType;

    while (!(*ctx->quit)) {
        // Check for outstanding errors
        status = NvQueueGetSize(ctx->errorQueue,
                                &queueSize);
        if (!queueSize) {
            usleep(1000);
            continue;
        }

        pTempCtx = NULL;
        status = NvQueueGet(ctx->errorQueue,
                            (void *)(&pTempCtx),
                            0);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get signal info from queue\n", __func__);
            *ctx->quit = NVMEDIA_TRUE;
            goto done;
        }

        errorType = EXT_IMG_DEV_NO_ERROR;
        status = ExtImgDevGetError(ctx->extImgDevice, &link, &errorType);
        if (status != NVMEDIA_STATUS_OK && status != NVMEDIA_STATUS_NOT_SUPPORTED) {
            LOG_ERR("%s: ISC error\n", __func__);
            // if we have some meaningful error code, log it
            if (EXT_IMG_DEV_NO_ERROR != errorType) {
                LOG_ERR("%s: ExtImgDevError = %d\n",
                        __func__,
                        errorType);
            }
            *ctx->quit = NVMEDIA_TRUE;
            goto done;
        }
    }
done:
    LOG_INFO("%s: Error Handler thread exited\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ErrHandlerInit(NvMainContext *mainCtx)
{
    NvErrHandlerContext *errHandlerCtx  = NULL;
    StreamCtx           *streamCtx = NULL;
    NvMediaStatus       status;

   /* allocating error handler context */
    mainCtx->ctxs[ERR_HANDLER_ELEMENT]= malloc(sizeof(NvErrHandlerContext));
    if (!mainCtx->ctxs[ERR_HANDLER_ELEMENT]){
        LOG_ERR("%s: Failed to allocate memory for error handler context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    errHandlerCtx = mainCtx->ctxs[ERR_HANDLER_ELEMENT];
    memset(errHandlerCtx,0,sizeof(NvErrHandlerContext));
    streamCtx = mainCtx->ctxs[STREAMING_ELEMENT];

    /* initialize context */
    errHandlerCtx->quit = &mainCtx->quit;
    errHandlerCtx->extImgDevice = streamCtx->captureSS.extImgDevice;

    /* Create error handler queue */
    status = NvQueueCreate(&errHandlerCtx->errorQueue,
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

void
ErrHandlerStop(NvMainContext *mainCtx)
{
    NvErrHandlerContext *errHandlerCtx = NULL;
    NvMediaStatus status;

    if (!mainCtx)
        return;

    errHandlerCtx = mainCtx->ctxs[ERR_HANDLER_ELEMENT];
    if (!errHandlerCtx)
        return;

    *(errHandlerCtx->quit) = NVMEDIA_TRUE;

    /* Destroy the thread */
    if (errHandlerCtx->errHandlerThread) {
        status = NvThreadDestroy(errHandlerCtx->errHandlerThread);
        if (status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy error handler thread\n",
                    __func__);
    }

    errHandlerCtx->errHandlerThread = NULL;

    return;
}

NvMediaStatus
ErrHandlerFini(NvMainContext *mainCtx)
{
    NvErrHandlerContext *errHandlerCtx = NULL;

    if (!mainCtx)
        return NVMEDIA_STATUS_OK;

    errHandlerCtx = mainCtx->ctxs[ERR_HANDLER_ELEMENT];
    if (!errHandlerCtx)
        return NVMEDIA_STATUS_OK;

    if (NULL != errHandlerCtx->errHandlerThread) {
        LOG_ERR("%s: Error Handler threads active, invoke ErrHandlerStop first\n",
                __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    /* Destroy error handler queue */
    if (errHandlerCtx->errorQueue) {
        NvQueueDestroy(errHandlerCtx->errorQueue);
    }

    free(errHandlerCtx);

    LOG_INFO("%s: done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ErrHandlerStart(NvMainContext *mainCtx)
{
    NvErrHandlerContext *errHandlerCtx = NULL;
    NvMediaStatus status;
    errHandlerCtx = mainCtx->ctxs[ERR_HANDLER_ELEMENT];

    /* Create error handler thread */
    status = NvThreadCreate(&errHandlerCtx->errHandlerThread,
                            &_ErrHandlerThreadFunc,
                            (void *)errHandlerCtx,
                            NV_THREAD_PRIORITY_NORMAL);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create error handler thread\n",
                __func__);
    }
    return status;
}
