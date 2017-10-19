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
#include "sample_plugin.h"
#include "nvmedia_acp.h"

#define BUFFER_POOLS_COUNT 3

static NvMediaStatus
IPPSetICPBufferPoolConfig (
    IPPCtx *ctx,
    NvMediaIPPBufferPoolParams *poolConfig)
{
    memset(poolConfig, 0, sizeof(NvMediaIPPBufferPoolParams));

    poolConfig->width = ctx->inputWidth;
    poolConfig->height = ctx->inputHeight;
    poolConfig->portType = NVMEDIA_IPP_PORT_IMAGE_1;
    poolConfig->poolBuffersNum = IMAGE_BUFFERS_POOL_SIZE;
    poolConfig->surfaceType = ctx->inputSurfType;
    poolConfig->surfAttributes = ctx->inputSurfAttributes;
    poolConfig->surfAdvConfig = ctx->inputSurfAdvConfig;

    /*flag to allocate capture surfaces*/
    poolConfig->surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_CAPTURE;

    if((ctx->imagesNum > 1) && !ctx->useVirtualChannels) {
        // Use captured image as aggregated image
        poolConfig->imageClass = NVMEDIA_IMAGE_CLASS_AGGREGATE_IMAGES;
        poolConfig->createSiblingsFlag = NVMEDIA_TRUE;
        poolConfig->siblingAttributes = NVMEDIA_IMAGE_ATTRIBUTE_SIBLING_USE_OFFSET;
        poolConfig->imagesCount = ctx->imagesNum;
    } else {
        // Use captured image as single image
        poolConfig->imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
        poolConfig->imagesCount = 1;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
IPPSetISPBufferPoolConfig (
    IPPCtx *ctx,
    NvMediaIPPBufferPoolParams *poolConfig,
    NvMediaIPPBufferPoolParams *poolStatsConfig)
{
    memset(poolConfig, 0, sizeof(NvMediaIPPBufferPoolParams));
    poolConfig->portType = NVMEDIA_IPP_PORT_IMAGE_1;
    poolConfig->poolBuffersNum = IMAGE_BUFFERS_POOL_SIZE;
    poolConfig->height = ctx->inputHeight;
    poolConfig->surfaceType = ctx->ispOutType;
    poolConfig->surfAttributes = ctx->inputSurfAttributes;
    if(poolConfig->surfaceType == NvMediaSurfaceType_Image_YUV_420) {
        poolConfig->surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_SEMI_PLANAR |
                                      NVMEDIA_IMAGE_ATTRIBUTE_UNMAPPED;
    }
    poolConfig->imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
    poolConfig->imagesCount = 1;
    poolConfig->createSiblingsFlag = NVMEDIA_FALSE;
    poolConfig->width = ctx->inputWidth;

    // Configure statistics port
    memset(poolStatsConfig, 0, sizeof(NvMediaIPPBufferPoolParams));
    poolStatsConfig->portType = NVMEDIA_IPP_PORT_STATS_1;
    poolStatsConfig->poolBuffersNum = STATS_BUFFERS_POOL_SIZE;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
IPPSetControlAlgorithmBufferPoolConfig (
    IPPCtx *ctx,
    NvMediaIPPBufferPoolParams *poolConfig)
{
    memset(poolConfig, 0, sizeof(NvMediaIPPBufferPoolParams));
    poolConfig->portType = NVMEDIA_IPP_PORT_SENSOR_CONTROL_1;
    poolConfig->poolBuffersNum = SENSOR_BUFFERS_POOL_SIZE;

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

    if(!strcasecmp(config->inputFormat, "raw12")) {
        inputFormat->inputFormatType = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_RAW;
        inputFormat->bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_12;
        inputFormat->pixelOrder = extImgDevProperty->pixelOrder;
        ctx->rawCompressionFormat = RAW1x12;
    } else {
        LOG_ERR("%s: Bad input format specified: %s. Using rgba.\n",
                __func__, config->inputFormat);
        inputFormat->inputFormatType = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_RGB888;
    }

    if (!strcasecmp(config->surfaceFormat, "raw12")) {
        ctx->inputSurfType = NvMediaSurfaceType_Image_RAW;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_RAW_PIXEL_ORDER;
        ctx->inputSurfAdvConfig.bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_12;
        ctx->inputSurfAdvConfig.pixelOrder = extImgDevProperty->pixelOrder;
        surfaceFormat->surfaceFormatType = NVMEDIA_IMAGE_CAPTURE_SURFACE_FORMAT_TYPE_RAW;
        surfaceFormat->bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_12;
        surfaceFormat->pixelOrder = extImgDevProperty->pixelOrder;
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

    if((ctx->imagesNum > 1) && !ctx->useVirtualChannels)
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

// Create Sensor Control Component
static NvMediaStatus IPPCreateSensorControlComponent(IPPCtx *ctx)
{
    NvU32 i;
    NvMediaIPPIscComponentConfig iscComponentConfig;

    memset(&iscComponentConfig, 0, sizeof(NvMediaIPPIscComponentConfig));
    for (i = 0; i < ctx->ippNum; i++) {
        iscComponentConfig.sensorEnumerator = i;
        iscComponentConfig.iscRootDevice = ctx->extImgDevice->iscRoot;
        iscComponentConfig.iscAggregatorDevice = ctx->extImgDevice->iscDeserializer;
        //iscComponentConfig.iscSerializerDevice = ctx->extImgDevice->iscSerializer[i];
        iscComponentConfig.iscSerializerDevice = ctx->extImgDevice->iscBroadcastSerializer;
        iscComponentConfig.iscSensorDevice = ctx->extImgDevice->iscSensor[i];

        ctx->ippIscComponents[i] = NvMediaIPPComponentCreate(ctx->ipp[i],     //ippPipeline
                                      NVMEDIA_IPP_COMPONENT_SENSOR_CONTROL, //componentType
                                      NULL,                                 //bufferPools
                                      &iscComponentConfig);                 //componentConfig
        if (!ctx->ippIscComponents[i]) {
            LOG_ERR("%s: Failed to create sensor ISC component\n", __func__);
            goto failed;
        }
    }

    LOG_DBG("%s: NvMediaIPPComponentCreate ISC\n", __func__);

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

// Create CaptureEx Component
static NvMediaStatus IPPCreateCaptureComponentEx(IPPCtx *ctx)
{
    NvMediaStatus status;
    NvMediaICPSettingsEx icpSettingsEx;
    NvMediaIPPBufferPoolParams *bufferPools[4 + 1], bufferPool;
    NvU32 i;

    IPPSetICPBufferPoolConfig(ctx, &bufferPool);
    memset(&icpSettingsEx, 0, sizeof(NvMediaICPSettingsEx));

    icpSettingsEx.interfaceType  = ctx->captureSettings.interfaceType;
    icpSettingsEx.interfaceLanes = ctx->captureSettings.interfaceLanes;
    icpSettingsEx.numVirtualChannels = ctx->ippNum;

    for (i = 0; i < ctx->imagesNum; i++) {
        icpSettingsEx.settings[i].virtualChannelIndex = i;
        memcpy(&icpSettingsEx.settings[i].icpSettings, &ctx->captureSettings,
               sizeof(NvMediaICPSettings));
        bufferPools[i] = &bufferPool;
    }
    bufferPools[ctx->imagesNum] = NULL;

    ctx->ippComponents[0][0] =
        NvMediaIPPComponentCreate(ctx->ipp[0],                      //ippPipeline
                                  NVMEDIA_IPP_COMPONENT_CAPTURE_EX, //componentType
                                  bufferPools,                      //bufferPools
                                  &icpSettingsEx);                  //componentConfig
    if (!ctx->ippComponents[0][0]) {
        LOG_ERR("%s: Failed to create image capture component\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    ctx->componentNum[0]++;

    // Put ICP as first component in all other pipelines
    for (i = 1; i< ctx->ippNum; i++) {
        status = NvMediaIPPComponentAddToPipeline(ctx->ipp[i], ctx->ippComponents[0][0]);
        if (IsFailed(status)) {
            LOG_ERR("%s: Failed to add image capture component to IPP %d\n", __func__, i);
            return status;
        }
        ctx->ippComponents[i][0] = ctx->ippComponents[0][0];
        ctx->componentNum[i]++;
    }

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
    icpConfig.siblingsNum = (ctx->imagesNum > 1) ? ctx->imagesNum : 0;
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

// Create ISP Component
static NvMediaStatus IPPCreateISPComponent(IPPCtx *ctx)
{
    NvMediaIPPIspComponentConfig ispConfig;
    NvMediaIPPBufferPoolParams *bufferPools[BUFFER_POOLS_COUNT + 1], bufferPool, bufferPool2;
    NvU32 i;

    for (i=0; i<ctx->ippNum; i++) {
        if (ctx->ispEnabled[i]) {
            memset(&ispConfig, 0, sizeof(NvMediaIPPIspComponentConfig));
            ispConfig.ispSelect = (i<2) ? NVMEDIA_ISP_SELECT_ISP_A : NVMEDIA_ISP_SELECT_ISP_B;
            ispConfig.registerImageBuffersWithIPA = NVMEDIA_FALSE;
            ispConfig.ispSettingsFile = NULL;

            bufferPools[0] = &bufferPool;
            bufferPools[1] = &bufferPool2;
            bufferPools[2] = NULL;
            IPPSetISPBufferPoolConfig(ctx, &bufferPool, &bufferPool2);

            ctx->ippComponents[i][ctx->componentNum[i]] = ctx->ippIspComponents[i] =
                    NvMediaIPPComponentCreate(ctx->ipp[i],               //ippPipeline
                                              NVMEDIA_IPP_COMPONENT_ISP,    //componentType
                                              bufferPools,                  //bufferPools
                                              &ispConfig);                  //componentConfig
            if (!ctx->ippComponents[i][ctx->componentNum[i]]) {
                LOG_ERR("%s: Failed to create image ISP component for pipeline %d\n", __func__, i);
                goto failed;
            }
            LOG_DBG("%s: NvMediaIPPComponentCreate ISP \n", __func__);
            ctx->componentNum[i]++;
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

// Create Control Algorithm Component
static NvMediaStatus IPPCreateControlAlgorithmComponent(IPPCtx *ctx)
{
    NvMediaIPPControlAlgorithmComponentConfig controlAlgorithmConfig;
    NvU32 i;
    NvMediaIPPBufferPoolParams *bufferPools[BUFFER_POOLS_COUNT + 1], bufferPool;
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
    NvMediaIPPPluginFuncs nvbepluginFuncs = {
        .createFunc = &NvMediaBEPCreate,
        .destroyFunc = &NvMediaBEPDestroy,
        .parseConfigurationFunc = NULL,
        .processExFunc = &NvMediaBEPProcessEx
    };

    for (i=0; i<ctx->ippNum; i++) {
        // Create Control Algorithm component (needs valid ISP)
        if (ctx->ispEnabled[i] && ctx->controlAlgorithmEnabled[i]) {
            memset(&controlAlgorithmConfig, 0, sizeof(NvMediaIPPControlAlgorithmComponentConfig));

            controlAlgorithmConfig.width = ctx->inputWidth;
            controlAlgorithmConfig.height = ctx->inputHeight;
            controlAlgorithmConfig.pixelOrder = ctx->inputSurfAdvConfig.pixelOrder;
            controlAlgorithmConfig.bitsPerPixel = ctx->inputSurfAdvConfig.bitsPerPixel;
            controlAlgorithmConfig.iscSensorDevice = ctx->extImgDevice->iscSensor[i];
            if(ctx->pluginFlag == NVMEDIA_SIMPLEACPLUGIN) {
                controlAlgorithmConfig.pluginFuncs = &samplepluginFuncs;
            }
            else if(ctx->pluginFlag == NVMEDIA_NVACPLUGIN){
                controlAlgorithmConfig.pluginFuncs = &nvpluginFuncs;
            }
            else if(ctx->pluginFlag == NVMEDIA_NVBEPLUGIN){
                controlAlgorithmConfig.pluginFuncs = &nvbepluginFuncs;
            }

            bufferPools[0] = &bufferPool;
            bufferPools[1] = NULL;
            IPPSetControlAlgorithmBufferPoolConfig(ctx, &bufferPool);

            ctx->ippControlAlgorithmComponents[i] =
                    NvMediaIPPComponentCreate(ctx->ipp[i],                       //ippPipeline
                                              NVMEDIA_IPP_COMPONENT_CONTROL_ALGORITHM, //componentType
                                              bufferPools,                          //bufferPools
                                              &controlAlgorithmConfig);                 //componentConfig
            if (!ctx->ippControlAlgorithmComponents[i]) {
                LOG_ERR("%s: Failed to create Control Algorithm \
                         component for pipeline %d\n", __func__, i);
                goto failed;
            }
            LOG_DBG("%s: NvMediaIPPComponentCreate Control Algorithm\n", __func__);
        }
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
        if(ctx->ippIspComponents[i] && ctx->ippControlAlgorithmComponents[i]) {
            status = NvMediaIPPComponentAttach(ctx->ipp[i],                     //ippPipeline
                                      ctx->ippIspComponents[i],                 //srcComponent,
                                      ctx->ippControlAlgorithmComponents[i],    //dstComponent,
                                      NVMEDIA_IPP_PORT_STATS_1);                //portType

            if (status != NVMEDIA_STATUS_OK) {
               LOG_ERR("%s: NvMediaIPPComponentAttach failed for \
                        IPP %d, component ISP -> Control Algorithm", __func__, i);
               goto failed;
            }
        }
        if(ctx->ippControlAlgorithmComponents[i] && ctx->ippIscComponents[i]) {
            status = NvMediaIPPComponentAttach(ctx->ipp[i],                          //ippPipeline
                                      ctx->ippControlAlgorithmComponents[i],         //srcComponent,
                                      ctx->ippIscComponents[i],                      //dstComponent,
                                      NVMEDIA_IPP_PORT_SENSOR_CONTROL_1);            //portType

            if (status != NVMEDIA_STATUS_OK) {
               LOG_ERR("%s: NvMediaIPPComponentAttach failed for \
                       IPP %d, component Control Algorithm -> ISC", __func__, i);
               goto failed;
            }
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

// Create Raw Pipeline
NvMediaStatus IPPCreateRawPipeline(IPPCtx*ctx) {
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


    // Create Sensor Control Component
    status = IPPCreateSensorControlComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Sensor Control Component \n", __func__);
        goto failed;
    }

    // Create Capture component
    if(ctx->useVirtualChannels) {
        status = IPPCreateCaptureComponentEx(ctx);
    } else {
        status = IPPCreateCaptureComponent(ctx);
    }

    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Capture Component \n", __func__);
        goto failed;
    }

    // Create ISP Component
    status = IPPCreateISPComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create ISP Component \n", __func__);
        goto failed;
    }

    // Create Control Algorithm Component
    status = IPPCreateControlAlgorithmComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Control algorithm Component \n", __func__);
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

