/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <string.h>
#include <stdlib.h>
#include "capture.h"
#include "log_utils.h"

#define NUM_CAPTURE_BUFFER_POOLS                    4
#define MIN_NUM_BUFFERS_IN_CAPTURE_BUFFER_POOL      3
#define GET_FRAME_TIMEOUT                           500

// data structure to encapsulate the capture object
typedef struct {
    // reference to capture object
    NvMediaIPPComponent             *capture;

    // the IPP pipeline to which the current instance
    // of capture is attached
    NvMediaIPPPipeline              *ippPipeline;

    // the output component will be used when
    // the capture output needs to be tapped directly
    NvMediaIPPComponent             *output;

    // capture component configuration
    NvMediaIPPIcpComponentConfig    config;

    // ICP settings for the capture
    NvMediaICPSettings              ICPsettings;

    // ICP settings to be used for streaming
    // with virtual channel
    NvMediaICPSettingsEx            ICPSettingsEx;

    // configure the number of internal buffers to be allocated
    // for capture buffer pool
    NvU32                           numOfBuffersInPool;

    // specifies the number of streams (either aggregated)
    // or using virtual channels
    NvU32                           streamCount;

    // specifies whether to use virtual channels for
    // aggregated images
    NvMediaBool                     useVirtualChannels;

} CaptureTop;

static void doHouseKeeping (CaptureTop *pCaptureTop)
{
    if (NULL == pCaptureTop) {
        // nothing to clean
        return;
    }

    if (NULL != pCaptureTop->output) {
        NvMediaIPPComponentDestroy (pCaptureTop->output);
        pCaptureTop->output = NULL;
    }

    if (NULL != pCaptureTop->capture) {
        NvMediaIPPComponentDestroy (pCaptureTop->capture);
        pCaptureTop->capture = NULL;
    }

    free (pCaptureTop);

    return;
}

