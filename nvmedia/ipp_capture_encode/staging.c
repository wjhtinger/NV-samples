/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
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

#include "staging.h"
#include "misc_utils.h"
#include "nvmedia_acp.h"
#include "sample_plugin.h"

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
    StreamCtx   *ctx;
    NvU32       cameraId;

    if (NULL == clientContext) {
        LOG_ERR("%s: invalid context\n",
                __func__);

        return;
    }

    ctx = (StreamCtx *)clientContext;

    switch (etype) {
        case NVMEDIA_IPP_EVENT_INFO_PROCESSING_DONE:
        {
            switch (componentType) {
                case NVMEDIA_IPP_COMPONENT_CONTROL_ALGORITHM:
                    if (edata) {
                        LOG_INFO("[CameraId: %u Frame: %u] - CA DONE\n",
                            edata->imageInformation.cameraId,
                            edata->imageInformation.frameSequenceNumber);
                    }
                    break;
                case NVMEDIA_IPP_COMPONENT_SENSOR_CONTROL:
                    if (edata) {
                        LOG_INFO("[CameraId: %u Frame: %u] - ISC DONE\n",
                            edata->imageInformation.cameraId,
                            edata->imageInformation.frameSequenceNumber);
                    }
                    break;
                case NVMEDIA_IPP_COMPONENT_ISP:
                    if (edata) {
                        LOG_INFO("[CameraId: %u Frame: %u] - ISP DONE\n",
                            edata->imageInformation.cameraId,
                            edata->imageInformation.frameSequenceNumber);
                    }
                    break;
                default:
                    break;
            }
            break;
        }
        case NVMEDIA_IPP_EVENT_INFO_FRAME_CAPTURE:
        {
            cameraId = edata->imageInformation.cameraId;
            if (cameraId >= MAX_NUM_OF_PIPES) {
                LOG_ERR("%s: Bad camera ID %d\n",
                        __func__,
                        cameraId);
            } else {
                ctx->totalFrameCount[cameraId] += 1;

                LOG_INFO("[CameraId: %u Frame: %u CaptureTimestamp: %u] - ICP CAPTURE\n",
                        cameraId,
                         edata->imageInformation.frameSequenceNumber,
                         edata->imageInformation.frameCaptureGlobalTimeStamp);
            }
            break;
        }
        case NVMEDIA_IPP_EVENT_WARNING_CAPTURE_FRAME_DROP:
        {
            // ignore the error if a quit is pending
            if (NVMEDIA_TRUE == *(ctx->pQuit)) {
                break;
            }

            cameraId = edata->imageInformation.cameraId;
            if (cameraId >= MAX_NUM_OF_PIPES) {
                LOG_ERR("%s: Bad camera ID %d\n",
                        __func__,
                        cameraId);
            } else {
                ctx->totalFrameDrops[cameraId] += 1;

                LOG_WARN("[CameraId: %u Frame: %u CaptureTimestamp: %u] - ICP DROP\n",
                          cameraId,
                          edata->imageInformation.frameSequenceNumber,
                          edata->imageInformation.frameCaptureGlobalTimeStamp);
            }
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

static NvMediaStatus
SetCaptureSettings (
    StreamCtx           *ctx,
    CaptureConfigParams *config,
    TestArgs            *testArgs)
{
    CaptureParams       *pCaptureParams;

    ExtImgDevProperty *extImgDevProperty = &(ctx->captureSS.extImgDevice->property);
    pCaptureParams = &(ctx->captureSS.captureParams);

    pCaptureParams->interfaceLanesCount = config->csiLanes;
    pCaptureParams->streamCount = ctx->imagesNum;

    if(!strcasecmp(config->interface, "csi-a"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_A;
    else if(!strcasecmp(config->interface, "csi-b"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_B;
    else if(!strcasecmp(config->interface, "csi-ab"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB;
    else if(!strcasecmp(config->interface, "csi-c"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_C;
    else if(!strcasecmp(config->interface, "csi-d"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_D;
    else if(!strcasecmp(config->interface, "csi-cd"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_CD;
    else if(!strcasecmp(config->interface, "csi-e"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_E;
    else if(!strcasecmp(config->interface, "csi-f"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_F;
    else if(!strcasecmp(config->interface, "csi-ef"))
        pCaptureParams->inputInterfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_EF;
    else {
        LOG_ERR("%s: Bad interface-type specified: %s\n",
                __func__,
                config->interface);
        return NVMEDIA_STATUS_ERROR;
    }

    if(!strcasecmp(config->inputFormat, "raw12")) {
        pCaptureParams->inputFormatType     = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_RAW;
        pCaptureParams->inputBitsPerPixel   = NVMEDIA_BITS_PER_PIXEL_12;
        pCaptureParams->inputPixelOrder     = extImgDevProperty->pixelOrder;
    } else {
        LOG_ERR("%s: Bad input format specified: %s\n",
                __func__, config->inputFormat);
        return NVMEDIA_STATUS_ERROR;
    }

    if (!strcasecmp(config->surfaceFormat, "raw12")) {
        pCaptureParams->outputSurfaceFormatType = NVMEDIA_IMAGE_CAPTURE_SURFACE_FORMAT_TYPE_RAW;
        pCaptureParams->outputBitsPerPixel      = NVMEDIA_BITS_PER_PIXEL_12;
        pCaptureParams->outputPixelOrder        = extImgDevProperty->pixelOrder;

    } else {
        LOG_ERR("Bad CSI capture surface format: %s\n",
                 config->surfaceFormat);
        return NVMEDIA_STATUS_ERROR;
    }

    LOG_DBG("%s: Setting capture parameters\n", __func__);
    if(sscanf(config->resolution,
              "%ux%u",
              &pCaptureParams->width,
              &pCaptureParams->height) != 2) {
        LOG_ERR("%s: Bad resolution: %s\n", __func__, config->resolution);
        return NVMEDIA_STATUS_ERROR;
    }

    pCaptureParams->startX = 0;
    pCaptureParams->startY = 0;
    pCaptureParams->embeddedDataLinesTop = config->embeddedDataLinesTop;
    pCaptureParams->embeddedDataLinesBottom = config->embeddedDataLinesBottom;
    pCaptureParams->interfaceLanesCount = config->csiLanes;
    pCaptureParams->useVirtualChannels = testArgs->useVirtualChannels;

    LOG_DBG("%s: inputWidth =%d, inputHeight =%d, num_csi_lanes =%d\n",
            __func__,
            pCaptureParams->width,
            pCaptureParams->height,
            pCaptureParams->interfaceLanesCount);

    LOG_DBG("%s: Embedded data lines top: %u\nEmbedded data lines bottom: %u\n",
            __func__,
            pCaptureParams->embeddedDataLinesTop,
            pCaptureParams->embeddedDataLinesBottom);

    // TODO: support extraction of output from capture module directly
    // this will be used for cases where we are streaming from YUV cameras
    // directly and want to encode them
    ctx->captureSS.captureParams.enableOuptut = NVMEDIA_FALSE;

    return NVMEDIA_STATUS_OK;
}


NvMediaStatus
StagingInit (NvMainContext *mainCtx)
{
    CaptureConfigParams        *captureParams;
    NvU32 setId, i;
    ExtImgDevParam          extImgDevParam;
    TestArgs                *testArgs;
    StreamCtx               *ctx;
    NvMediaIPPPluginFuncs   *pPluginFuncs;
    NvU32                   count;

    NvMediaIPPPluginFuncs nvpluginFuncs = {
        .createFunc = &NvMediaACPCreate,
        .destroyFunc = &NvMediaACPDestroy,
        .parseConfigurationFunc = &NvMediaACPParseConfiguration,
        .processFunc = &NvMediaACPProcess
    };

    NvMediaIPPPluginFuncs samplepluginFuncs = {
        .createFunc = &NvSampleACPCreate,
        .destroyFunc = &NvSampleACPDestroy,
        .parseConfigurationFunc = &NvSampleACPParseConfiguration,
        .processFunc = &NvSampleACPProcess
    };

    if (NULL == mainCtx) {
        LOG_ERR("%s: Bad parameter", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    ctx = calloc(1, sizeof(StreamCtx));
    if (NULL == ctx){
        LOG_ERR("%s: Failed to allocate memory for error handler context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    mainCtx->ctxs[STREAMING_ELEMENT]= ctx;
    testArgs = mainCtx->testArgs;

    ctx->imagesNum = testArgs->imagesNum;
    if (ctx->imagesNum > NVMEDIA_MAX_AGGREGATE_IMAGES) {
        LOG_WARN("Max aggregate images is: %u\n",
                 NVMEDIA_MAX_AGGREGATE_IMAGES);
        ctx->imagesNum = NVMEDIA_MAX_AGGREGATE_IMAGES;
    }

    ctx->captureSS.streamCount = ctx->imagesNum;

    for (count = 0; count < ctx->captureSS.streamCount; count++) {
        if (NVMEDIA_TRUE == testArgs->enableEncode) {
            ctx->captureSS.enablePipeOutputs[count].enableProcessing = NVMEDIA_TRUE;
            ctx->captureSS.enablePipeOutputs[count].numOutputs = 1;
        }
    }

    // the first pipe can generate 2 outputs, one for display and
    // another one for encoder, the rest will generate one output
    if (NVMEDIA_TRUE == testArgs->displayEnabled) {
        ctx->captureSS.enablePipeOutputs[0].enableProcessing = NVMEDIA_TRUE;
        ctx->captureSS.enablePipeOutputs[0].numOutputs += 1;
    }

    ctx->pluginFlag = testArgs->pluginFlag;
    ctx->captureSS.camMap = &testArgs->camMap;

    setId = testArgs->configCaptureSetUsed;
    captureParams = &testArgs->captureConfigCollection[setId];
    LOG_DBG("%s: setId=%d,input resolution %s\n", __func__,
                setId, captureParams->resolution);

    memset(&extImgDevParam, 0, sizeof(extImgDevParam));
    extImgDevParam.desAddr = captureParams->desAddr;
    extImgDevParam.brdcstSerAddr = captureParams->brdcstSerAddr;
    extImgDevParam.brdcstSensorAddr = captureParams->brdcstSensorAddr;
    for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
        extImgDevParam.sensorAddr[i] = captureParams->brdcstSensorAddr + i + 1;
    }
    extImgDevParam.i2cDevice = captureParams->i2cDevice;
    extImgDevParam.moduleName = captureParams->inputDevice;
    extImgDevParam.board = captureParams->board;
    extImgDevParam.resolution = captureParams->resolution;
    extImgDevParam.sensorsNum = ctx->imagesNum;
    extImgDevParam.inputFormat = captureParams->inputFormat;
    extImgDevParam.interface = captureParams->interface;
    extImgDevParam.camMap = ctx->captureSS.camMap;
    extImgDevParam.enableEmbLines =
        (captureParams->embeddedDataLinesTop || captureParams->embeddedDataLinesBottom) ?
            NVMEDIA_TRUE : NVMEDIA_FALSE;
    extImgDevParam.initialized = NVMEDIA_FALSE;
    extImgDevParam.enableSimulator = NVMEDIA_FALSE;
    extImgDevParam.slave = testArgs->slaveTegra;
    extImgDevParam.enableVirtualChannels = testArgs->useVirtualChannels;
    extImgDevParam.enableExtSync = testArgs->enableExtSync;
    extImgDevParam.dutyRatio =
        ((testArgs->dutyRatio <= 0.0) || (testArgs->dutyRatio >= 1.0)) ?
            0.25 : testArgs->dutyRatio;

    ctx->captureSS.extImgDevice = ExtImgDevInit(&extImgDevParam);
    if(!ctx->captureSS.extImgDevice) {
        LOG_ERR("%s: Failed to initialize ISC devices\n", __func__);
        goto failed;
    }

    if(IsFailed(SetCaptureSettings(ctx, captureParams, testArgs))) {
        goto failed;
    }

    LOG_DBG("%s: creating capture encode instance\n", __func__);

    if (NVMEDIA_NOACPLUGIN == ctx->pluginFlag) {
        LOG_DBG("%s: using no plugin for algorithm control\n", __func__);
        pPluginFuncs = NULL;
    }
    else if (NVMEDIA_SIMPLEACPLUGIN == ctx->pluginFlag) {
        LOG_DBG("%s: using sample plugin for algorithm control\n", __func__);
        pPluginFuncs = &samplepluginFuncs;
    }
    else {
        LOG_DBG("%s: using nvplugin for algorithm control\n", __func__);
        pPluginFuncs = &nvpluginFuncs;
    }

    // set the event callback handler
    ctx->captureSS.pEventCallback = &IPPEventHandler;
    ctx->captureSS.pEventCallbackContext = (void *)ctx;

    // set the global time stamp callback handler
    ctx->captureSS.pGlobalTimeStamp = &IPPGetAbsoluteGlobalTime;

    // set a reference to the algorithm control plugin functions
    ctx->captureSS.pPluginFuncs = pPluginFuncs;

    ctx->captureAndProcess = CaptureAndProcessCreate(&ctx->captureSS);

    if (NULL == ctx->captureAndProcess) {
        LOG_ERR("%s: Failed to create capture and encode instance\n", __func__);
        goto failed;
    }

    ctx->pQuit = &mainCtx->quit;

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    StagingFini(mainCtx);
    return NVMEDIA_STATUS_ERROR;
}

NvMediaStatus
StagingStart (
        NvMainContext *mainCtx)
{
    NvMediaStatus   status;
    StreamCtx       *ctx;
    NvU32           index;

    if ((NULL == mainCtx) ||
        (NULL == mainCtx->ctxs[STREAMING_ELEMENT])) {
        LOG_ERR("%s: invalid context\n",
                __func__);

        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    ctx = mainCtx->ctxs[STREAMING_ELEMENT];
    // Start the capture encode pipes
    status = CaptureAndProcessStart (ctx->captureAndProcess);

    if (NVMEDIA_STATUS_OK != status) {
        goto failed;
    }

    for (index = 0; index < MAX_NUM_OF_PIPES; index+=1) {
        ctx->totalFrameCount[index] = 0;
        ctx->totalFrameDrops[index] = 0;
    }

    ctx->startStreamingTime_us = 0;
    ctx->stopStreamingTime_us = 0;

    // Start ExtImgDevice
    if(ctx->captureSS.extImgDevice)
        ExtImgDevStart(ctx->captureSS.extImgDevice);

    GetTimeMicroSec(&ctx->startStreamingTime_us);

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    StagingFini(mainCtx);

    return NVMEDIA_STATUS_ERROR;
}

NvMediaStatus StagingGetOutput (
        NvMainContext               *mainCtx,
        NvMediaIPPComponentOutput   *output,
        NvU32                       pipeNum,
        NvU32                       outputNum)
{
    StreamCtx       *ctx;
    NvMediaStatus   status;

    if ((NULL == mainCtx) ||
        (NULL == output)) {
        LOG_ERR("%s: invalid parameter to IPP\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    ctx = mainCtx->ctxs[STREAMING_ELEMENT];
    if (NULL == ctx) {
        LOG_ERR("%s: invalid context\n",
                __func__);
    }

    status = CaptureAndProcessGetOutput(ctx->captureAndProcess,
                                        output,
                                        pipeNum,
                                        outputNum);

    return status;
}

NvMediaStatus StagingPutOutput (
        NvMainContext               *mainCtx,
        NvMediaIPPComponentOutput   *output,
        NvU32                       pipeNum,
        NvU32                       outputNum)
{
    StreamCtx      *ctx;
    NvMediaStatus   status;

    if ((NULL == mainCtx) ||
        (NULL == output)) {
        LOG_ERR("%s: invalid parameter to IPP\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    ctx = mainCtx->ctxs[STREAMING_ELEMENT];

    if (NULL == ctx) {
        LOG_ERR("%s: invalid context\n",
                __func__);
    }

    status = CaptureAndProcessPutOutput(ctx->captureAndProcess,
                                        output,
                                        pipeNum,
                                        outputNum);

    return status;
}

NvMediaStatus
StagingStop (NvMainContext *mainCtx)
{
    NvMediaStatus   status;
    StreamCtx       *ctx;
    float           average_frame_rate_fps;
    NvU64           total_stream_time_us;
    CaptureParams   *pCaptureParams;
    NvU32           discreteFrameCount;
    NvU32           count;

    if ((NULL == mainCtx) ||
        (NULL == mainCtx->ctxs[STREAMING_ELEMENT])) {

        LOG_ERR("%s: invalid context\n",
                __func__);

        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    ctx = mainCtx->ctxs[STREAMING_ELEMENT];

    pCaptureParams = &(ctx->captureSS.captureParams);

    if (NVMEDIA_TRUE == pCaptureParams->useVirtualChannels) {
        discreteFrameCount = ctx->imagesNum;
    } else {
        discreteFrameCount = 1;
    }

    GetTimeMicroSec(&ctx->stopStreamingTime_us);

    status = CaptureAndProcessStop(ctx->captureAndProcess);

    total_stream_time_us = ctx->stopStreamingTime_us - ctx->startStreamingTime_us;

    for (count = 0; count < discreteFrameCount; count+=1) {
        average_frame_rate_fps = ctx->totalFrameCount[count]/(total_stream_time_us/1000000.0);

        LOG_MSG("%s: cameraID: %d, frames captured = %d, frames dropped = %d, average frame rate = %.2f\n",
                __func__,
                count,
                ctx->totalFrameCount[count],
                ctx->totalFrameDrops[count],
                average_frame_rate_fps);
    }

    return status;
}

NvMediaStatus
StagingFini (NvMainContext *mainCtx)
{
    StreamCtx *ctx;

    if (NULL == mainCtx) {
        return NVMEDIA_STATUS_OK;
    }

    ctx = mainCtx->ctxs[STREAMING_ELEMENT];

    if (NULL == ctx) {
        return NVMEDIA_STATUS_OK;
    }

    // destroy the capture encode instance
    CaptureAndProcessDestroy(ctx->captureAndProcess);

    if(ctx->captureSS.extImgDevice) {
        ExtImgDevStop(ctx->captureSS.extImgDevice);
        ExtImgDevDeinit(ctx->captureSS.extImgDevice);
    }

    free(ctx);

    mainCtx->ctxs[STREAMING_ELEMENT] = NULL;

    return NVMEDIA_STATUS_OK;
}
