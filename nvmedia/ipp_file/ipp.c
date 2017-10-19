/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#include "ipp.h"
#include "misc_utils.h"
#include "log_utils.h"
#include "sample_plugin.h"
#include "nvmedia_acp.h"

#define BUFFER_POOLS_COUNT              4
#define IMAGE_BUFFERS_POOL_SIZE         3
#define STATS_BUFFERS_POOL_SIZE         3
#define SENSOR_BUFFERS_POOL_SIZE        3

typedef enum
{
    IPP_PIPE_INDEX_DATA_SOURCE,
    IPP_PIPE_INDEX_ISP,
    IPP_PIPE_INDEX_DATA_SINK,
}IppPipeIndex;

static NvMediaStatus
ipp_SetRawImageProperties (
    IPPTest *ctx,
    CaptureConfigParams *config)
{
    if(0 != strcasecmp(config->inputFormat, "raw12")) {

        LOG_ERR("%s: Bad input format specified: %s.\n",
                __func__, config->inputFormat);
        return NVMEDIA_STATUS_ERROR;
    }

    if (!strcasecmp(config->surfaceFormat, "raw8")) {
        ctx->inputSurfType = NvMediaSurfaceType_Image_RAW;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_RAW_PIXEL_ORDER;
        ctx->inputSurfAdvConfig.bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_8;
        ctx->inputSurfAdvConfig.pixelOrder = ctx->inputPixelOrder;
    } else if (!strcasecmp(config->surfaceFormat, "raw10")) {
        ctx->inputSurfType = NvMediaSurfaceType_Image_RAW;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_RAW_PIXEL_ORDER;
        ctx->inputSurfAdvConfig.bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_10;
        ctx->inputSurfAdvConfig.pixelOrder = ctx->inputPixelOrder;
    } else if (!strcasecmp(config->surfaceFormat, "raw12")) {
        ctx->inputSurfType = NvMediaSurfaceType_Image_RAW;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_RAW_PIXEL_ORDER;
        ctx->inputSurfAdvConfig.bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_12;
        ctx->inputSurfAdvConfig.pixelOrder = ctx->inputPixelOrder;
    } else if (!strcasecmp(config->surfaceFormat, "raw14")) {
        ctx->inputSurfType = NvMediaSurfaceType_Image_RAW;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_RAW_PIXEL_ORDER;
        ctx->inputSurfAdvConfig.bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_14;
        ctx->inputSurfAdvConfig.pixelOrder = ctx->inputPixelOrder;
    } else {
        LOG_ERR("%s: Bad CSI capture surface format: %s\n",
                 __func__,
                 config->surfaceFormat);

        return NVMEDIA_STATUS_ERROR;
    }

    ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_EXTRA_LINES;
    ctx->inputSurfAdvConfig.embeddedDataLinesTop = config->embeddedDataLinesTop;
    ctx->inputSurfAdvConfig.embeddedDataLinesBottom = config->embeddedDataLinesBottom;
    LOG_DBG("Embedded data lines top: %u\nEmbedded data lines bottom: %u\n",
            config->embeddedDataLinesTop, config->embeddedDataLinesBottom);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
ipp_SetReaderBufferPoolConfig (
    IPPTest *ctx,
    NvMediaIPPBufferPoolParams *poolConfig)
{
    memset(poolConfig, 0, sizeof(NvMediaIPPBufferPoolParams));
    poolConfig->portType = NVMEDIA_IPP_PORT_IMAGE_1;
    poolConfig->poolBuffersNum = IMAGE_BUFFERS_POOL_SIZE;
    poolConfig->height = ctx->inputHeight;
    poolConfig->surfaceType = ctx->inputSurfType;
    poolConfig->surfAttributes = ctx->inputSurfAttributes;
    poolConfig->surfAdvConfig = ctx->inputSurfAdvConfig;

    if(ctx->aggregateFlag) {
        // Use captured image as aggregated
        poolConfig->width = ctx->inputWidth/ctx->imagesNum;
        poolConfig->imageClass = NVMEDIA_IMAGE_CLASS_AGGREGATE_IMAGES;
        poolConfig->imagesCount = ctx->imagesNum;
        poolConfig->createSiblingsFlag = NVMEDIA_TRUE;
        poolConfig->siblingAttributes = NVMEDIA_IMAGE_ATTRIBUTE_SIBLING_USE_OFFSET;
    } else {
        // Single image processing case
        poolConfig->width = ctx->inputWidth;
        poolConfig->imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
        poolConfig->imagesCount = 1;
    }

    return NVMEDIA_STATUS_OK;
}

// Create File Reader Component
static NvMediaStatus
ipp_CreateFileReaderComponent(
    IPPTest *ctx)
{
    NvMediaIPPFileReaderComponentConfig readerConfig;
    NvMediaIPPBufferPoolParams *bufferPools[BUFFER_POOLS_COUNT], bufferPool;
    // Create Capture component
    bufferPools[0] = &bufferPool;
    bufferPools[1] = NULL;
    ipp_SetReaderBufferPoolConfig(ctx, &bufferPool);
    NvU32 i;

    memset(&readerConfig, 0, sizeof(readerConfig));
    readerConfig.inputFileName = ctx->inputFileName;
    readerConfig.fileLoopBackCount = 0;
    readerConfig.siblingsNum = (ctx->aggregateFlag) ? ctx->imagesNum : 0;
    readerConfig.iscSensorDevice = ctx->extImgDevice->iscSensor[0];

    ctx->ippComponents[0][0] =
            NvMediaIPPComponentCreate(ctx->ipp[0],                   //ippPipeline
                                      NVMEDIA_IPP_COMPONENT_FILE_READER,  //componentType
                                      bufferPools,                     //bufferPools
                                      &readerConfig);                      //componentConfig
    if (!ctx->ippComponents[0][0]) {
        LOG_ERR("%s: Failed to create File Reader component\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }
    LOG_DBG("%s: NvMediaIPPComponentCreate File Reader\n", __func__);

    ctx->componentNum[0]++;
    // Put source component as first component in all other pipelines
    for (i=1; i<ctx->ippNum; i++) {
        if (IsFailed(NvMediaIPPComponentAddToPipeline(ctx->ipp[i], ctx->ippComponents[0][0]))) {
            LOG_ERR("%s: Failed to File Reader component to IPP %d\n", __func__, i);
            return NVMEDIA_STATUS_ERROR;
        }
        ctx->ippComponents[i][0] = ctx->ippComponents[0][0];
        ctx->componentNum[i]++;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
ipp_SetISPBufferPoolConfig (
    IPPTest *ctx,
    NvMediaIPPBufferPoolParams *poolConfig,
    NvMediaIPPBufferPoolParams *poolConfig2,
    NvMediaIPPBufferPoolParams *poolStatsConfig)
{
    // For human vision
    if(poolConfig) {
        memset(poolConfig, 0, sizeof(NvMediaIPPBufferPoolParams));
        poolConfig->portType = NVMEDIA_IPP_PORT_IMAGE_1;
        poolConfig->poolBuffersNum = IMAGE_BUFFERS_POOL_SIZE;
        poolConfig->height = ctx->inputHeight;
        poolConfig->surfaceType = ctx->ispOutType;
        poolConfig->surfAttributes = ctx->inputSurfAttributes;
        if(poolConfig->surfaceType == NvMediaSurfaceType_Image_YUV_420) {
            poolConfig->surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_SEMI_PLANAR
                                          | NVMEDIA_IMAGE_ATTRIBUTE_UNMAPPED;
        }
        poolConfig->imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
        poolConfig->imagesCount = 1;
        poolConfig->createSiblingsFlag = NVMEDIA_FALSE;
        if(ctx->aggregateFlag) {
            poolConfig->width = ctx->inputWidth / ctx->imagesNum;
        } else {
            poolConfig->width = ctx->inputWidth;
        }
    }

    // For machine vision
    if(poolConfig2) {
        memset(poolConfig2, 0, sizeof(NvMediaIPPBufferPoolParams));
        poolConfig2->portType = NVMEDIA_IPP_PORT_IMAGE_2;
        poolConfig2->poolBuffersNum = IMAGE_BUFFERS_POOL_SIZE;
        poolConfig2->height = ctx->inputHeight;
        poolConfig2->surfaceType = ctx->ispMvSurfaceType;
        poolConfig2->surfAttributes = ctx->inputSurfAttributes;
        if(poolConfig2->surfaceType == NvMediaSurfaceType_Image_YUV_420) {
            poolConfig2->surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_SEMI_PLANAR;
        }
        poolConfig2->imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
        poolConfig2->imagesCount = 1;
        poolConfig2->createSiblingsFlag = NVMEDIA_FALSE;
        if(ctx->aggregateFlag) {
            poolConfig2->width = ctx->inputWidth / ctx->imagesNum;
        } else {
            poolConfig2->width = ctx->inputWidth;
        }
    }

    // Configure statistics port
    memset(poolStatsConfig, 0, sizeof(NvMediaIPPBufferPoolParams));
    poolStatsConfig->portType = NVMEDIA_IPP_PORT_STATS_1;
    poolStatsConfig->poolBuffersNum = STATS_BUFFERS_POOL_SIZE;

    return NVMEDIA_STATUS_OK;
}

// Create ISP Component
static NvMediaStatus
ipp_CreateISPComponent(
    IPPTest *ctx)
{
    NvMediaIPPIspComponentConfig ispConfig;
    NvMediaIPPBufferPoolParams *bufferPools[BUFFER_POOLS_COUNT],
                                bufferPool[BUFFER_POOLS_COUNT];
    NvU32 i;

    for (i=0; i<ctx->ippNum; i++) {
        if (ctx->ispEnabled[i]) {
            memset(&ispConfig, 0, sizeof(NvMediaIPPIspComponentConfig));
            ispConfig.ispSelect = (i<2) ? NVMEDIA_ISP_SELECT_ISP_A : NVMEDIA_ISP_SELECT_ISP_B;

            if(!ctx->ispMvFlag) {
                ipp_SetISPBufferPoolConfig(ctx, &bufferPool[0], NULL, &bufferPool[1]);
                bufferPools[0] = &bufferPool[0];
                bufferPools[1] = &bufferPool[1];
                bufferPools[2] = NULL;
            } else {
                ipp_SetISPBufferPoolConfig(ctx, &bufferPool[0],
                        &bufferPool[1], &bufferPool[2]);
                bufferPools[0] = &bufferPool[0];
                bufferPools[1] = &bufferPool[1];
                bufferPools[2] = &bufferPool[2];
                bufferPools[3] = NULL;
            }

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

static NvMediaStatus
_SetExtImgDevParameters(IPPTest *ctx,
                        CaptureConfigParams *captureParams,
                        ExtImgDevParam *configParam)
{
    unsigned int i;

    configParam->desAddr = captureParams->max9286_address;
    configParam->brdcstSerAddr = captureParams->max9271_address;
    configParam->brdcstSensorAddr =
        captureParams->sensor_address;
    for (i = 0; i < ctx->imagesNum; i++) {
        configParam->serAddr[i] = captureParams->max9271_address;
        configParam->sensorAddr[i] = captureParams->sensor_address;
    }
    configParam->i2cDevice = captureParams->i2cDevice;
    configParam->moduleName = captureParams->inputDevice;
    configParam->board = captureParams->board;
    configParam->resolution = captureParams->resolution;
    configParam->camMap = NULL;
    configParam->sensorsNum = ctx->imagesNum;
    configParam->inputFormat = captureParams->inputFormat;
    configParam->interface = captureParams->interface;
    configParam->enableEmbLines =
        (captureParams->embeddedDataLinesTop || captureParams->embeddedDataLinesBottom) ?
            NVMEDIA_TRUE : NVMEDIA_FALSE;
    configParam->initialized = NVMEDIA_FALSE;
    configParam->enableSimulator = NVMEDIA_TRUE;
    configParam->slave = NVMEDIA_FALSE;
    configParam->enableVirtualChannels = NVMEDIA_FALSE;

    return NVMEDIA_STATUS_OK;
}

// Create Sensor Control Component
static NvMediaStatus ipp_CreateSensorControlComponent(IPPTest *ctx)
{
    NvU32 i;
    NvMediaIPPIscComponentConfig iscComponentConfig;

    memset(&iscComponentConfig, 0, sizeof(NvMediaIPPIscComponentConfig));

    for (i=0; i < ctx->ippNum; i++) {
        iscComponentConfig.iscRootDevice = ctx->extImgDevice->iscRoot;
        iscComponentConfig.iscAggregatorDevice = ctx->extImgDevice->iscDeserializer;
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

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

static NvMediaStatus
ipp_SetControlAlgorithmBufferPoolConfig (
    IPPTest *ctx,
    NvMediaIPPBufferPoolParams *poolConfig)
{
    memset(poolConfig, 0, sizeof(NvMediaIPPBufferPoolParams));
    poolConfig->portType = NVMEDIA_IPP_PORT_SENSOR_CONTROL_1;
    poolConfig->poolBuffersNum = SENSOR_BUFFERS_POOL_SIZE;

    return NVMEDIA_STATUS_OK;
}

// Create Control Algorithm Component
static NvMediaStatus
ipp_CreateControlAlgorithmComponent(IPPTest *ctx)
{
    NvMediaIPPControlAlgorithmComponentConfig controlAlgorithmConfig;
    NvU32 i;
    NvMediaIPPBufferPoolParams *bufferPools[BUFFER_POOLS_COUNT], bufferPool;
    NvMediaIPPPluginFuncs simplepluginFuncs;
    NvMediaIPPPluginFuncs nvpluginFuncs;

    memset((void *)&simplepluginFuncs, 0, sizeof(NvMediaIPPPluginFuncs));
    memset((void *)&nvpluginFuncs, 0, sizeof(NvMediaIPPPluginFuncs));

    simplepluginFuncs.createFunc                = &NvSampleACPCreate;
    simplepluginFuncs.destroyFunc               = &NvSampleACPDestroy;
    simplepluginFuncs.parseConfigurationFunc    = &NvSampleACPParseConfiguration;
    simplepluginFuncs.processFunc               = &NvSampleACPProcess;

    nvpluginFuncs.createFunc                    = &NvMediaACPCreate;
    nvpluginFuncs.destroyFunc                   = &NvMediaACPDestroy;
    nvpluginFuncs.parseConfigurationFunc        = &NvMediaACPParseConfiguration;
    nvpluginFuncs.processFunc                   = &NvMediaACPProcess;

    for (i=0; i<ctx->ippNum; i++) {
        // Create Control Algorithm component (needs valid ISP)
        if (ctx->ispEnabled[i] && ctx->controlAlgorithmEnabled[i]) {
            memset(&controlAlgorithmConfig, 0, sizeof(NvMediaIPPControlAlgorithmComponentConfig));

            controlAlgorithmConfig.width = ctx->inputWidth / ctx->imagesNum;
            controlAlgorithmConfig.height = ctx->inputHeight;
            controlAlgorithmConfig.pixelOrder = ctx->inputPixelOrder;
            controlAlgorithmConfig.bitsPerPixel = ctx->inputSurfAdvConfig.bitsPerPixel;
            controlAlgorithmConfig.iscSensorDevice = ctx->extImgDevice->iscSensor[i];
            if(ctx->pluginFlag == NVMEDIA_SIMPLEACPLUGIN) {
                controlAlgorithmConfig.pluginFuncs = &simplepluginFuncs;
            } else if(ctx->pluginFlag == NVMEDIA_NVACPLUGIN) {
                controlAlgorithmConfig.pluginFuncs = &nvpluginFuncs;
            }

            bufferPools[0] = &bufferPool;
            bufferPools[1] = NULL;
            ipp_SetControlAlgorithmBufferPoolConfig(ctx, &bufferPool);

            ctx->ippControlAlgorithmComponents[i] =
                    NvMediaIPPComponentCreate(ctx->ipp[i],                       //ippPipeline
                                              NVMEDIA_IPP_COMPONENT_CONTROL_ALGORITHM, //componentType
                                              bufferPools,                          //bufferPools
                                              &controlAlgorithmConfig);                 //componentConfig
            if (!ctx->ippControlAlgorithmComponents[i]) {
                LOG_ERR("%s: Failed to create Control Algorithm component for pipeline %d\n", __func__, i);
                goto failed;
            }
            LOG_DBG("%s: NvMediaIPPComponentCreate Control Algorithm\n", __func__);
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

static void
ipp_GetOutputFileNames(
    IPPTest *ctx,
    TestArgs *testArgs)
{
    unsigned int i;
    NvU32 width, height;
    width = ctx->inputWidth/ctx->imagesNum;
    height = ctx->inputHeight;

    ctx->saveIspMvFlag = testArgs->saveIspMvFlag;
    ctx->saveMetadataFlag = testArgs->saveMetadataFlag;

    for(i = 0; i < ctx->imagesNum; i++) {
        snprintf(ctx->outputFile[i], FILE_NAME_MAX_LENGTH,
            "%s_out_%u_%ux%u.yuv",
            testArgs->outputFilePrefix, i,
            width, height);
    }

    if(ctx->saveIspMvFlag) {
        for(i = 0; i < ctx->imagesNum; i++) {
            snprintf(ctx->ispMvOutputFile[i], FILE_NAME_MAX_LENGTH,
                "%s_ispmv_%u_%ux%u.yuv",
                testArgs->saveIspMvPrefix, i,
                width, height);
        }
    }

    if(ctx->saveMetadataFlag) {
        for(i = 0; i < ctx->imagesNum; i++) {
            snprintf(ctx->metadataFile[i], FILE_NAME_MAX_LENGTH,
                "%s_%u.meta", testArgs->saveMetadataPrefix, i);
        }
    }
}

// Create File Writer Component
static NvMediaStatus ipp_CreateFileWriterComponent(IPPTest *ctx)
{
    NvU32 i;
    NvMediaIPPFileWriterComponentConfig writerConfig;
    writerConfig.metadataFileName = NULL;
    for (i=0; i<ctx->ippNum; i++) {
        writerConfig.outputFileName = ctx->outputFile[i];
        if(ctx->saveMetadataFlag) {
            writerConfig.metadataFileName = ctx->metadataFile[i];
        }
        ctx->ippComponents[i][ctx->componentNum[i]] =
                NvMediaIPPComponentCreate(
                    ctx->ipp[i],        //ippPipeline
                    NVMEDIA_IPP_COMPONENT_FILE_WRITER,  //componentType
                    NULL,                //bufferPools
                    &writerConfig);     //componentConfig

        if (!ctx->ippComponents[i][ctx->componentNum[i]]) {
            LOG_ERR("%s: Failed to create File Writer component for pipeline %u\n", __func__, i);
            goto failed;
        }
        LOG_DBG("%s: NvMediaIPPComponentCreate File Writer %u\n", __func__, i);
        ctx->componentNum[i]++;
        ctx->activeFileWriters++;
    }

    writerConfig.metadataFileName = NULL;

    if(ctx->saveIspMvFlag) {
        for (i=0; i<ctx->ippNum; i++) {
            writerConfig.outputFileName = ctx->ispMvOutputFile[i];
            ctx->ispMvFileWriter[i] =
                NvMediaIPPComponentCreate(
                        ctx->ipp[i],        //ippPipeline
                        NVMEDIA_IPP_COMPONENT_FILE_WRITER,  //componentType
                        NULL,                //bufferPools
                        &writerConfig);     //componentConfig

            if (!ctx->ispMvFileWriter[i]) {
                LOG_ERR("%s: Failed to create File Writer component for ISP "
                    "machine vision %u\n", __func__, i);
                goto failed;
            }
            LOG_DBG("%s: NvMediaIPPComponentCreate ISP MV File Writer %u\n", __func__, i);
            ctx->activeFileWriters++;
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

// Build IPP Component
static NvMediaStatus ipp_AttachComponent(IPPTest *ctx)
{
    NvU32 i;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;

    // Building IPPs
    for (i=0; i<ctx->ippNum; i++) {

        // For pipe 'i' connect the image data source (file reader) to ISP component
        status = NvMediaIPPComponentAttach(ctx->ipp[i],                                         //ippPipeline
                                           ctx->ippComponents[i][IPP_PIPE_INDEX_DATA_SOURCE],   //srcComponent,
                                           ctx->ippComponents[i][IPP_PIPE_INDEX_ISP],           //dstComponent,
                                           NVMEDIA_IPP_PORT_IMAGE_1);                           //portType

        if (NVMEDIA_STATUS_OK != status) {

            LOG_ERR("%s: Failed to connect data source to ISP for pipe %d\n",
                    __func__,
                    i);

            goto failed;
        }

        // For pipe 'i' connect the ISP component to output sink (file writer)
        status = NvMediaIPPComponentAttach(ctx->ipp[i],                                         //ippPipeline
                                           ctx->ippComponents[i][IPP_PIPE_INDEX_ISP],           //srcComponent,
                                           ctx->ippComponents[i][IPP_PIPE_INDEX_DATA_SINK],     //dstComponent,
                                           NVMEDIA_IPP_PORT_IMAGE_1);                           //portType

        if (NVMEDIA_STATUS_OK != status) {

            LOG_ERR("%s: Failed to connect ISP to data sink for pipe %d\n",
                    __func__,
                    i);

            goto failed;
        }

        if(ctx->ippIspComponents[i] && ctx->ippControlAlgorithmComponents[i]) {
            status = NvMediaIPPComponentAttach(ctx->ipp[i],                     //ippPipeline
                                      ctx->ippIspComponents[i],                 //srcComponent,
                                      ctx->ippControlAlgorithmComponents[i],    //dstComponent,
                                      NVMEDIA_IPP_PORT_STATS_1);                //portType

            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: NvMediaIPPComponentAttach failed for IPP %d, "
                    "component ISP -> Control Algorithm", __func__, i);
                goto failed;
            }
        }
        if(ctx->ippControlAlgorithmComponents[i] && ctx->ippIscComponents[i]) {
            status = NvMediaIPPComponentAttach(ctx->ipp[i],                          //ippPipeline
                                      ctx->ippControlAlgorithmComponents[i],         //srcComponent,
                                      ctx->ippIscComponents[i],                      //dstComponent,
                                      NVMEDIA_IPP_PORT_SENSOR_CONTROL_1);            //portType

            if (status != NVMEDIA_STATUS_OK) {
               LOG_ERR("%s: NvMediaIPPComponentAttach failed for IPP %d, "
                    "component Control Algorithm -> ISC", __func__, i);
               goto failed;
            }
        }

        if(ctx->ippIspComponents[i] && ctx->ispMvFileWriter[i]) {
            status = NvMediaIPPComponentAttach(ctx->ipp[i],     //ippPipeline
                                      ctx->ippIspComponents[i], //srcComponent,
                                      ctx->ispMvFileWriter[i],  //dstComponent,
                                      NVMEDIA_IPP_PORT_IMAGE_2); //portType

            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: NvMediaIPPComponentAttach failed for IPP %d, "
                    "ISP MV -> File Writer", __func__, i);
                goto failed;
            }
        }
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

// Create Raw Pipeline
static NvMediaStatus
ipp_CreateRawPipeline(
    IPPTest*ctx)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;

    if(!ctx) {
        LOG_ERR("%s: Bad parameters \n", __func__);
        goto failed;
    }

    // Create Sensor Control Component
    status = ipp_CreateSensorControlComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Sensor Control Component \n", __func__);
        goto failed;
    }

    // Create File Reader component
    status = ipp_CreateFileReaderComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create File Reader \n", __func__);
        goto failed;
    }

    // Create ISP Component
    status = ipp_CreateISPComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create ISP Component \n", __func__);
        goto failed;
    }

    // Create Control Algorithm Component
    status = ipp_CreateControlAlgorithmComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Control algorithm Component \n", __func__);
        goto failed;
    }

    // Create File Writer Component
    status = ipp_CreateFileWriterComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Control algorithm Component \n", __func__);
        goto failed;
    }

    status = ipp_AttachComponent(ctx);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to build IPP components \n", __func__);
        goto failed;
    }

    return NVMEDIA_STATUS_OK;
failed:
    return NVMEDIA_STATUS_ERROR;
}

//
// This is the callback function to get the global time
//
static NvMediaStatus
ipp_GetAbsoluteGlobalTime(
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


static void
ipp_EventHandler( void *clientContext,
        NvMediaIPPComponentType componentType,
        NvMediaIPPComponent *ippComponent,
        NvMediaIPPEventType etype,
        NvMediaIPPEventData *edata)
{
    IPPTest *ctx = (IPPTest*) clientContext;
    switch(etype) {
    case NVMEDIA_IPP_EVENT_INFO_EOF:
        ctx->activeFileWriters--;
        if(!ctx->activeFileWriters) {
            LOG_INFO("%s: Finished processing %llu frames\n",
                __func__, edata->imageInformation.frameSequenceNumber + 1);
            ctx->quit = NVMEDIA_TRUE;
        }
        break;
    case NVMEDIA_IPP_EVENT_INFO_PROCESSING_DONE:
        if( componentType == NVMEDIA_IPP_COMPONENT_FILE_WRITER &&
            edata->imageInformation.cameraId == 0)
        {
            LOG_INFO("%s: Finish processing frame %llu\n",
                __func__, edata->imageInformation.frameSequenceNumber);
        }
        break;
    case NVMEDIA_IPP_EVENT_INFO_FRAME_CAPTURE:
    case NVMEDIA_IPP_EVENT_WARNING_CAPTURE_FRAME_DROP:
        break;
    case NVMEDIA_IPP_EVENT_ERROR_NO_RESOURCES:
    case NVMEDIA_IPP_EVENT_ERROR_INTERNAL_FAILURE:
    case NVMEDIA_IPP_EVENT_ERROR_BUFFER_PROCESSING_FAILURE:
        LOG_ERR("%s: error %u occurred\n", __func__, etype);
        ctx->quit = NVMEDIA_TRUE;
        break;
    default:
        break;
    }
}

NvMediaStatus
IPP_Init(
    IPPTest **ctx,
    TestArgs *testArgs)
{
    IPPTest *ctxTmp = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    CaptureConfigParams        *captureParams;
    NvU32 setId, i;
    ExtImgDevParam *extImgDevParam;

    if (!ctx || !testArgs) {
        LOG_ERR("%s: Bad parameter", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    // Create and initialize ipp context
    ctxTmp = calloc(1, sizeof(IPPTest));
    if (!ctxTmp) {
        LOG_ERR("%s: Out of memory", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    memset(ctxTmp->ippComponents, 0, sizeof(ctxTmp->ippComponents));
    memset(ctxTmp->ippIspComponents, 0, sizeof(ctxTmp->ippIspComponents));
    memset(ctxTmp->ippIscComponents, 0, sizeof(ctxTmp->ippIscComponents));
    memset(ctxTmp->ippControlAlgorithmComponents, 0, sizeof(ctxTmp->ippControlAlgorithmComponents));
    memset(ctxTmp->componentNum, 0, sizeof(ctxTmp->componentNum));

    ctxTmp->imagesNum = testArgs->imagesNum;
    if (ctxTmp->imagesNum > IPP_TEST_MAX_AGGREGATE_IMAGES) {
        LOG_WARN("Max aggregate images is: %u\n",
                 IPP_TEST_MAX_AGGREGATE_IMAGES);
        ctxTmp->imagesNum = IPP_TEST_MAX_AGGREGATE_IMAGES;
    }

    ctxTmp->quit = NVMEDIA_FALSE;

    ctxTmp->aggregateFlag = testArgs->useAggregationFlag;
    ctxTmp->ispOutType = testArgs->ispOutType;
    ctxTmp->mvSurfaceType = NvMediaSurfaceType_Image_Y16;
    ctxTmp->ispMvFlag = testArgs->ispMvFlag;
    ctxTmp->ispMvSurfaceType = testArgs->ispMvSurfaceType;
    ctxTmp->pluginFlag = testArgs->pluginFlag;
    ctxTmp->inputFileName = testArgs->inputFileName;
    ctxTmp->inputPixelOrder = testArgs->inputPixelOrder;

    setId = testArgs->configCaptureSetUsed;
    captureParams = &testArgs->captureConfigCollection[setId];
    LOG_DBG("%s: setId=%d,input resolution %s\n", __func__,
                setId, captureParams->resolution);
    if (sscanf(captureParams->resolution, "%ux%u",
        &ctxTmp->inputWidth,
        &ctxTmp->inputHeight) != 2) {
        LOG_ERR("%s: Invalid input resolution %s\n", __func__,
                captureParams->resolution);
        goto failed;
    }
    LOG_DBG("%s: inputWidth =%d,ctxTmp->inputHeight =%d\n", __func__,
                ctxTmp->inputWidth, ctxTmp->inputHeight);

    if (ctxTmp->aggregateFlag) {

        ctxTmp->inputWidth *= ctxTmp->imagesNum;
    }

    if (ctxTmp->aggregateFlag &&
       (ctxTmp->inputWidth % ctxTmp->imagesNum) != 0) {
        LOG_ERR("%s: Invalid number of siblings (%u) for input width: %u\n",
                __func__, ctxTmp->imagesNum, ctxTmp->inputWidth);
        goto failed;
    }

    if(IsFailed(ipp_SetRawImageProperties(ctxTmp, captureParams)))
        goto failed;

    extImgDevParam = &ctxTmp->extImgDevParam;
    /* Set ExtImgDev params */
    status = _SetExtImgDevParameters(ctxTmp,
                                     captureParams,
                                     extImgDevParam);

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: Failed to set ISC device parameters\n", __func__);
        goto failed;
    }

    /* Create ExtImgDev object */
    ctxTmp->extImgDevice = ExtImgDevInit(extImgDevParam);
    if (!ctxTmp->extImgDevice) {
        LOG_ERR("%s: Failed to initialize ISC devices\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    ctxTmp->ippNum = ctxTmp->imagesNum;

    ipp_GetOutputFileNames(ctxTmp, testArgs);

    for (i=0; i<ctxTmp->ippNum; i++) {

        ctxTmp->ispEnabled[i] = NVMEDIA_TRUE;
        ctxTmp->outputEnabled[i] = NVMEDIA_TRUE;
        ctxTmp->controlAlgorithmEnabled[i] = NVMEDIA_TRUE;
    }

    ctxTmp->device = NvMediaDeviceCreate();
    if(!ctxTmp->device) {
        LOG_ERR("%s: Failed to create NvMedia device\n", __func__);
        goto failed;
    }

    // Create IPPManager
    ctxTmp->ippManager = NvMediaIPPManagerCreate(NVMEDIA_IPP_VERSION_INFO, ctxTmp->device, NULL);
    if(!ctxTmp->ippManager) {
        LOG_ERR("%s: Failed to create ippManager\n", __func__);
        goto done;
    }

    // create IPPPipeline EventCallback
    if (NvMediaIPPManagerSetEventCallback(ctxTmp->ippManager, (void*)ctxTmp, ipp_EventHandler)) {
        LOG_ERR("%s : NvMediaIPPPipelineSetEventCallback failed\n", __func__);
        goto failed;
    }

    status = NvMediaIPPManagerSetTimeSource(ctxTmp->ippManager, NULL, ipp_GetAbsoluteGlobalTime);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set time source\n", __func__);
        goto failed;
    }

    // Create IPPs
    for (i=0; i<ctxTmp->ippNum; i++) {
        ctxTmp->ipp[i] = NvMediaIPPPipelineCreate(ctxTmp->ippManager);
        if(!ctxTmp->ipp[i]) {
            LOG_ERR("%s: Failed to create ipp %d\n", __func__, i);
            goto failed;
        }
    }

    // Build IPP components for each IPP pipeline
    memset(ctxTmp->ippComponents, 0, sizeof(ctxTmp->ippComponents));
    memset(ctxTmp->componentNum, 0, sizeof(ctxTmp->componentNum));

    status = ipp_CreateRawPipeline(ctxTmp);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create Raw Pipeline \n", __func__);
        goto failed;
    }

    *ctx = ctxTmp;
    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    IPP_Fini(ctxTmp);
    status = NVMEDIA_STATUS_ERROR;

done:
    return status;
}

NvMediaStatus
IPP_Start (
    IPPTest *ctx,
    TestArgs *testArgs)
{
    NvU32 i;

    if (ctx->extImgDevice) {

        ExtImgDevStart(ctx->extImgDevice);
    }

    // Start IPPs
    for (i=0; i<ctx->ippNum; i++) {
        if (IsFailed(NvMediaIPPPipelineStart(ctx->ipp[i]))) {      //ippPipeline
            LOG_ERR("%s: Failed starting pipeline %d\n", __func__, i);
            goto failed;
        }
    }
    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    IPP_Fini(ctx);

    return NVMEDIA_STATUS_ERROR;
}

NvMediaStatus
IPP_Stop (IPPTest *ctx)
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
IPP_Fini (IPPTest *ctx)
{
    ctx->quit = NVMEDIA_TRUE;

    NvMediaIPPManagerDestroy(ctx->ippManager);

    if (ctx->extImgDevice) {
        ExtImgDevStop(ctx->extImgDevice);
        ExtImgDevDeinit(ctx->extImgDevice);
    }

    if(ctx->device)
        NvMediaDeviceDestroy(ctx->device);

    free(ctx);
    return NVMEDIA_STATUS_OK;
}
