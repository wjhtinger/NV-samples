/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <limits.h>
#include <math.h>
#include <sys/time.h>
#include <signal.h>

#include "buffer_utils.h"
#include "ipp_yuv.h"
#include "misc_utils.h"
#include "ipp_component.h"

//
// This is the callback function to get the global time
//
static NvMediaStatus
IPPGetAbsoluteGlobalTime(
    void *clientContext,
    NvMediaGlobalTime *timeValue)
{
    struct timeval tv;

    if(!timeValue)
        return NVMEDIA_STATUS_ERROR;

    gettimeofday(&tv, NULL);

    // Convert timeval to 64-bit microseconds
    *timeValue = (NvU64)tv.tv_sec * 1000000 + (NvU64)tv.tv_usec;

    return NVMEDIA_STATUS_OK;
}

// Event callback function
// This function will be called by IPP in case of an event
static void
IPPEventHandler(
    void *clientContext,
    NvMediaIPPComponentType componentType,
    NvMediaIPPComponent *ippComponent,
    NvMediaIPPEventType etype,
    NvMediaIPPEventData *edata)
{
    switch (etype) {
        case NVMEDIA_IPP_EVENT_INFO_FRAME_CAPTURE:
        {
            LOG_INFO("[Frame: %u CaptureTimestamp: %u] - ICP CAPTURE\n",
                edata->imageInformation.frameSequenceNumber,
                edata->imageInformation.frameCaptureGlobalTimeStamp);
            break;
        }
        case NVMEDIA_IPP_EVENT_WARNING_CAPTURE_FRAME_DROP:
        {
            LOG_WARN("[Frame: %u CaptureTimestamp: %u] - ICP DROP\n",
                edata->imageInformation.frameSequenceNumber,
                edata->imageInformation.frameCaptureGlobalTimeStamp);
            break;
        }
        case NVMEDIA_IPP_EVENT_ERROR_NO_RESOURCES:
        {
            LOG_ERR("Out of resource\n");
            break;
        }
        case NVMEDIA_IPP_EVENT_ERROR_INTERNAL_FAILURE:
        {
            LOG_ERR("Internal failure\n");
            break;
        }
        case NVMEDIA_IPP_EVENT_ERROR_BUFFER_PROCESSING_FAILURE:
        {
            LOG_ERR("Buffer processing failure\n");
            break;
        }
        case NVMEDIA_IPP_EVENT_WARNING_CSI_FRAME_DISCONTINUITY:
        {
            LOG_WARN("Frame drop(s) at CSI\n");
            break;
        }
        default:
        {
            break;
        }
    }
}

static void
ISC_ErrorNotificationHandler(void *context)
{
    NvMediaStatus status;
    IPPCtx *ctx = (IPPCtx *)context;

    status = NvQueuePut(ctx->errorHandlerThreadQueue,
                        ctx,
                        ENQUEUE_TIMEOUT);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s Failed to put signal information on queue\n", __func__);
        ctx->quit = NVMEDIA_TRUE;
    }
}

static NvU32
ISC_ErrorHandlerThreadFunc(void *data)
{
    IPPCtx *ctx = (IPPCtx *)data;
    NvMediaStatus status;
    NvU32 queueSize = 0, link = 0;
    siginfo_t info;

    while(!ctx->quit) {

        // Check for queued errors
        status = NvQueueGetSize(ctx->errorHandlerThreadQueue,
                                &queueSize);
        if(!queueSize) {
            usleep(1000);
            continue;
        }

        status = NvQueueGet(ctx->errorHandlerThreadQueue,
                            &info,
                            DEQUEUE_TIMEOUT);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get signal info from queue\n", __func__);
            ctx->quit = NVMEDIA_TRUE;
            goto done;
        }

        /* does not check error type, only gets the failed link */
        status = ExtImgDevGetError(ctx->extImgDevice, &link, NULL);
        if(status != NVMEDIA_STATUS_OK && status != NVMEDIA_STATUS_NOT_SUPPORTED) {
            LOG_ERR("%s: ExtImgDevGetError failed\n", __func__);
            ctx->quit = NVMEDIA_TRUE;
            goto done;
        }
    }
done:
    ctx->errorHandlerThreadExited = NVMEDIA_TRUE;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
