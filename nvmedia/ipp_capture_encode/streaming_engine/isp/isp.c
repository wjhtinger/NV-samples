/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "isp.h"
#include "stdlib.h"
#include "log_utils.h"
#include "string.h"

#define NUM_ISP_BUFFER_POOLS                    2
#define MAX_OUTPUT_PER_PIPE                     2
#define NUM_ALGORITHM_BUFFER_POOLS              1

#define MIN_BUFFERS_IN_ISP_POOL                 3
#define MIN_BUFFERS_IN_ALGORITHM_CONTROL_POOL   3

#define GET_FRAME_TIMEOUT                       500

// data structure to encapsulate the IPP ISP object
typedef struct {
    // reference to capture object
    NvMediaIPPComponent                         *pISP;

    // capture component configuration
    NvMediaIPPIspComponentConfig                config;

    // reference to algorithm control object
    NvMediaIPPComponent                         *pAlgorithmControl;

    // algorithm control configuration
    NvMediaIPPControlAlgorithmComponentConfig   algorithmControlConfig;

    // reference to the sensor control component attached
    NvMediaIPPComponent                         *pSensorControl;

    // sensor control configuration
    NvMediaIPPIscComponentConfig                sensorControlConfig;

    // the IPP pipeline to which the current instance
    // of capture is attached
    NvMediaIPPPipeline                          *pIPPPipeline;

    // the output component to which the ISP
    // output is directed
    NvMediaIPPComponent                         *pOutput[MAX_OUTPUT_PER_PIPE];

    // specifies the number of outputs enabled for the
    // instance of ISP
    NvU32                                       numOutputs;

    // specifies the number of buffers being used in ISP buffer pool
    NvU32                                       numOfBuffersInISPPool;

    // specifies the number of buffers being used in algorithm buffer pool
    NvU32                                       numOfBuffersInAlgorithmControlPool;

} ISPTop;

static void doHouseKeeping (ISPTop *pISPTop)
{
    NvU32   count;

    if (NULL == pISPTop) {
        // nothing to clean
        return;
    }

    if (NULL != pISPTop->pISP) {
        NvMediaIPPComponentDestroy(pISPTop->pISP);
    }

    for (count = 0; count < pISPTop->numOutputs; count++) {
        if (NULL != pISPTop->pOutput[count]) {
            NvMediaIPPComponentDestroy(pISPTop->pOutput[count]);
        }
    }

    if (NULL != pISPTop->pSensorControl) {
        NvMediaIPPComponentDestroy (pISPTop->pSensorControl);
    }

    if (NULL != pISPTop->pAlgorithmControl) {
        NvMediaIPPComponentDestroy (pISPTop->pAlgorithmControl);
    }

    free (pISPTop);

    return;
}

