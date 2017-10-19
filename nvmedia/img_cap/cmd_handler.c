/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "cmd_handler.h"

static NvU32
_CmdHandlerThreadFunc(void *data)
{
    NvCmdHandlerContext *ctx = (NvCmdHandlerContext *)data;
    NvMediaStatus status;
    RtCommand command;
    NvU32 queueSize = 0;

    while (!(*ctx->quit)) {
        /* Check for outstanding commands */
        status = NvQueueGetSize(ctx->threadQueue,
                                &queueSize);
        if (!queueSize) {
            usleep(1000);
            continue;
        }

        status = NvQueueGet(ctx->threadQueue,
                            &command,
                            QUEUE_DEQUEUE_TIMEOUT);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get command from queue\n", __func__);
            *ctx->quit = NVMEDIA_TRUE;
            goto done;
        }

        switch (command.type) {
            case(RT_CAMERA_RESET):
                break;
            case(RT_CAMERA_OFF):
                break;
            case(RT_CAMERA_ON):
                break;
            case(RT_CAMERA_TEMP):
                break;
            default:
                LOG_ERR("%s: Unknown runtime command type encountered\n",
                        __func__);
                goto done;
        }
    }
done:
    LOG_INFO("%s: Command Handler thread exited\n", __func__);
    ctx->exitedFlag = NVMEDIA_TRUE;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CmdHandlerInit(NvMainContext *mainCtx)
{
    NvCmdHandlerContext *cmdHandlerCtx  = NULL;
    NvMediaStatus status;

   /* allocating command handler context */
    mainCtx->ctxs[CMD_HANDLER_ELEMENT]= malloc(sizeof(NvCmdHandlerContext));
    if (!mainCtx->ctxs[CMD_HANDLER_ELEMENT]){
        LOG_ERR("%s: Failed to allocate memory for command handler context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    cmdHandlerCtx = mainCtx->ctxs[CMD_HANDLER_ELEMENT];
    memset(cmdHandlerCtx,0,sizeof(NvCmdHandlerContext));

    /* initialize context */
    cmdHandlerCtx->quit = &mainCtx->quit;
    cmdHandlerCtx->exitedFlag = NVMEDIA_TRUE;

    /* Create command handler queue */
    status = NvQueueCreate(&cmdHandlerCtx->threadQueue,
                           MAX_COMMAND_QUEUE_SIZE,
                           sizeof(RtCommand));
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create queue for command handler thread\n",
                __func__);
        goto failed;
    }

    return NVMEDIA_STATUS_OK;
failed:
    return status;
}

NvMediaStatus
CmdHandlerFini(NvMainContext *mainCtx)
{
    NvCmdHandlerContext *cmdHandlerCtx = NULL;
    NvMediaStatus status;

    if (!mainCtx)
        return NVMEDIA_STATUS_OK;

    cmdHandlerCtx = mainCtx->ctxs[CMD_HANDLER_ELEMENT];
    if (!cmdHandlerCtx)
        return NVMEDIA_STATUS_OK;

    /* wait for thread to exit */
    while (!cmdHandlerCtx->exitedFlag) {
        LOG_DBG("%s: Waiting for command handler thread to quit\n",
                __func__);
    }

    /* Destroy the thread */
    if (cmdHandlerCtx->cmdHandlerThread) {
        status = NvThreadDestroy(cmdHandlerCtx->cmdHandlerThread);
        if (status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy command handler thread\n",
                    __func__);
    }

    /* Destroy command handler queue */
    if (cmdHandlerCtx->threadQueue) {
        NvQueueDestroy(cmdHandlerCtx->threadQueue);
    }

    free(cmdHandlerCtx);

    LOG_INFO("%s: CmdHandlerFini done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CmdHandlerProc(NvMainContext *mainCtx)
{
    NvCmdHandlerContext *cmdHandlerCtx = NULL;
    NvMediaStatus status;
    cmdHandlerCtx = mainCtx->ctxs[CMD_HANDLER_ELEMENT];

   /* Create command handler thread */
    cmdHandlerCtx->exitedFlag = NVMEDIA_FALSE;
    status = NvThreadCreate(&cmdHandlerCtx->cmdHandlerThread,
                            &_CmdHandlerThreadFunc,
                            (void *)cmdHandlerCtx,
                            NV_THREAD_PRIORITY_NORMAL);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create command handler thread\n",
                __func__);
        cmdHandlerCtx->exitedFlag = NVMEDIA_TRUE;
    }
    return status;
}

NvMediaStatus
CmdHandlerProcessRuntimeCommand(NvMainContext *mainCtx,
                                char *cmd)
{
    NvCmdHandlerContext *cmdHandlerCtx = NULL;
    cmdHandlerCtx = mainCtx->ctxs[CMD_HANDLER_ELEMENT];
    NvMediaStatus status;
    RtCommand command;
    NvU32 cameraId;

    if (sscanf(cmd, "reset: %u", &cameraId) == 1) {
        command.type = RT_CAMERA_RESET;
        command.cameraId = cameraId;
    } else if (sscanf(cmd, "power off: %u", &cameraId) == 1) {
        command.type = RT_CAMERA_OFF;
        command.cameraId = cameraId;
    } else if (sscanf(cmd, "power on: %u", &cameraId) == 1) {
        command.type = RT_CAMERA_ON;
        command.cameraId = cameraId;
    } else if (sscanf(cmd, "temperature: %u", &cameraId) == 1) {
        command.type = RT_CAMERA_TEMP;
        command.cameraId = cameraId;
    } else {
        LOG_ERR("%s: Invalid run time command %s\n", __func__, cmd);
        return NVMEDIA_STATUS_ERROR;
    }

    status = NvQueuePut(cmdHandlerCtx->threadQueue,
                        (void *)&command,
                        QUEUE_ENQUEUE_TIMEOUT);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enque command to command queue\n", __func__);
        return status;
    }

    return NVMEDIA_STATUS_OK;
}