IPPInit (
    IPPCtx *ctx,
    TestArgs *testArgs)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    CaptureConfigParams        *captureParams;
    NvU32 setId, i;
    ExtImgDevVersion extImgDevVersion;
    ExtImgDevParam extImgDevParam;

    if (!ctx || !testArgs) {
        LOG_ERR("%s: Bad parameter", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    memset(ctx->ippComponents, 0, sizeof(ctx->ippComponents));
    memset(ctx->componentNum, 0, sizeof(ctx->componentNum));

    ctx->imagesNum = testArgs->imagesNum;
    if (ctx->imagesNum > NVMEDIA_MAX_AGGREGATE_IMAGES) {
        LOG_WARN("Max aggregate images is: %u\n",
                 NVMEDIA_MAX_AGGREGATE_IMAGES);
        ctx->imagesNum = NVMEDIA_MAX_AGGREGATE_IMAGES;
    }

    ctx->quit = NVMEDIA_FALSE;

    ctx->aggregateFlag = testArgs->useAggregationFlag;
    ctx->outputSurfType = testArgs->outputSurfType;
    ctx->outputSurfAdvConfig = testArgs->outputSurfAdvConfig;
    ctx->outputSurfAttributes = testArgs->outputSurfAttributes;
    ctx->ispOutType = testArgs->ispOutType;
    ctx->inputFormatWidthMultiplier = 1;
    ctx->showTimeStamp = testArgs->showTimeStamp;
    ctx->showMetadataFlag = testArgs->showMetadataFlag;
    ctx->displayId = testArgs->displayId;
    ctx->camMap = &testArgs->camMap;
    ctx->errorHandlerThreadExited = NVMEDIA_TRUE;

    setId = testArgs->configCaptureSetUsed;
    captureParams = &testArgs->captureConfigCollection[setId];
    LOG_DBG("%s: setId=%d,input resolution %s\n", __func__,
                setId, captureParams->resolution);
    if (sscanf(captureParams->resolution, "%ux%u",
        &ctx->inputWidth,
        &ctx->inputHeight) != 2) {
        LOG_ERR("%s: Invalid input resolution %s\n", __func__,
                captureParams->resolution);
        goto failed;
    }
    LOG_DBG("%s: inputWidth =%d,ctx->inputHeight =%d\n", __func__,
                ctx->inputWidth, ctx->inputHeight);

    if (ctx->aggregateFlag)
       ctx->inputWidth *= ctx->imagesNum;

    if (ctx->aggregateFlag &&
       (ctx->inputWidth % ctx->imagesNum) != 0) {
        LOG_ERR("%s: Invalid number of siblings (%u) for input width: %u\n",
                __func__, ctx->imagesNum, ctx->inputWidth);
        goto failed;
    }

    ctx->ippNum = ctx->imagesNum;

    for (i=0; i<ctx->ippNum; i++) {
        ctx->outputEnabled[i] = NVMEDIA_TRUE;
    }

    memset(&extImgDevVersion, 0, sizeof(extImgDevVersion));
    EXTIMGDEV_SET_VERSION(extImgDevVersion, EXTIMGDEV_VERSION_MAJOR,
                                            EXTIMGDEV_VERSION_MINOR);
    status = ExtImgDevCheckVersion(&extImgDevVersion);
    if(status != NVMEDIA_STATUS_OK) {
        goto failed;
    }

    memset(&extImgDevParam, 0, sizeof(extImgDevParam));
    extImgDevParam.desAddr = captureParams->desAddr;
    extImgDevParam.brdcstSerAddr = captureParams->brdcstSerAddr;
    extImgDevParam.brdcstSensorAddr = captureParams->brdcstSensorAddr;
    for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
        //FIXME: Hardcoded now, would be read from config file later
        //extImgDevParam.serAddr[i] = captureParams->brdcstSerAddr + i + 1;
        extImgDevParam.sensorAddr[i] = captureParams->brdcstSensorAddr + i + 1;
        //extImgDevParam.serAddr[i] = captureParams->serAddr[i];
        //extImgDevParam.sensorAddr[i] = captureParams->sensorAddr[i];
    }
    extImgDevParam.i2cDevice = captureParams->i2cDevice;
    extImgDevParam.moduleName = captureParams->inputDevice;
    extImgDevParam.board = captureParams->board;
    extImgDevParam.resolution = captureParams->resolution;
    extImgDevParam.sensorsNum = ctx->imagesNum;
    extImgDevParam.inputFormat = captureParams->inputFormat;
    extImgDevParam.interface = captureParams->interface;
    extImgDevParam.enableEmbLines =
        (captureParams->embeddedDataLinesTop || captureParams->embeddedDataLinesBottom) ?
            NVMEDIA_TRUE : NVMEDIA_FALSE;
    extImgDevParam.initialized = NVMEDIA_FALSE;
    extImgDevParam.enableSimulator = NVMEDIA_FALSE;
    extImgDevParam.slave = testArgs->slaveTegra;
    extImgDevParam.camMap = ctx->camMap;
    extImgDevParam.enableExtSync = testArgs->enableExtSync;
    extImgDevParam.dutyRatio =
        ((testArgs->dutyRatio <= 0.0) || (testArgs->dutyRatio >= 1.0)) ?
            0.25 : testArgs->dutyRatio;

    ctx->extImgDevice = ExtImgDevInit(&extImgDevParam);
    if(!ctx->extImgDevice) {
        LOG_ERR("%s: Failed to initialize ISC devices\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    if(IsFailed(IPPSetCaptureSettings(ctx, captureParams))) {
        goto failed;
    }

    ctx->device = NvMediaDeviceCreate();
    if(!ctx->device) {
        LOG_ERR("%s: Failed to create NvMedia device\n", __func__);
        goto failed;
    }

    // Create IPPManager
    ctx->ippManager = NvMediaIPPManagerCreate(NVMEDIA_IPP_VERSION_INFO, ctx->device, ctx->ipaDevice);
    if(!ctx->ippManager) {
        LOG_ERR("%s: Failed to create ippManager\n", __func__);
        goto done;
    }

    status = NvMediaIPPManagerSetTimeSource(ctx->ippManager, NULL, IPPGetAbsoluteGlobalTime);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set time source\n", __func__);
        goto failed;
    }

    status = IPPCreateYUVPipeline(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create YUV Pipeline \n", __func__);
        goto failed;
    }

    // Create error handler queue
    status = NvQueueCreate(&ctx->errorHandlerThreadQueue,
                            MAX_ERROR_QUEUE_SIZE,
                            sizeof(IPPCtx *));
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create queue for error handler thread\n", __func__);
        goto failed;
    }

    // Create error handler thread
    ctx->errorHandlerThreadExited = NVMEDIA_FALSE;
    status = NvThreadCreate(&ctx->errorHandlerThread,
                            &ISC_ErrorHandlerThreadFunc,
                            (void *)ctx,
                            NV_THREAD_PRIORITY_NORMAL);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create error handler thread\n", __func__);
        ctx->errorHandlerThreadExited = NVMEDIA_TRUE;
        goto failed;
    }

    // Register callback function for errors
    status = ExtImgDevRegisterCallback(ctx->extImgDevice, 36, ISC_ErrorNotificationHandler, ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to register ISC callback function\n", __func__);
        goto failed;
    }

        // Set the callback function for event
    status = NvMediaIPPManagerSetEventCallback(ctx->ippManager, ctx, IPPEventHandler);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set event callback\n", __func__);
        return status;
    }

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    IPPFini(ctx);
    return NVMEDIA_STATUS_ERROR;

