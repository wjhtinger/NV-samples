/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "grp_activate.h"
#include "capture.h"

static NvU32
_GrpActivationThreadFunc(void *data)
{
    NvGrpActivationContext *ctx = (NvGrpActivationContext *)data;
    I2cHandle handle = NULL;
    NvU32 currentGroup = 0;
    NvU32 triggerFrame = 0;
    NvU32 cmdIdx = 0;
    Command *command = NULL;
    NvMediaStatus status;

    if (!ctx->allGroups.numGroups)
        goto done;

    testutil_i2c_open(ctx->i2cDeviceNum, &handle);
    if (!handle) {
        LOG_ERR("%s: Failed to open handle with id %u\n", __func__,
                ctx->i2cDeviceNum);
        *ctx->quit = NVMEDIA_TRUE;
        goto done;
    }

    cmdIdx = ctx->allGroups.groups[currentGroup].firstCommand;
    command = &ctx->parsedCommands->commands[cmdIdx];
    triggerFrame = command->triggerFrame;

    while (!(*ctx->quit)) {
        if (*ctx->currentFrame >= triggerFrame) {
            // Write group registers
            status = I2cProcessGroup(handle,
                                     ctx->parsedCommands,
                                     &ctx->allGroups.groups[currentGroup]);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to write registers for frame %u\n",
                        __func__, triggerFrame);
                *ctx->quit = NVMEDIA_TRUE;
                goto done;
            }

            if (++currentGroup == ctx->allGroups.numGroups) {
                goto done;
            } else {
                cmdIdx = ctx->allGroups.groups[currentGroup].firstCommand;
                command = &ctx->parsedCommands->commands[cmdIdx];
                triggerFrame = command->triggerFrame;
            }
        }
        usleep(100);
    }

done:
    if (handle)
        testutil_i2c_close(handle);
    LOG_INFO("%s: Group Activation thread exited\n", __func__);
    ctx->exitedFlag = NVMEDIA_TRUE;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
GrpActivationInit(NvMainContext *mainCtx)
{
    NvGrpActivationContext *grpActCtx  = NULL;
    NvCaptureContext   *captureCtx = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

   /* allocating group activation context */
    mainCtx->ctxs[GRP_ACTIVATION_ELEMENT]= malloc(sizeof(NvGrpActivationContext));
    if (!mainCtx->ctxs[GRP_ACTIVATION_ELEMENT]){
        LOG_ERR("%s: Failed to allocate memory for group activation context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    grpActCtx = mainCtx->ctxs[GRP_ACTIVATION_ELEMENT];
    memset(grpActCtx,0,sizeof(NvGrpActivationContext));
    captureCtx = mainCtx->ctxs[CAPTURE_ELEMENT];

    /* initialize context */
    grpActCtx->quit = &mainCtx->quit;
    grpActCtx->exitedFlag = NVMEDIA_TRUE;
    grpActCtx->currentFrame =  &captureCtx->threadCtx[0].currentFrame;
    grpActCtx->i2cDeviceNum = captureCtx->i2cDeviceNum;
    grpActCtx->parsedCommands = &captureCtx->parsedCommands;

    /* Setup group activation groups */
    if (mainCtx->testArgs->wrregs.isUsed) {
        status = I2cSetupGroups(grpActCtx->parsedCommands,
                                &grpActCtx->allGroups);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to setup groups\n",__func__);
            goto failed;
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return status;
}

NvMediaStatus
GrpActivationFini(NvMainContext *mainCtx)
{
    NvGrpActivationContext *grpActCtx = NULL;
    NvMediaStatus status;

    if (!mainCtx)
        return NVMEDIA_STATUS_OK;

    grpActCtx = mainCtx->ctxs[GRP_ACTIVATION_ELEMENT];
    if (!grpActCtx)
        return NVMEDIA_STATUS_OK;

    /* wait for thread to exit */
    while (!grpActCtx->exitedFlag) {
        LOG_DBG("%s: Waiting for group activation thread to quit\n",
                __func__);
    }

    /* Destroy the thread */
    if (grpActCtx->grpActThread) {
        status = NvThreadDestroy(grpActCtx->grpActThread);
        if (status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy group activation thread\n",
                    __func__);
    }

    free(grpActCtx);

    LOG_INFO("%s: GrpActivationFini done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
GrpActivationProc(NvMainContext *mainCtx)
{
    NvGrpActivationContext *grpActCtx = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    grpActCtx = mainCtx->ctxs[GRP_ACTIVATION_ELEMENT];

    /* Create group activation thread only if groups are present */
    if (grpActCtx->allGroups.numGroups) {
        grpActCtx->exitedFlag = NVMEDIA_FALSE;
        status = NvThreadCreate(&grpActCtx->grpActThread,
                                &_GrpActivationThreadFunc,
                                (void *)grpActCtx,
                                NV_THREAD_PRIORITY_NORMAL);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to create group activation thread\n",
                    __func__);
            grpActCtx->exitedFlag = NVMEDIA_TRUE;
        }
    }

    return status;
}
