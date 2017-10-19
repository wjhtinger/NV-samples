/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "capture_status.h"
#include "capture.h"

static NvU32
_CaptureStatusThreadFunc(void *data)
{
    NvCaptureStatusContext *ctx = (NvCaptureStatusContext *)data;
    char status_log[4] = {'|', '/', '-', '\\'};

    printf("Capturing Frames...\n");
    while (!(*ctx->quit)) {
        printf("%c\r", status_log[
            (*ctx->currentFrame) % 4]);
        usleep(500);
    }

    ctx->exitedFlag = NVMEDIA_TRUE;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CaptureStatusInit(NvMainContext *mainCtx)
{
    NvCaptureStatusContext *capStatusCtx  = NULL;
    NvCaptureContext   *captureCtx = NULL;

   /* allocating capture status context */
    mainCtx->ctxs[CAPTURE_STATUS_ELEMENT]= malloc(sizeof(NvCaptureStatusContext));
    if (!mainCtx->ctxs[CAPTURE_STATUS_ELEMENT]){
        LOG_ERR("%s: Failed to allocate memory for capture status context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    capStatusCtx = mainCtx->ctxs[CAPTURE_STATUS_ELEMENT];
    memset(capStatusCtx,0,sizeof(NvCaptureStatusContext));
    captureCtx = mainCtx->ctxs[CAPTURE_ELEMENT];

    /* initialize context */
    capStatusCtx->quit = &mainCtx->quit;
    capStatusCtx->exitedFlag = NVMEDIA_TRUE;
    capStatusCtx->currentFrame =  &captureCtx->threadCtx[0].currentFrame;

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CaptureStatusFini(NvMainContext *mainCtx)
{
    NvCaptureStatusContext *capStatusCtx = NULL;
    NvMediaStatus status;

    if (!mainCtx)
        return NVMEDIA_STATUS_OK;

    capStatusCtx = mainCtx->ctxs[CAPTURE_STATUS_ELEMENT];
    if (!capStatusCtx)
        return NVMEDIA_STATUS_OK;

    /* wait for thread to exit */
    while (!capStatusCtx->exitedFlag) {
        LOG_DBG("%s: Waiting for capture status thread to quit\n",
                __func__);
    }

    /* Destroy the thread */
    if (capStatusCtx->capStatusThread) {
        status = NvThreadDestroy(capStatusCtx->capStatusThread);
        if (status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy capture status thread\n",
                    __func__);
    }

    free(capStatusCtx);

    LOG_INFO("%s: CaptureStatusFini done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CaptureStatusProc(NvMainContext *mainCtx)
{
    NvCaptureStatusContext *capStatusCtx = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    capStatusCtx = mainCtx->ctxs[CAPTURE_STATUS_ELEMENT];

    /* Create capture status thread */
    capStatusCtx->exitedFlag = NVMEDIA_FALSE;
    status = NvThreadCreate(&capStatusCtx->capStatusThread,
                            &_CaptureStatusThreadFunc,
                            (void *)capStatusCtx,
                            NV_THREAD_PRIORITY_NORMAL);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create capture status thread\n",
                __func__);
        capStatusCtx->exitedFlag = NVMEDIA_TRUE;
    }
    return status;
}
