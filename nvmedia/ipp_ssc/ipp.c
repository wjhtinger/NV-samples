/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>

#include "log_utils.h"
#include "misc_utils.h"
#include "ipp.h"
#include "nv_emb_stats_plugin.h"

#include "display.h"
#include "write.h"

#define IMAGE_BUFFERS_SAVE_MODE     10
#define IMAGE_BUFFERS_DISP_MODE     3
#define SENSOR_BUFFERS_POOL_SIZE    3
#define BUFFER_POOLS_COUNT          3
#define GET_FRAME_TIMEOUT 500

// Callback function for global time
static NvMediaStatus
IPPGetAbsoluteGlobalTime(void *clientContext, NvMediaGlobalTime *timeValue)
{
    struct timeval tv;

    if (!timeValue) {
        return NVMEDIA_STATUS_ERROR;
    }

    gettimeofday(&tv, NULL);

    // Convert timeval to 64-bit microseconds
    *timeValue = (NvU64)tv.tv_sec * 1000000 + (NvU64)tv.tv_usec;

    return NVMEDIA_STATUS_OK;
}

// Event callback function for IPP in case of an event
static void
IPPEventHandler(
    void *clientContext,
    NvMediaIPPComponentType componentType,
    NvMediaIPPComponent *ippComponent,
    NvMediaIPPEventType etype,
    NvMediaIPPEventData *edata)
{
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
                default:
                    break;
            }
            break;
        }
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

