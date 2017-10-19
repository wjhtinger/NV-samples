/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "ipp_component.h"
#include "nvmedia_acp.h"

#define BUFFER_POOLS_COUNT 3

static NvMediaStatus
IPPSetICPBufferPoolConfig (
    IPPCtx *ctx,
    NvMediaIPPBufferPoolParams *poolConfig)
{
    memset(poolConfig, 0, sizeof(NvMediaIPPBufferPoolParams));
    poolConfig->portType = NVMEDIA_IPP_PORT_IMAGE_1;
    poolConfig->poolBuffersNum = IMAGE_BUFFERS_POOL_SIZE;
    poolConfig->height = ctx->inputHeight;
    poolConfig->surfaceType = ctx->inputSurfType;
    poolConfig->surfAttributes = ctx->inputSurfAttributes;
    poolConfig->surfAdvConfig = ctx->inputSurfAdvConfig;

    /*flag to allocate capture surfaces*/
    poolConfig->surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_CAPTURE;

    if(ctx->aggregateFlag) {
        // Use captured image as aggregated
        poolConfig->width = (ctx->inputWidth * ctx->inputFormatWidthMultiplier) / ctx->imagesNum;
        poolConfig->imageClass = NVMEDIA_IMAGE_CLASS_AGGREGATE_IMAGES;
        poolConfig->imagesCount = ctx->imagesNum;
        poolConfig->createSiblingsFlag = NVMEDIA_TRUE;
        poolConfig->siblingAttributes = ctx->useOffsetsFlag ?
                                                NVMEDIA_IMAGE_ATTRIBUTE_SIBLING_USE_OFFSET :
                                                0;
    } else {
        // Single image processing case
        poolConfig->width = ctx->inputWidth * ctx->inputFormatWidthMultiplier;
        poolConfig->imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
        poolConfig->imagesCount = 1;
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
IPPSetCaptureSettings (
    IPPCtx *ctx,
    CaptureConfigParams *config)
{
    NvMediaICPSettings *settings = &ctx->captureSettings;
    NvMediaICPInputFormat *inputFormat = &settings->inputFormat;
    NvMediaICPSurfaceFormat *surfaceFormat = &settings->surfaceFormat;
    ExtImgDevProperty *extImgDevProperty = &ctx->extImgDevice->property;

    if(!strcasecmp(config->interface, "csi-a"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_A;
    else if(!strcasecmp(config->interface, "csi-b"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_B;
    else if(!strcasecmp(config->interface, "csi-ab"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB;
    else if(!strcasecmp(config->interface, "csi-c"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_C;
    else if(!strcasecmp(config->interface, "csi-d"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_D;
    else if(!strcasecmp(config->interface, "csi-cd"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_CD;
    else if(!strcasecmp(config->interface, "csi-e"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_E;
    else if(!strcasecmp(config->interface, "csi-f"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_F;
    else if(!strcasecmp(config->interface, "csi-ef"))
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_EF;
    else {
        LOG_ERR("%s: Bad interface-type specified: %s.Using csi-ab as default\n",
                __func__,
                config->interface);
        settings->interfaceType = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB;
    }

    if(!strcasecmp(config->inputFormat, "422p")) {
        inputFormat->inputFormatType = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_YUV422;
    } else {
        LOG_ERR("%s: Bad input format specified: %s. Using rgba.\n",
                __func__, config->inputFormat);
        inputFormat->inputFormatType = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_RGB888;
    }

    if (!strcasecmp(config->surfaceFormat, "yv12")) {
        ctx->inputSurfType = NvMediaSurfaceType_Image_YUV_420;
        surfaceFormat->surfaceFormatType = NVMEDIA_IMAGE_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_420;
    } else if (!strcasecmp(config->surfaceFormat, "yv16")) {
        ctx->inputSurfType = NvMediaSurfaceType_Image_YUV_422;
        surfaceFormat->surfaceFormatType = NVMEDIA_IMAGE_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_422;
    } else if (!strcasecmp(config->surfaceFormat, "yuyv")) {
        ctx->inputSurfType = NvMediaSurfaceType_Image_YUYV_422;
        surfaceFormat->surfaceFormatType = NVMEDIA_IMAGE_CAPTURE_SURFACE_FORMAT_TYPE_YUYV_422;
    } else {
        LOG_WARN("Bad CSI capture surface format: %s. Using rgb as default\n",
                 config->surfaceFormat);
        surfaceFormat->surfaceFormatType = NVMEDIA_IMAGE_CAPTURE_SURFACE_FORMAT_TYPE_R8G8B8A8;
    }

    ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_EXTRA_LINES;
    ctx->inputSurfAdvConfig.embeddedDataLinesTop = config->embeddedDataLinesTop;
    ctx->inputSurfAdvConfig.embeddedDataLinesBottom = config->embeddedDataLinesBottom;
    LOG_DBG("Embedded data lines top: %u\nEmbedded data lines bottom: %u\n",
            config->embeddedDataLinesTop, config->embeddedDataLinesBottom);
    ctx->rawBytesPerPixel = ((ctx->inputSurfAttributes & NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL) &&
                              ctx->inputSurfAdvConfig.bitsPerPixel > NVMEDIA_BITS_PER_PIXEL_8) ? 2 : 1;

    LOG_DBG("%s: Setting capture parameters\n", __func__);
    if(sscanf(config->resolution,
              "%hux%hu",
              &settings->width,
              &settings->height) != 2) {
        LOG_ERR("%s: Bad resolution: %s\n", __func__, config->resolution);
        return NVMEDIA_STATUS_ERROR;
    }

    settings->width *= ctx->inputFormatWidthMultiplier;

    if(ctx->aggregateFlag)
        settings->width *= ctx->imagesNum;

    settings->startX = 0;
    settings->startY = 0;
    settings->embeddedDataLines = config->embeddedDataLinesTop + config->embeddedDataLinesBottom;
    settings->interfaceLanes = config->csiLanes;
    /* pixel frequency is from imgDevPropery, it is calculated by (VTS * HTS * FrameRate) * n sensors */
    settings->pixelFrequency = extImgDevProperty->pixelFrequency;

    LOG_INFO("Capture params:\nInterface type: %u\nStart X,Y: %u,%u\n",
             settings->interfaceType, settings->startX, settings->startY);
    LOG_INFO("resolution: %ux%u\nextra-lines: %u\ninterface-lanes: %u\n\n",
             settings->width, settings->height, settings->embeddedDataLines, settings->interfaceLanes);

    return NVMEDIA_STATUS_OK;
}

// Create Capture Component
static NvMediaStatus IPPCreateCaptureComponent(IPPCtx *ctx)
{
    NvMediaIPPIcpComponentConfig icpConfig;
    NvMediaIPPBufferPoolParams *bufferPools[BUFFER_POOLS_COUNT + 1], bufferPool;
    // Create Capture component
    bufferPools[0] = &bufferPool;
    bufferPools[1] = NULL;
    IPPSetICPBufferPoolConfig(ctx, &bufferPool);
    NvU32 i;

    memset(&icpConfig, 0, sizeof(NvMediaIPPIcpComponentConfig));
    icpConfig.interfaceFormat = NVMEDIA_IMAGE_CAPTURE_INTERFACE_FORMAT_CSI;
    icpConfig.settings = &ctx->captureSettings;
    icpConfig.siblingsNum = (ctx->aggregateFlag) ? ctx->imagesNum:0;
    icpConfig.registerImageBuffersWithIPA = NVMEDIA_FALSE;

    ctx->ippComponents[0][0] =
            NvMediaIPPComponentCreate(ctx->ipp[0],                   //ippPipeline
                                      NVMEDIA_IPP_COMPONENT_CAPTURE,    //componentType
                                      bufferPools,                     //bufferPools
                                      &icpConfig);                      //componentConfig
    if (!ctx->ippComponents[0][0]) {
        LOG_ERR("%s: Failed to create image capture component\n", __func__);
        goto failed;
    }
    LOG_DBG("%s: NvMediaIPPComponentCreate capture\n", __func__);

    ctx->componentNum[0]++;
    // Put ICP as first component in all other pipelines
    for (i=1; i<ctx->ippNum; i++) {
        if (IsFailed(NvMediaIPPComponentAddToPipeline(ctx->ipp[i], ctx->ippComponents[0][0]))) {
            LOG_ERR("%s: Failed to add image capture component to IPP %d\n", __func__, i);
            goto failed;
        }
        ctx->ippComponents[i][0] = ctx->ippComponents[0][0];
        ctx->componentNum[i]++;
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

// Create Output component
static NvMediaStatus IPPCreateOutputComponent(IPPCtx *ctx)
{
    NvU32 i;
    for (i=0; i<ctx->ippNum; i++) {
        // Create output component
        if (ctx->outputEnabled[i]) {
            ctx->ippComponents[i][ctx->componentNum[i]] =
                    NvMediaIPPComponentCreate(ctx->ipp[i],                     //ippPipeline
                                              NVMEDIA_IPP_COMPONENT_OUTPUT,    //componentType
                                              NULL,                            //bufferPools
                                              NULL);                           //componentConfig
            if (!ctx->ippComponents[i][ctx->componentNum[i]]) {
                LOG_ERR("%s: Failed to create output component \
                         for pipeline %d\n", __func__, i);
                goto failed;
            }

            LOG_DBG("%s: NvMediaIPPComponentCreate Output\n", __func__);
            ctx->outputComponent[i] = ctx->ippComponents[i][ctx->componentNum[i]];
            ctx->componentNum[i]++;
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

// Add components to Pipeline
static NvMediaStatus IPPAddComponentsToPipeline(IPPCtx *ctx)
{
    NvU32 i, j;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;

    // Building IPPs
    for (i=0; i<ctx->ippNum; i++) {
        for (j=1; j<ctx->componentNum[i]; j++) {
            status = NvMediaIPPComponentAttach(ctx->ipp[i],                 //ippPipeline
                                      ctx->ippComponents[i][j-1],           //srcComponent,
                                      ctx->ippComponents[i][j],             //dstComponent,
                                      NVMEDIA_IPP_PORT_IMAGE_1);            //portType

            if (status != NVMEDIA_STATUS_OK) {
               LOG_ERR("%s: NvMediaIPPComponentAttach failed \
                        for IPP %d, component %d -> %d", __func__, i, j-1, j);
               goto failed;
            }
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

// Create YUV Pipeline
NvMediaStatus IPPCreateYUVPipeline(IPPCtx *ctx)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU32 i;
    if(!ctx) {
        LOG_ERR("%s: Bad parameters \n", __func__);
        goto failed;
    }

    // Create IPPs
    for (i=0; i<ctx->ippNum; i++) {
        ctx->ipp[i] = NvMediaIPPPipelineCreate(ctx->ippManager);
        if(!ctx->ipp[i]) {
            LOG_ERR("%s: Failed to create ipp %d\n", __func__, i);
            goto failed;
        }
    }

    // Build IPP components for each IPP pipeline
    memset(ctx->ippComponents, 0, sizeof(ctx->ippComponents));
    memset(ctx->componentNum, 0, sizeof(ctx->componentNum));

    // Create Capture component
    status = IPPCreateCaptureComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Capture Component \n", __func__);
        goto failed;
    }

    // Create Output component
    status = IPPCreateOutputComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Output Component \n", __func__);
        goto failed;
    }

    status = IPPAddComponentsToPipeline(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to build IPP components \n", __func__);
        goto failed;
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