static NvMediaStatus
createAlgorithmAndSensorControl (
        ISPTop      *pISPTop,
        ISPParams   *pISPConfig)
{
    NvMediaIPPBufferPoolParams                  *pBufferPoolParams[NUM_ALGORITHM_BUFFER_POOLS + 1];
    NvMediaIPPBufferPoolParams                  bufferPool;
    NvMediaIPPControlAlgorithmComponentConfig   *pAlgorithmControlConfig;
    NvMediaIPPIscComponentConfig                *pSensorControlConfig;
    NvMediaStatus                               status;

    pAlgorithmControlConfig = &(pISPTop->algorithmControlConfig);
    pAlgorithmControlConfig->width = pISPConfig->width;
    pAlgorithmControlConfig->height = pISPConfig->height;
    pAlgorithmControlConfig->pixelOrder = pISPConfig->pixelOrder;
    pAlgorithmControlConfig->bitsPerPixel = pISPConfig->bitsPerPixel;
    pAlgorithmControlConfig->iscSensorDevice = pISPConfig->iscSensorDevice;
    pAlgorithmControlConfig->pluginFuncs = pISPConfig->pPluginFuncs;

    pSensorControlConfig = &(pISPTop->sensorControlConfig);
    pSensorControlConfig->iscRootDevice = pISPConfig->iscRootDevice;
    pSensorControlConfig->iscAggregatorDevice = pISPConfig->iscAggregatorDevice;
    pSensorControlConfig->iscSerializerDevice = pISPConfig->iscSerializerDevice;
    pSensorControlConfig->iscSensorDevice = pISPConfig->iscSensorDevice;

    memset(&bufferPool, 0, sizeof(NvMediaIPPBufferPoolParams));
    bufferPool.portType = NVMEDIA_IPP_PORT_SENSOR_CONTROL_1;
    bufferPool.poolBuffersNum = pISPTop->numOfBuffersInAlgorithmControlPool;
    pBufferPoolParams[0] = &bufferPool;
    pBufferPoolParams[1] = NULL;

    LOG_DBG("%s: creating algorithm control component\n",__func__);
    pISPTop->pAlgorithmControl =
            NvMediaIPPComponentCreate(pISPTop->pIPPPipeline,                    //ippPipeline
                                      NVMEDIA_IPP_COMPONENT_CONTROL_ALGORITHM,  //componentType
                                      pBufferPoolParams,                        //bufferPools
                                      pAlgorithmControlConfig);                 //componentConfig

    if (NULL == pISPTop->pAlgorithmControl) {
        LOG_ERR("%s: failed to allocate IPP algorithm control component\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    // create the sensor control components
    LOG_DBG("%s: creating sensor control component\n",__func__);
    pISPTop->pSensorControl =
            NvMediaIPPComponentCreate(pISPTop->pIPPPipeline,                //ippPipeline
                                      NVMEDIA_IPP_COMPONENT_SENSOR_CONTROL, //componentType
                                      NULL,                                 //bufferPools
                                      pSensorControlConfig);                //componentConfig

    if (NULL == pISPTop->pSensorControl) {
        LOG_ERR("%s: failed to allocate IPP sensor control component\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    LOG_DBG("%s: attaching algorithm to sensor control component\n",__func__);
    // join the algorithm control and sensor control objects
    status = NvMediaIPPComponentAttach(pISPTop->pIPPPipeline,               //ippPipeline
                                       pISPTop->pAlgorithmControl,          //srcComponent,
                                       pISPTop->pSensorControl,             //dstComponent,
                                       NVMEDIA_IPP_PORT_SENSOR_CONTROL_1);  //portType

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: failed to attach algorithm and sensor control components\n", __func__);
    }

    LOG_DBG("%s: attaching ISP to algorithm component\n",__func__);
    // join the ISP and algorithm control objects
    status = NvMediaIPPComponentAttach(pISPTop->pIPPPipeline,       //ippPipeline
                                       pISPTop->pISP,               //srcComponent,
                                       pISPTop->pAlgorithmControl,  //dstComponent,
                                       NVMEDIA_IPP_PORT_STATS_1);   //portType

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: failed to attach algorithm and ISP components\n", __func__);
    }

    return status;
}

static void setBufferPoolConfig(
        NvMediaIPPBufferPoolParams  *pBufferPool,
        ISPTop                      *pISPTop,
        ISPParams                   *pISPConfig)
{

    memset((void *)(pBufferPool), 0, sizeof(NvMediaIPPBufferPoolParams));

    pBufferPool->portType           = NVMEDIA_IPP_PORT_IMAGE_1;
    pBufferPool->poolBuffersNum     = pISPTop->numOfBuffersInISPPool;
    pBufferPool->width              = pISPConfig->width;
    pBufferPool->height             = pISPConfig->height;
    pBufferPool->surfaceType        = NvMediaSurfaceType_Image_YUV_420;
    pBufferPool->surfAttributes     = NVMEDIA_IMAGE_ATTRIBUTE_SEMI_PLANAR |
                                      NVMEDIA_IMAGE_ATTRIBUTE_UNMAPPED;
    pBufferPool->imageClass         = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
    pBufferPool->imagesCount        = 1;
    pBufferPool->createSiblingsFlag = NVMEDIA_FALSE;
    pBufferPool->surfAttributes    |= NVMEDIA_IMAGE_ATTRIBUTE_DISPLAY;

    return;
}

static NvMediaStatus ispComponentCreate(
        ISPTop              *pISPTop,
        ISPParams           *pISPConfig,
        NvMediaIPPComponent *pImageDataInput,
        NvU32               pipeNum)
{
    NvMediaIPPBufferPoolParams      *pBufferPoolParams[NUM_ISP_BUFFER_POOLS + 1];
    NvMediaIPPBufferPoolParams      bufferPool, bufferPoolStats;
    NvMediaIPPIspComponentConfig    *pIPPIspConfig;
    NvU32                           poolIndex;
    NvMediaStatus                   status;
    NvU32                           count;

    pIPPIspConfig = &(pISPTop->config);
    pIPPIspConfig->ispSettingsFile = NULL;
    pIPPIspConfig->ispSettingAttr = 0;
    pIPPIspConfig->ispSelect = pISPConfig->ispSelect;

    poolIndex = 0;

    // at least one output port is enabled
    setBufferPoolConfig(&bufferPool, pISPTop, pISPConfig);
    pBufferPoolParams[poolIndex] = &bufferPool;
    poolIndex += 1;

    if (NVMEDIA_TRUE == pISPConfig->enableProcessing) {
        bufferPoolStats.portType = NVMEDIA_IPP_PORT_STATS_1;
        bufferPoolStats.poolBuffersNum = pISPTop->numOfBuffersInISPPool;
        pBufferPoolParams[poolIndex] = &bufferPoolStats;
        poolIndex += 1;
    }

    pBufferPoolParams[poolIndex] = NULL;

    LOG_DBG("%s: creating ISP %d\n",__func__, pipeNum);

    pISPTop->pISP =
            NvMediaIPPComponentCreate(pISPTop->pIPPPipeline,          //ippPipeline
                                      NVMEDIA_IPP_COMPONENT_ISP,    //componentType
                                      pBufferPoolParams,            //bufferPools
                                      &pISPTop->config);             //componentConfig

    if (NULL == pISPTop->pISP) {
        LOG_ERR("%s: failed to allocate IPP ISP component\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    // if ISP stats processing has been enabled, then
    // create the algorithm control component
    if (NVMEDIA_TRUE == pISPConfig->enableProcessing) {
        status = createAlgorithmAndSensorControl (pISPTop, pISPConfig);

        if (NVMEDIA_STATUS_OK != status) {
            return NVMEDIA_STATUS_ERROR;
        }
    }

    // create an output component to extract the stream
    for (count = 0; count < pISPConfig->numOutputs; count++) {
        LOG_DBG("%s: creating output %d\n",
                __func__,
                count);

        pISPTop->pOutput[count] =
                NvMediaIPPComponentCreate(pISPTop->pIPPPipeline,          //ippPipeline
                                          NVMEDIA_IPP_COMPONENT_OUTPUT, //componentType
                                          NULL,                         //bufferPools
                                          NULL);                        //componentConfig

        if (NULL == pISPTop->pOutput[count]) {
            LOG_ERR("%s: Failed to create output for ISP\n",
                    __func__);

            return NVMEDIA_STATUS_ERROR;
        }

        LOG_DBG("%s: attaching ISP to output %d\n",
                __func__,
                count);

        status = NvMediaIPPComponentAttach(pISPTop->pIPPPipeline,       //ippPipeline
                                           pISPTop->pISP,               //srcComponent,
                                           pISPTop->pOutput[count],     //dstComponent,
                                           NVMEDIA_IPP_PORT_IMAGE_1);   //portType

        if (NVMEDIA_STATUS_OK != status) {
            LOG_ERR("%s: Failed to mount output\n",
                    __func__);

            return NVMEDIA_STATUS_ERROR;
        }
    }

    pISPTop->numOutputs = pISPConfig->numOutputs;

    // connect input data to ISP
    // for the ISP instance corresponding to the
    // first stream, connect the input natively (NvMediaIPPComponentAttach)
    // for the rest, connect using NvMediaIPPComponentAddToPipeline
    if (0 != pipeNum) {
        status = NvMediaIPPComponentAddToPipeline(pISPTop->pIPPPipeline,    //ippPipeline
                                                  pImageDataInput);         //ippComponent
    }

    status = NvMediaIPPComponentAttach(pISPTop->pIPPPipeline,       //ippPipeline
                                       pImageDataInput,             //srcComponent,
                                       pISPTop->pISP,               //dstComponent,
                                       NVMEDIA_IPP_PORT_IMAGE_1);   //portType

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: failed to mount ISP input %d\n",
                __func__);

        return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}

void * ISPCreate(
        NvMediaIPPPipeline  *pIPPPipeline,
        ISPParams           *pISPConfig,
        NvMediaIPPComponent *pImageDataInput,
        NvU32               pipeNum)
{
    ISPTop          *pISPTop;
    NvMediaStatus   status;

    // sanity checks
    if ((NULL == pISPConfig)     ||
        (NULL == pIPPPipeline)   ||
        (NULL == pImageDataInput)||
        (pISPConfig->numOutputs > MAX_OUTPUT_PER_PIPE)) {

        LOG_ERR("%s: invalid ISPCreate arguments\n", __func__);
        return NULL;
    }

    // at least one output must be enabled
    if (0 == pISPConfig->numOutputs) {
        LOG_ERR("%s: at least one output port must be enabled\n", __func__);
        return NULL;
    }

    pISPTop = (ISPTop *)calloc(1, sizeof(ISPTop));

    if (NULL == pISPTop) {
        LOG_ERR("%s: failed to allocate IPP ISP component\n", __func__);
        return NULL;
    }

    pISPTop->pIPPPipeline = pIPPPipeline;

    if (pISPConfig->numOfBuffersInISPPool < MIN_BUFFERS_IN_ISP_POOL) {
        pISPTop->numOfBuffersInISPPool = MIN_BUFFERS_IN_ISP_POOL;

        LOG_WARN("%s: %d buffers for ISP pool is too less, using %d buffers\n",
                __func__,
                pISPConfig->numOfBuffersInISPPool,
                MIN_BUFFERS_IN_ISP_POOL);
    } else {
        pISPTop->numOfBuffersInISPPool = pISPConfig->numOfBuffersInISPPool;
    }

    if (pISPConfig->numOfBuffersInAlgorithmControlPool < MIN_BUFFERS_IN_ALGORITHM_CONTROL_POOL) {
        pISPTop->numOfBuffersInAlgorithmControlPool = MIN_BUFFERS_IN_ALGORITHM_CONTROL_POOL;

        LOG_WARN("%s: %d buffers for algorithm control pool is too less, using %d buffers\n",
                __func__,
                pISPConfig->numOfBuffersInAlgorithmControlPool,
                MIN_BUFFERS_IN_ALGORITHM_CONTROL_POOL);
    } else {
        pISPTop->numOfBuffersInAlgorithmControlPool = pISPConfig->numOfBuffersInAlgorithmControlPool;
    }

    status = ispComponentCreate(pISPTop,
                                pISPConfig,
                                pImageDataInput,
                                pipeNum);

    if (NVMEDIA_STATUS_OK == status) {
        return (void *)pISPTop;
    } else {
        doHouseKeeping(pISPTop);
        return NULL;
    }
}

NvMediaStatus ISPGetOutput (
        void                        *pISP,
        NvMediaIPPComponentOutput   *pOutput,
        NvU32                       outputNum)
{
    ISPTop                  *pispTop;
    NvMediaStatus           status;
    NvMediaIPPComponent     *pOutputComponent;

    if ((NULL == pISP) ||
        (outputNum > MAX_OUTPUT_PER_PIPE)) {
        LOG_ERR("%s: invalid ISP get output inputs\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pispTop = (ISPTop *)(pISP);
    pOutputComponent = pispTop->pOutput[outputNum];

    if (NULL == pOutputComponent) {
        LOG_ERR("%s: ISP %p not setup to generate output %d\n",
                __func__,
                pispTop,
                outputNum);
        return NVMEDIA_STATUS_ERROR;
    }

    status = NvMediaIPPComponentGetOutput(pOutputComponent,     //component
                                          GET_FRAME_TIMEOUT,    //millisecondTimeout,
                                          pOutput);             //output image

    if (NVMEDIA_STATUS_OK != status) {
        LOG_DBG("%s: failed to obtain output frame from ISP\n", __func__);
    }

    return status;
}

NvMediaStatus ISPPutOutput (
        void                        *pISP,
        NvMediaIPPComponentOutput   *pOutput,
        NvU32                       outputNum)
{
    ISPTop                  *pispTop;
    NvMediaStatus           status;
    NvMediaIPPComponent     *pOutputComponent;

    if (NULL == pISP) {
        LOG_ERR("%s: invalid ISP get output inputs\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pispTop = (ISPTop *)(pISP);
    pOutputComponent = pispTop->pOutput[outputNum];

    if (NULL == pOutputComponent) {
        LOG_ERR("%s: ISP not setup to generate output\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    status = NvMediaIPPComponentReturnOutput(pOutputComponent,  //component
                                             pOutput);          //output image

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: failed to return output frame to ISP, status = %d\n", __func__, status);
    }

    return status;
}

void ISPDelete(void *pISP)
{
    ISPTop *pISPTop;
    pISPTop = (ISPTop *)pISP;

    doHouseKeeping(pISPTop);

    return;
}