static NvMediaStatus
setICPSettings(IPPCtx *ctx,
               NvMediaICPSettings *icpSettings,
               ExtImgDevProperty *property,
               CaptureConfigParams *captureParams)
{
    if (!strcasecmp(captureParams->interface, "csi-ab")) {
        icpSettings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB;
    }
    else if (!strcasecmp(captureParams->interface, "csi-cd")) {
        icpSettings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_CD;
    }
    else if (!strcasecmp(captureParams->interface, "csi-ef")) {
        icpSettings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_EF;
    }
    else {
        LOG_ERR("%s: Bad interface-type specified: %s\n", __func__, captureParams->interface);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    icpSettings->inputFormat.inputFormatType = property->inputFormatType;
    icpSettings->inputFormat.bitsPerPixel = property->bitsPerPixel;
    icpSettings->inputFormat.pixelOrder = property->pixelOrder;

    memcpy(&icpSettings->surfaceFormat, &ctx->inputSurfFormat,
           sizeof(NvMediaICPSurfaceFormat));
    icpSettings->width = property->width * ctx->imagesNum;
    icpSettings->height = property->height;
    icpSettings->startX = 0;
    icpSettings->startY = 0;
    icpSettings->embeddedDataLines = property->embLinesTop + property->embLinesBottom;
    icpSettings->interfaceLanes = captureParams->csiLanes;
    /* pixel frequency is from imgDevPropery, it is calculated by (VTS * HTS * FrameRate) * n sensors */
    icpSettings->pixelFrequency = property->pixelFrequency;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
IPPCreateICPComponent(IPPCtx *ctx)
{
    NvMediaIPPIcpComponentConfig config;
    NvMediaIPPBufferPoolParams *bufferPools[BUFFER_POOLS_COUNT + 1], bufferPool;
    NvU32 i;

    memset(&bufferPool, 0, sizeof(NvMediaIPPBufferPoolParams));
    memset(&config, 0, sizeof(NvMediaIPPIcpComponentConfig));
    bufferPools[0] = &bufferPool;

    // buffer pool configuration
    bufferPool.portType = NVMEDIA_IPP_PORT_IMAGE_1;
    bufferPool.poolBuffersNum = ctx->saveEnabled ? IMAGE_BUFFERS_SAVE_MODE:
                                                   IMAGE_BUFFERS_DISP_MODE;
    bufferPool.height = ctx->inputHeight;
    bufferPool.surfaceType = ctx->inputSurfType;
    bufferPool.surfAttributes = ctx->inputSurfAttributes;
    // CPU access attribute for faster access of buffer in CPU
    bufferPool.surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_CPU_ACCESS;
    bufferPool.surfAdvConfig = ctx->inputSurfAdvConfig;
    bufferPool.width = ctx->inputWidth;

    /*flag to allocate capture surfaces*/
    bufferPool.surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_CAPTURE;

    if (ctx->imagesNum > 1) {
        bufferPool.imageClass = NVMEDIA_IMAGE_CLASS_AGGREGATE_IMAGES;
        bufferPool.imagesCount = ctx->imagesNum;
        bufferPool.createSiblingsFlag = NVMEDIA_TRUE;
        bufferPool.siblingAttributes = NVMEDIA_IMAGE_ATTRIBUTE_SIBLING_USE_OFFSET;
    }

    else {
        bufferPool.imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
        bufferPool.imagesCount = 1;
    }

    // ICP component configuration
    config.interfaceFormat = NVMEDIA_IMAGE_CAPTURE_INTERFACE_FORMAT_CSI;
    config.settings = &ctx->icpSettings;
    config.siblingsNum = (ctx->imagesNum > 1) ? ctx->imagesNum : 0;

    // create ICP component
    ctx->icpComponent = NvMediaIPPComponentCreate(ctx->ippPipeline[0],
                                                  NVMEDIA_IPP_COMPONENT_CAPTURE,
                                                  bufferPools,
                                                  &config);
    if (!ctx->icpComponent) {
        LOG_ERR("%s: Failed to create ICP Component\n", __func__);
        goto failed;
    }

    LOG_DBG("%s: NvMediaIPPComponentCreate ICP\n", __func__);

    // share ICP component with all pipelines
    for (i = 1; i < ctx->ippPipelineNum; i++) {
        if (IsFailed(NvMediaIPPComponentAddToPipeline(ctx->ippPipeline[i],
                                                      ctx->icpComponent))) {
            LOG_ERR("%s: Failed to add ICP Component to IPP %d\n", __func__, i);
            goto failed;
        }
    }

    return NVMEDIA_STATUS_OK;

failed:
    return NVMEDIA_STATUS_ERROR;
}

static NvMediaStatus
IPPCreateISCComponent(IPPCtx *ctx)
{
    NvMediaIPPIscComponentConfig config;
    NvU32 i;

    // create ISC component
    for (i = 0; i < ctx->ippPipelineNum; i++) {

        memset(&config, 0, sizeof(NvMediaIPPIscComponentConfig));

        // component configuration
        config.iscRootDevice = ctx->extImgDevice->iscRoot;
        config.iscAggregatorDevice = ctx->extImgDevice->iscDeserializer;
        config.iscSerializerDevice = ctx->extImgDevice->iscBroadcastSerializer;
        config.iscSensorDevice = ctx->extImgDevice->iscSensor[i];

        ctx->iscComponent[i] = NvMediaIPPComponentCreate(ctx->ippPipeline[i],
                                                         NVMEDIA_IPP_COMPONENT_SENSOR_CONTROL,
                                                         NULL,
                                                         &config);
        if (!ctx->iscComponent[i]) {
            LOG_ERR("%s: Failed to create ISC Component\n", __func__);
            goto failed;
        }

        LOG_DBG("%s: NvMediaIPPComponentCreate ISC Component\n", __func__);
    }

    return NVMEDIA_STATUS_OK;

failed:
    return NVMEDIA_STATUS_ERROR;
}

static NvMediaStatus
IPPCreateControlAlgorithmComponent(IPPCtx *ctx)
{
    NvMediaIPPControlAlgorithmComponentConfig config;
    NvMediaIPPBufferPoolParams *bufferPools[BUFFER_POOLS_COUNT + 1], bufferPool;
    NvMediaIPPPluginFuncs sscPluginFuncs;
    NvU32 i;

    memset(&bufferPool, 0, sizeof(NvMediaIPPBufferPoolParams));
    memset(&sscPluginFuncs, 0, sizeof(NvMediaIPPPluginFuncs));
    bufferPools[0] = &bufferPool;

    // plugin configuration
    sscPluginFuncs.createFunc = &NvEmbStatsPluginCreate;
    sscPluginFuncs.destroyFunc = &NvEmbStatsPluginDestroy;
    sscPluginFuncs.processExFunc = &NvEmbStatsPluginProcessEx;

    // buffer pool configuration
    bufferPool.portType = NVMEDIA_IPP_PORT_SENSOR_CONTROL_1;
    bufferPool.poolBuffersNum = SENSOR_BUFFERS_POOL_SIZE;

    // create Control Algorithm component
    for (i = 0; i < ctx->ippPipelineNum; i++) {

        memset(&config, 0, sizeof(NvMediaIPPControlAlgorithmComponentConfig));

        // component configuration
        config.width = ctx->inputWidth;
        config.height = ctx->inputHeight;
        config.pixelOrder = ctx->inputSurfAdvConfig.pixelOrder;
        config.bitsPerPixel = ctx->inputSurfAdvConfig.bitsPerPixel;
        config.iscSensorDevice = ctx->extImgDevice->iscSensor[i];
        config.pluginFuncs = &sscPluginFuncs;

        ctx->controlAlgorithmComponent[i] = NvMediaIPPComponentCreate(ctx->ippPipeline[i],
                                                                      NVMEDIA_IPP_COMPONENT_CONTROL_ALGORITHM,
                                                                      bufferPools,
                                                                      &config);
        if (!ctx->controlAlgorithmComponent[i]) {
            LOG_ERR("%s: Failed to create Control Algorithm Component\n", __func__);
            goto failed;
        }

        LOG_DBG("%s: NvMediaIPPComponentCreate Control Algorithm Component\n",
                __func__);
    }

    return NVMEDIA_STATUS_OK;

failed:
    return NVMEDIA_STATUS_ERROR;
}

static NvMediaStatus
IPPCreateOutputComponent(IPPCtx *ctx)
{
    NvU32 i;

    // create Output Component
    for (i = 0; i < ctx->ippPipelineNum; i++) {
        ctx->outputComponent[i] = NvMediaIPPComponentCreate(ctx->ippPipeline[i],
                                                            NVMEDIA_IPP_COMPONENT_OUTPUT,
                                                            NULL,
                                                            NULL);
        if (!ctx->outputComponent) {
            LOG_ERR("%s: Failed to create Output Component\n", __func__);
            goto failed;
        }

        LOG_DBG("%s: NvMediaIPPComponentCreate Output Component\n", __func__);
    }

    return NVMEDIA_STATUS_OK;

failed:
    return NVMEDIA_STATUS_ERROR;
}

static NvMediaStatus
IPPAttachComponents(IPPCtx *ctx)
{
    NvU32 i;

    for (i = 0; i < ctx->ippPipelineNum; i++) {
        if (IsFailed(NvMediaIPPComponentAttach(ctx->ippPipeline[i],
                                               ctx->icpComponent,
                                               ctx->outputComponent[i],
                                               NVMEDIA_IPP_PORT_IMAGE_1))) {
            LOG_ERR("%s: NvMedia IPPComponentAttach failed between ICP and Output components\n",
                    __func__);
            goto failed;
        }

        if (IsFailed(NvMediaIPPComponentAttach(ctx->ippPipeline[i],
                                               ctx->icpComponent,
                                               ctx->controlAlgorithmComponent[i],
                                               NVMEDIA_IPP_PORT_IMAGE_1))) {
            LOG_ERR("%s: NvMedia IPPComponentAttach failed between ICP and Control Algorithm components\n",
                    __func__);
            goto failed;
        }

        if (IsFailed(NvMediaIPPComponentAttach(ctx->ippPipeline[i],
                                               ctx->controlAlgorithmComponent[i],
                                               ctx->iscComponent[i],
                                               NVMEDIA_IPP_PORT_SENSOR_CONTROL_1))) {
            LOG_ERR("%s: NvMedia IPPComponentAttach failed between Control Algorithm and ISC components\n",
                    __func__);
            goto failed;
        }
    }

    return NVMEDIA_STATUS_OK;

failed:
    return NVMEDIA_STATUS_ERROR;
}

static NvMediaStatus
IPPCreateIPPComponents(IPPCtx *ctx)
{
    if (IsFailed(IPPCreateICPComponent(ctx))) {
        LOG_ERR("%s: Failed to create ICP Component\n", __func__);
        goto failed;
    }

    if (IsFailed(IPPCreateISCComponent(ctx))) {
        LOG_ERR("%s: Failed to create ISC Component\n", __func__);
        goto failed;
    }

    if (IsFailed(IPPCreateControlAlgorithmComponent(ctx))) {
        LOG_ERR("%s: Failed to create Control Algorithm Component\n", __func__);
        goto failed;
    }

    if (IsFailed(IPPCreateOutputComponent(ctx))) {
        LOG_ERR("%s: Failed to create Output Component\n", __func__);
        goto failed;
    }

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed to initialize IPP\n", __func__);
    return NVMEDIA_STATUS_ERROR;
}

static NvMediaStatus
GetOutputThreadFunc(void *data)
{
    NvMediaIPPComponentOutput output;
    NvU32 pipelineNum;
    NvMediaStatus status;

    ThreadData *getOutputThreadData = (ThreadData *)data;
    if (!getOutputThreadData) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    IPPCtx *ctx = (IPPCtx *)getOutputThreadData->ctx;
    pipelineNum = getOutputThreadData->threadId;

    while(!ctx->quit) {
        // Get image from Output component
        status = NvMediaIPPComponentGetOutput(ctx->outputComponent[pipelineNum],
                                              GET_FRAME_TIMEOUT,
                                              &output);
        if (IsFailed(status)) {
            if (status == NVMEDIA_STATUS_TIMED_OUT) {
                LOG_DBG("%s: Get output timed out\n", __func__);
                continue;
            }
            else {
                LOG_ERR("%s: Failed to get output from Output component\n", __func__);
                return NVMEDIA_STATUS_ERROR;
            }
        }

        if (ctx->showTimeStamp) {
            NvMediaGlobalTime globalTimeStamp;
            // Get the image timestamp
            if (IsSucceed(NvMediaImageGetGlobalTimeStamp(output.image,
                                                         &globalTimeStamp))) {
                LOG_INFO("IPP: Timestamp: %lld.%06lld\n",
                         globalTimeStamp / 1000000,
                         globalTimeStamp % 1000000);
            }
            else {
                LOG_DBG("%s: Get time-stamp failed\n", __func__);
            }
        }

        // Display image
        DisplayImage(ctx, pipelineNum, output.image);

        // Record image
        WriteRawImage(ctx, pipelineNum, output.image);

        // Return image back to Output component
        if (IsFailed(NvMediaIPPComponentReturnOutput(ctx->outputComponent[pipelineNum],
                                                     &output))) {
            LOG_ERR("Failed to return output to output component\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    return NVMEDIA_STATUS_OK;
}

//IPP Initialization
NvMediaStatus
IPPInit(IPPCtx *ctx, TestArgs *args)
{
    CaptureConfigParams *captureParams;
    NvMediaIPPPipelineProperty ippPipelineProperty;
    NvBool useEmbStats = NVMEDIA_TRUE;
    NvU32 setId;
    NvU32 i;

    if (!ctx || !args) {
        LOG_ERR("%s: Bad parameter", __func__);
        goto failed;
    }

    memset(ctx->ippPipeline, 0, sizeof(ctx->ippPipeline));
    memset(ctx->controlAlgorithmComponent, 0, sizeof(ctx->controlAlgorithmComponent));
    memset(ctx->iscComponent, 0, sizeof(ctx->iscComponent));
    memset(ctx->getOutputThreadData, 0, sizeof(ctx->getOutputThreadData));
    memset(ctx->getOutputThread, 0, sizeof(ctx->getOutputThread));

    setId = args->configId;
    captureParams = &args->captureConfigs[setId];
    ctx->ippPipelineNum = ctx->imagesNum;

    // configure pipeline properties
    ippPipelineProperty.type = NVMEDIA_IPP_PIPELINE_PROPERTY_ONLY_EMB_STATS;
    ippPipelineProperty.value = &useEmbStats;

    // configure ICP settings
    if (IsFailed(setICPSettings(ctx,
                                &ctx->icpSettings,
                                &ctx->extImgDevice->property,
                                captureParams))) {
        LOG_ERR("%s: Failed to set ICP settings\n", __func__);
        goto failed;
    }

    // create IPP Manager
    ctx->ippManager = NvMediaIPPManagerCreate(NVMEDIA_IPP_VERSION_INFO,
                                              ctx->device,
                                              NULL);
    if (!ctx->ippManager) {
        LOG_ERR("%s: Failed to create IPP Manager\n", __func__);
        goto failed;
    }

    if (IsFailed(NvMediaIPPManagerSetTimeSource(ctx->ippManager,
                                                NULL,
                                                IPPGetAbsoluteGlobalTime))) {
        LOG_ERR("%s: Failed to set time source\n", __func__);
        goto failed;
    }

    if (IsFailed(NvMediaIPPManagerSetEventCallback(ctx->ippManager,
                                                   ctx,
                                                   IPPEventHandler))) {
        LOG_ERR("%s: Failed to set IPP event callback\n", __func__);
        goto failed;
    }

    // create pipelines
    for (i = 0; i < ctx->ippPipelineNum; i++) {
        ctx->ippPipeline[i] = NvMediaIPPPipelineCreate(ctx->ippManager);
        if (!ctx->ippPipeline[i]) {
            LOG_ERR("%s: Failed to create IPP Pipeline\n", __func__);
            goto failed;
        }
        // set pipeline properties
        if (IsFailed(NvMediaIPPPipelineSetProperties(ctx->ippPipeline[i],
                                                     1,
                                                     &ippPipelineProperty))) {
            LOG_ERR("%s: Failed to set pipeline property\n", __func__);
            goto failed;
        }
    }

    // create pipeline componenets
    if (IsFailed(IPPCreateIPPComponents(ctx))) {
        LOG_ERR("%s: Failed to create IPP components\n", __func__);
        goto failed;
    }

    // attach pipeline components
    if (IsFailed(IPPAttachComponents(ctx))) {
        LOG_ERR("%s: Failed to attach pipeline components\n", __func__);
        goto failed;
    }

    // create output consumer threads
    for (i = 0; i < ctx->ippPipelineNum; i++) {
        ctx->getOutputThreadData[i].ctx = ctx;
        ctx->getOutputThreadData[i].threadId = i;

        if (IsFailed(NvThreadCreate(&ctx->getOutputThread[i],
                                    &GetOutputThreadFunc,
                                    (void *)&ctx->getOutputThreadData[i],
                                    NV_THREAD_PRIORITY_NORMAL))) {
            LOG_ERR("%s: Failed to create output thread\n", __func__);
            goto failed;
        }
    }

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed to initialize IPP\n", __func__);
    return NVMEDIA_STATUS_ERROR;
}

void
IPPFini(IPPCtx *ctx)
{
    NvU32 i;

    for (i = 0; i < ctx->ippPipelineNum; i++) {
        if (ctx->getOutputThread[i]) {
            NvThreadDestroy(ctx->getOutputThread[i]);
        }
    }

    if (ctx->ippManager) {
        NvMediaIPPManagerDestroy(ctx->ippManager);
    }

}

NvMediaStatus
IPPStart(IPPCtx *ctx)
{
    NvU32 i;

    for (i = 0; i < ctx->ippPipelineNum; i++) {
        if (IsFailed(NvMediaIPPPipelineStart(ctx->ippPipeline[i]))) {
            LOG_ERR("%s: Failed to start pipeline\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    return NVMEDIA_STATUS_OK;
}