done:
    return status;
}

NvMediaStatus
IPPStart (
    IPPCtx *ctx)
{
    NvU32 i;

    // Start IPPs
    for (i=0; i<ctx->ippNum; i++) {
        if (IsFailed(NvMediaIPPPipelineStart(ctx->ipp[i]))) {      //ippPipeline
            LOG_ERR("%s: Failed starting pipeline %d\n", __func__, i);
            goto failed;
        }
    }

    // Start ExtImgDevice
    if(ctx->extImgDevice)
        ExtImgDevStart(ctx->extImgDevice);

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    IPPFini(ctx);

    return NVMEDIA_STATUS_ERROR;
}
NvMediaStatus
IPPStop (IPPCtx *ctx)
{

    NvU32 i;

    for(i = 0; i < ctx->ippNum; i++) {
        if (IsFailed(NvMediaIPPPipelineStop(ctx->ipp[i]))) {
            LOG_ERR("%s: Failed stop pipeline %d\n", __func__, i);
            return NVMEDIA_STATUS_ERROR;
        }
    }
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
IPPFini (IPPCtx *ctx)
{
    NvMediaStatus status;
    ctx->quit = NVMEDIA_TRUE;

    //Wait for error thread to exit
    while(!ctx->errorHandlerThreadExited) {
        LOG_DBG("%s: Waiting for error handler thread to quit\n", __func__);
        usleep(1000);
    }

    NvMediaIPPManagerDestroy(ctx->ippManager);

    if(ctx->extImgDevice) {
        ExtImgDevDeinit(ctx->extImgDevice);
    }

    if(ctx->device)
        NvMediaDeviceDestroy(ctx->device);

     // Destroy error thread queue
    if(ctx->errorHandlerThreadQueue)
        NvQueueDestroy(ctx->errorHandlerThreadQueue);

    // Destroy error thread
    if(ctx->errorHandlerThread) {
        status = NvThreadDestroy(ctx->errorHandlerThread);
        if(status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy error handler thread\n", __func__);
    }

    free(ctx);
    return NVMEDIA_STATUS_OK;
}