static NvMediaStatus captureComponentCreate(
        CaptureTop  *pCaptureTop,
        NvU32       embeddedDataLinesTop,
        NvU32       embeddedDataLinesBottom,
        NvBool      enableOuptut)
{
    NvMediaIPPBufferPoolParams  *pBufferPoolParams[NUM_CAPTURE_BUFFER_POOLS + 1], bufferPool;
    NvMediaICPSettings          *pICPSettings;
    NvU32                       streamCount;
    NvU32                       count;
    NvMediaStatus               status;

    pICPSettings = pCaptureTop->config.settings;

    pBufferPoolParams[0] = &bufferPool;
    pBufferPoolParams[1] = NULL;

    // prepare buffer pool configuration
    memset((void *)(&bufferPool), 0, sizeof(NvMediaIPPBufferPoolParams));

    // capture component can only have output of NVMEDIA_IPP_PORT_IMAGE_1
    bufferPool.portType = NVMEDIA_IPP_PORT_IMAGE_1;
    bufferPool.poolBuffersNum = pCaptureTop->numOfBuffersInPool;

    if (0 == pCaptureTop->config.siblingsNum) {
        // siblingsNum == 0 ==> no aggregation
        streamCount = 1;

        // Single image processing case
        bufferPool.imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
        bufferPool.createSiblingsFlag = NVMEDIA_FALSE;
        bufferPool.siblingAttributes = 0;
    } else {
        streamCount = pCaptureTop->config.siblingsNum;
        bufferPool.imageClass = NVMEDIA_IMAGE_CLASS_AGGREGATE_IMAGES;
        bufferPool.createSiblingsFlag = NVMEDIA_TRUE;
        bufferPool.siblingAttributes = NVMEDIA_IMAGE_ATTRIBUTE_SIBLING_USE_OFFSET;
    }

    // if no aggregation, then pipeCount == 1 i.e. single pipe
    // NvMediaICPSettings.width holds the aggregated width
    bufferPool.width = pICPSettings->width/streamCount;
    bufferPool.height = pICPSettings->height;

    // currently only RAW capture is supported
    bufferPool.surfaceType = NvMediaSurfaceType_Image_RAW;

    if (NvMediaSurfaceType_Image_RAW == bufferPool.surfaceType) {
        bufferPool.surfAttributes =
                NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL  |
                NVMEDIA_IMAGE_ATTRIBUTE_RAW_PIXEL_ORDER;
    }

    bufferPool.surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_EXTRA_LINES;
    bufferPool.surfAdvConfig.embeddedDataLinesTop = embeddedDataLinesTop;
    bufferPool.surfAdvConfig.embeddedDataLinesBottom = embeddedDataLinesBottom;
    bufferPool.surfAdvConfig.extraMetaDataSize = 0;
    bufferPool.surfAdvConfig.bitsPerPixel = pICPSettings->surfaceFormat.bitsPerPixel;
    bufferPool.surfAdvConfig.pixelOrder = pICPSettings->surfaceFormat.pixelOrder;
    bufferPool.imagesCount = streamCount;

    /*flag to allocate capture surfaces*/
    bufferPool.surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_CAPTURE;

    if (NVMEDIA_TRUE == pCaptureTop->useVirtualChannels) {
        pCaptureTop->ICPSettingsEx.interfaceType = pICPSettings->interfaceType;
        pCaptureTop->ICPSettingsEx.interfaceLanes = pICPSettings->interfaceLanes;
        pCaptureTop->ICPSettingsEx.numVirtualChannels = pCaptureTop->streamCount;

        for (count = 0; count < pCaptureTop->streamCount; count++) {
            pCaptureTop->ICPSettingsEx.settings[count].virtualChannelIndex = count;
            memcpy(&pCaptureTop->ICPSettingsEx.settings[count].icpSettings,
                   pICPSettings,
                   sizeof(NvMediaICPSettings));
            pBufferPoolParams[count] = &bufferPool;
        }

        pBufferPoolParams[pCaptureTop->streamCount] = NULL;

        pCaptureTop->capture =
                NvMediaIPPComponentCreate(pCaptureTop->ippPipeline,         //ippPipeline
                                          NVMEDIA_IPP_COMPONENT_CAPTURE_EX, //componentType
                                          pBufferPoolParams,                //bufferPools
                                          &pCaptureTop->ICPSettingsEx);     //componentConfig
    } else {
        pCaptureTop->capture =
                NvMediaIPPComponentCreate(pCaptureTop->ippPipeline,         //ippPipeline
                                          NVMEDIA_IPP_COMPONENT_CAPTURE,    //componentType
                                          pBufferPoolParams,                //bufferPools
                                          &pCaptureTop->config);            //componentConfig
    }

    if (NULL == pCaptureTop->capture) {
        LOG_ERR("%s: failed to allocate IPP ICP component\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    if (NVMEDIA_TRUE == enableOuptut) {
        // create an output component
        pCaptureTop->output =
        NvMediaIPPComponentCreate(pCaptureTop->ippPipeline,     //ippPipeline
                                  NVMEDIA_IPP_COMPONENT_OUTPUT, //componentType
                                  NULL,                         //bufferPools
                                  NULL);                        //componentConfig

        if (NULL == pCaptureTop->output) {
                LOG_ERR("%s: failed to allocate capture output component\n", __func__);
                return NVMEDIA_STATUS_ERROR;
        }

        // connect the capture component to output component
        status = NvMediaIPPComponentAttach(pCaptureTop->ippPipeline,     //ippPipeline
                                           pCaptureTop->capture,         //srcComponent,
                                           pCaptureTop->output,          //dstComponent,
                                           NVMEDIA_IPP_PORT_IMAGE_1);   //portType

        if (NVMEDIA_STATUS_OK != status) {
                LOG_ERR("%s: failed to mount output on capture\n", __func__);
                return NVMEDIA_STATUS_ERROR;
        }
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
captureSetConfig(
        CaptureTop      *pCaptureTop,
        CaptureParams   *pCaptureConfig)
{
    NvMediaIPPIcpComponentConfig *pIPPIcpConfig;
    NvMediaICPSettings           *pICPsettings;
    // sanity checks
    if ((NULL == pCaptureTop) ||
        (NULL == pCaptureConfig)) {
        LOG_ERR("%s: invalid capture config input arguments\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if (NVMEDIA_IMAGE_CAPTURE_SURFACE_FORMAT_TYPE_RAW !=
            pCaptureConfig->outputSurfaceFormatType) {
        // TODO: we will support other format types too eventually
        LOG_ERR("%s: only RAW streaming is supported currently\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pIPPIcpConfig = &(pCaptureTop->config);
    pIPPIcpConfig->settings = &pCaptureTop->ICPsettings;
    pICPsettings = &pCaptureTop->ICPsettings;

    pCaptureTop->useVirtualChannels = pCaptureConfig->useVirtualChannels;
    pCaptureTop->streamCount = pCaptureConfig->streamCount;

    // currently only CSI physical interface is supported
    pIPPIcpConfig->interfaceFormat = NVMEDIA_IMAGE_CAPTURE_INTERFACE_FORMAT_CSI;

    if ((NVMEDIA_FALSE == pCaptureTop->useVirtualChannels) && (pCaptureConfig->streamCount > 1)) {
        pIPPIcpConfig->siblingsNum = pCaptureConfig->streamCount;
    } else {
        pIPPIcpConfig->siblingsNum = 0;
    }

    pICPsettings->interfaceType = pCaptureConfig->inputInterfaceType;

    pICPsettings->inputFormat.inputFormatType = pCaptureConfig->inputFormatType;
    pICPsettings->inputFormat.bitsPerPixel = pCaptureConfig->inputBitsPerPixel;
    pICPsettings->inputFormat.pixelOrder = pCaptureConfig->inputPixelOrder;

    pICPsettings->surfaceFormat.surfaceFormatType = pCaptureConfig->outputSurfaceFormatType;
    pICPsettings->surfaceFormat.bitsPerPixel = pCaptureConfig->outputBitsPerPixel;
    pICPsettings->surfaceFormat.pixelOrder = pCaptureConfig->outputPixelOrder;

    if (0 == pIPPIcpConfig->siblingsNum) {
        pICPsettings->width = pCaptureConfig->width;
    } else {
        pICPsettings->width = pCaptureConfig->width * pCaptureConfig->streamCount;
    }

    pICPsettings->height = pCaptureConfig->height;
    pICPsettings->startX = pCaptureConfig->startX;
    pICPsettings->startY = pCaptureConfig->startY;
    pICPsettings->embeddedDataLines = pCaptureConfig->embeddedDataLinesTop +
                                      pCaptureConfig->embeddedDataLinesBottom;

    pICPsettings->interfaceLanes = pCaptureConfig->interfaceLanesCount;
    pICPsettings->pixelFrequency = pCaptureConfig->pixelFrequency_Hz;
    pICPsettings->thsSettle = pCaptureConfig->thsSettle;

    if (pCaptureConfig->numOfBuffersInPool < MIN_NUM_BUFFERS_IN_CAPTURE_BUFFER_POOL) {
        pCaptureTop->numOfBuffersInPool = MIN_NUM_BUFFERS_IN_CAPTURE_BUFFER_POOL;
    }
    else {
        pCaptureTop->numOfBuffersInPool = pCaptureConfig->numOfBuffersInPool;
    }

    return NVMEDIA_STATUS_OK;
}

void * CaptureCreate(
        NvMediaIPPPipeline  *pIppPipeline,
        CaptureParams       *pCptureConfig)
{
    CaptureTop      *pCaptureTop = NULL;
    NvMediaStatus   status;

    // sanity checks
    if ((NULL == pIppPipeline) ||
        (NULL == pCptureConfig)) {
        LOG_ERR("%s: invalid capture create inputs\n", __func__);
        return NULL;
    }

    // create an instance of the capture object
    pCaptureTop = (CaptureTop *)calloc(1, sizeof(CaptureTop));

    if (NULL == pCaptureTop) {
        LOG_ERR("%s: failed to allocate capture object\n", __func__);
        return NULL;
    }

    // maintain a reference to the pipeline to which the
    // current instance of capture belongs
    pCaptureTop->ippPipeline = pIppPipeline;

    status = captureSetConfig(pCaptureTop, pCptureConfig);

    if (NVMEDIA_STATUS_OK != status) {
        doHouseKeeping(pCaptureTop);
        return NULL;
    }

    status = captureComponentCreate(pCaptureTop,
                                    pCptureConfig->embeddedDataLinesTop,
                                    pCptureConfig->embeddedDataLinesBottom,
                                    pCptureConfig->enableOuptut);

    if (NVMEDIA_STATUS_OK != status) {
        doHouseKeeping(pCaptureTop);
        return NULL;
    }

    return (void *)pCaptureTop;
}

NvMediaStatus CaptureGetOutput (
        void                        *pCapture,
        NvMediaIPPComponentOutput   *pOutput)
{
    CaptureTop      *pCaptureTop;
    NvMediaStatus   status;

    if (NULL == pCapture) {
        LOG_ERR("%s: invalid capture get output inputs\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pCaptureTop = (CaptureTop *)(pCapture);

    if (NULL == pCaptureTop->output) {
        LOG_ERR("%s: capture not setup to generate output\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    status = NvMediaIPPComponentGetOutput(pCaptureTop->output,  //component
                                          GET_FRAME_TIMEOUT,    //millisecondTimeout,
                                          pOutput);             //output image

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: failed to obtain output frame from capture \n", __func__);
    }

    return status;
}

NvMediaStatus CapturePutOutput (
        void                        *pCapture,
        NvMediaIPPComponentOutput   *pOutput)
{

    CaptureTop      *pCaptureTop;
    NvMediaStatus   status;

    if (NULL == pCapture) {
        LOG_ERR("%s: invalid inputs\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pCaptureTop = (CaptureTop *)pCapture;

    if (NULL == pCaptureTop->output) {
        LOG_ERR("%s: capture not setup for output dump\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    status = NvMediaIPPComponentReturnOutput(pCaptureTop->output,   //component
                                             pOutput);              //output image

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: failed to return frame to capture\n", __func__);
    }

    return status;
}

NvMediaIPPComponent * CaptureGetIPPComponent(
        void  *pCapture)
{
    CaptureTop *pCaptureTop;

    if (NULL == pCapture) {
        return NULL;
    }

    pCaptureTop = (CaptureTop *)pCapture;

    return pCaptureTop->capture;
}

void CaptureDelete(void *pCapture)
{
    CaptureTop *pCaptureTop;

    pCaptureTop = (CaptureTop *)(pCapture);

    doHouseKeeping(pCaptureTop);

    return;
}
