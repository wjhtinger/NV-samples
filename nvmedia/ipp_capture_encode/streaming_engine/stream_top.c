/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include "stream_top.h"
#include "capture.h"
#include "isp.h"
#include "log_utils.h"
#include "string.h"


#define NUM_BUFFERS_IN_ISP_BUFFER_POOL          10
#define NUM_BUFFERS_IN_ALGORITHM_BUFFER_POOL    10

typedef struct {
    // reference to IPP pipeline
    NvMediaIPPPipeline  *ipp;

    // reference to opaque capture component of the pipe
    void                *capture;

    // reference to opaque ISP component of the pipe
    void                *isp;

} PipeTop;

// Top level glue data structure to bind capture-process and encoder blocks
typedef struct {

    // reference to NvMediaDevice
    NvMediaDevice           *device;

    // reference to top level ipp manager
    NvMediaIPPManager       *ippManager;

    // pipes
    PipeTop                 pipes[MAX_NUM_OF_PIPES];

    NvMediaIPPPluginFuncs   *pPluginFuncs;

    // number of pipes streams created
    NvU32                   pipeCount;

    // flag to indicate if ISP is enabled
    // ISP will be used only if the RAW
    // image is captured from the sensor
    NvMediaBool             ispEnabled;

} CaptureAndProcessTop;

static void doHouseKeeping (CaptureAndProcessTop *pCaptureAndProcess)
{
    NvU32   pipeIndex;
    PipeTop *pipe;

    if (NULL == pCaptureAndProcess) {
        // nothing to clean
        return;
    }

    // loop through all the pipes and delete them
    for (pipeIndex = 0; pipeIndex < pCaptureAndProcess->pipeCount; pipeIndex += 1) {
        pipe = &(pCaptureAndProcess->pipes[pipeIndex]);

        if (NULL != pipe) {
            // delete capture only for pipe 0, others are just pointers
            // to pipe 0 capture instance
            if (0 == pipeIndex) {
                CaptureDelete(pipe->capture);
            }

            // delete ISP
            if (NULL != pipe->isp) {
                ISPDelete(pipe->isp);
            }

            // delete the IPP pipeline object
            if (NULL != pipe->ipp) {
                NvMediaIPPPipelineDestroy(pipe->ipp);
            }
        }
    }

    // delete the PluginFuncs
    if (NULL != pCaptureAndProcess->pPluginFuncs) {
        free(pCaptureAndProcess->pPluginFuncs);
    }

    // delete the IPP manager
    NvMediaIPPManagerDestroy(pCaptureAndProcess->ippManager);

    // delete the NvMedia device
    if(pCaptureAndProcess->device) {
        NvMediaDeviceDestroy(pCaptureAndProcess->device);
    }

    // free the top level object
    free (pCaptureAndProcess);

    return;
}

static NvMediaStatus
CreatePipes (
        CaptureAndProcessTop    *pCaptureAndProcess,
        StreamConfigParams      *pCaptureSS)
{
    PipeTop                 *pipe;
    NvU32                   pipeIndex;
    ISPParams               ispParams;
    NvMediaIPPComponent     *pCaptureIPP;
    void                    *pCapture = NULL;

    // setup isp params for capture and encode
    ispParams.numOfBuffersInISPPool                 = NUM_BUFFERS_IN_ISP_BUFFER_POOL;
    ispParams.width                                 = pCaptureSS->captureParams.width;
    ispParams.height                                = pCaptureSS->captureParams.height;
    ispParams.pixelOrder                            = pCaptureSS->captureParams.outputPixelOrder;
    ispParams.bitsPerPixel                          = pCaptureSS->captureParams.outputBitsPerPixel;
    ispParams.iscRootDevice                         = pCaptureSS->extImgDevice->iscRoot;
    ispParams.iscAggregatorDevice                   = pCaptureSS->extImgDevice->iscDeserializer;
    ispParams.numOfBuffersInAlgorithmControlPool    = NUM_BUFFERS_IN_ALGORITHM_BUFFER_POOL;
    ispParams.pPluginFuncs                          = pCaptureAndProcess->pPluginFuncs;

    // create 'streamCount' streaming pipes
    for (pipeIndex = 0; pipeIndex < pCaptureSS->streamCount; pipeIndex++) {
        pipe = &(pCaptureAndProcess->pipes[pipeIndex]);
        // create IPP pipeline
        pipe->ipp = NvMediaIPPPipelineCreate(pCaptureAndProcess->ippManager);

        if (NULL == pipe->ipp) {
            LOG_ERR("%s: Failed to create IPP pipeline for pipe %d\n",
                    __func__,
                    pipeIndex);

            return NVMEDIA_STATUS_ERROR;
        }

        // create capture component only for pipe0
        // for other pipes, simply propagate the
        // same capture reference as in pipe0
        if (0 == pipeIndex) {
            LOG_DBG("%s: creating capture for pipe %d\n",
                    __func__,
                    pipeIndex);
            pipe->capture = CaptureCreate(pipe->ipp, &(pCaptureSS->captureParams));
            pCapture = pipe->capture;
        }
        else {
            LOG_DBG("%s: reusing capture for pipe %d\n",
                    __func__,
                    pipeIndex);
            pipe->capture = pCapture;
        }

        if (NULL == pipe->capture) {
            LOG_ERR("%s: Failed to create capture component for for pipe %d\n",
                    __func__,
                    pipeIndex);

            return NVMEDIA_STATUS_ERROR;
        }

        pCaptureIPP                 = CaptureGetIPPComponent(pipe->capture);
        ispParams.enableProcessing  = pCaptureSS->enablePipeOutputs[pipeIndex].enableProcessing;
        ispParams.numOutputs        = pCaptureSS->enablePipeOutputs[pipeIndex].numOutputs;

        // create ISP component
        if (pipeIndex < 2) {
            ispParams.ispSelect = NVMEDIA_ISP_SELECT_ISP_A;
        } else {
            ispParams.ispSelect = NVMEDIA_ISP_SELECT_ISP_B;
        }

        ispParams.iscSerializerDevice = pCaptureSS->extImgDevice->iscBroadcastSerializer;
        ispParams.iscSensorDevice = pCaptureSS->extImgDevice->iscSensor[pipeIndex];

        // create the ISP only if it is needed
        if (NVMEDIA_TRUE == pCaptureAndProcess->ispEnabled) {
            LOG_DBG("%s: creating ISPTop for pipe %d\n",
                    __func__,
                    pipeIndex);

            pipe->isp = ISPCreate(pipe->ipp,
                                  &ispParams,
                                  pCaptureIPP,
                                  pipeIndex);
        } else {
            pipe->isp = NULL;
        }

        if ((NVMEDIA_TRUE == pCaptureAndProcess->ispEnabled) &&
            (NULL == pipe->isp)) {
            LOG_ERR("%s: Failed to create capture component for for pipe %d\n",
                    __func__,
                    pipeIndex);

            return NVMEDIA_STATUS_ERROR;
        }
    }

    return NVMEDIA_STATUS_OK;
}

void
*CaptureAndProcessCreate(StreamConfigParams *pCaptureSS)
{
    CaptureAndProcessTop    *pCaptureAndProcess;
    NvMediaStatus           status;

    // sanity check
    if ((NULL == pCaptureSS) ||
        (NULL == pCaptureSS->extImgDevice) ||
        (NULL == pCaptureSS->camMap)) {
        LOG_ERR("%s: invalid arguments\n", __func__);
        return NULL;
    }

    if (pCaptureSS->streamCount > MAX_NUM_OF_PIPES) {
        LOG_ERR("%s: stream count cannot be greater than %d\n", __func__, MAX_NUM_OF_PIPES);
        return NULL;
    }

    pCaptureAndProcess = (CaptureAndProcessTop *)calloc(1, sizeof(CaptureAndProcessTop));

    if (NULL == pCaptureAndProcess) {
        LOG_ERR("%s: failed to allocate capture process object\n", __func__);
        return NULL;
    }

    pCaptureAndProcess->pipeCount = pCaptureSS->streamCount;

    if (NULL != pCaptureSS->pPluginFuncs) {
        pCaptureAndProcess->pPluginFuncs = (NvMediaIPPPluginFuncs *)calloc(1, sizeof(NvMediaIPPPluginFuncs));

        if (NULL == pCaptureAndProcess->pPluginFuncs) {
            LOG_ERR("%s: failed to allocate plugin functions object\n", __func__);
            doHouseKeeping(pCaptureAndProcess);
            return NULL;
        }

        memcpy((void *)pCaptureAndProcess->pPluginFuncs,
               (void *)pCaptureSS->pPluginFuncs,
               sizeof(NvMediaIPPPluginFuncs));
    } else {
        pCaptureAndProcess->pPluginFuncs = NULL;
    }

    LOG_DBG("%s: number of streams = %d\n",
            __func__,
            pCaptureAndProcess->pipeCount);

    // decide if it is required to use the ISP
    if (NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_RAW == pCaptureSS->captureParams.inputFormatType) {
        pCaptureAndProcess->ispEnabled = NVMEDIA_TRUE;
    } else {
        pCaptureAndProcess->ispEnabled = NVMEDIA_FALSE;
    }

    // create NvMedia device
    pCaptureAndProcess->device = NvMediaDeviceCreate();

    if (NULL == pCaptureAndProcess->device) {
        LOG_ERR("%s: failed to create NvMediaDevice instance\n", __func__);
        doHouseKeeping(pCaptureAndProcess);
        return NULL;
    }

    // create the IPP manager
    LOG_DBG("%s: Creating IPP manager\n", __func__);

    pCaptureAndProcess->ippManager =
            NvMediaIPPManagerCreate(NVMEDIA_IPP_VERSION_INFO,
                                    pCaptureAndProcess->device,
                                    NULL);

    if(NULL == pCaptureAndProcess->ippManager) {
        LOG_ERR("%s: Failed to create ippManager\n", __func__);
        doHouseKeeping(pCaptureAndProcess);
        return NULL;
    }

    // if the user has requested an event callback, pass it onto IPP
    if (NULL != pCaptureSS->pEventCallback) {
        status = NvMediaIPPManagerSetEventCallback(
                pCaptureAndProcess->ippManager,
                pCaptureSS->pEventCallbackContext,
                pCaptureSS->pEventCallback);

        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set event callback\n", __func__);

            doHouseKeeping(pCaptureAndProcess);
            return NULL;
        }
    }

    // if the user has requested an global time stamp callback, pass it onto IPP
    if (NULL != pCaptureSS->pGlobalTimeStamp) {
        status = NvMediaIPPManagerSetTimeSource(
                pCaptureAndProcess->ippManager,
                pCaptureSS->pGlobalTimeStampCallbackContext,
                pCaptureSS->pGlobalTimeStamp);

        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set global timestamp callback\n", __func__);

            doHouseKeeping(pCaptureAndProcess);
            return NULL;
        }
    }

    status = CreatePipes(pCaptureAndProcess, pCaptureSS);

    if (NVMEDIA_STATUS_OK != status) {
        // error in pipe creation
        doHouseKeeping(pCaptureAndProcess);
        return NULL;
    }

    return (void *)pCaptureAndProcess;
}

static NvMediaStatus startStop(
        CaptureAndProcessTop    *pCaptureAndProcess,
        NvBool                  start)
{
    NvU32           pipeIndex;
    NvMediaStatus   status;
    PipeTop         *pipe;

    if (NULL == pCaptureAndProcess) {
        LOG_ERR("%s: Invalid capture encode reference\n",
                __func__);

        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    // loop through each pipe and start/stop it
    for (pipeIndex = 0; pipeIndex < pCaptureAndProcess->pipeCount; pipeIndex += 1) {
        pipe = &(pCaptureAndProcess->pipes[pipeIndex]);

        if (NULL != pipe->ipp) {
            if (NVMEDIA_TRUE == start) {
                status = NvMediaIPPPipelineStart(pipe->ipp);
            } else {
                status = NvMediaIPPPipelineStop(pipe->ipp);
            }

            if (NVMEDIA_STATUS_OK != status) {
                LOG_ERR("%s: Failed to start/stop pipeline %d\n",
                        __func__,
                        pipeIndex);

                return NVMEDIA_STATUS_ERROR;
            }
        }
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus CaptureAndProcessGetOutput (
        void                        *pHandle,
        NvMediaIPPComponentOutput   *pOutput,
        NvU32                       pipeNum,
        NvU32                       outputNum)
{
    PipeTop                 *pPipeTop;
    NvMediaStatus           status;
    CaptureAndProcessTop    *pCaptureAndProcess;

    pCaptureAndProcess = (CaptureAndProcessTop *)pHandle;

    if ((NULL == pCaptureAndProcess) ||
        (NULL == pOutput) ||
        (pipeNum > pCaptureAndProcess->pipeCount)) {
        LOG_ERR("%s: invalid get output inputs\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pPipeTop = &(pCaptureAndProcess->pipes[pipeNum]);

    // if the ISP is enabled, then extract output
    // from ISP, else extract from capture
    if (NULL != pPipeTop->isp) {
        status = ISPGetOutput((void *)pPipeTop->isp, pOutput, outputNum);
    } else {
        status = ISPGetOutput((void *)pPipeTop->capture, pOutput, outputNum);
    }

    if (NVMEDIA_STATUS_OK != status) {
        LOG_DBG("%s: failed to obtain output frame for pipe %d, output %d, status = %d\n",
                __func__,
                pipeNum,
                outputNum,
                status);
    }

    return status;
}

NvMediaStatus CaptureAndProcessPutOutput (
        void                        *pHandle,
        NvMediaIPPComponentOutput   *pOutput,
        NvU32                       pipeNum,
        NvU32                       outputNum)
{
    PipeTop                 *pPipeTop;
    NvMediaStatus           status;
    CaptureAndProcessTop    *pCaptureAndProcess;

    pCaptureAndProcess = (CaptureAndProcessTop *)pHandle;

    if ((NULL == pCaptureAndProcess) ||
        (NULL == pOutput) ||
        (pipeNum > pCaptureAndProcess->pipeCount)) {
        LOG_ERR("%s: invalid get output inputs\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pPipeTop = &(pCaptureAndProcess->pipes[pipeNum]);

    // if the ISP has been enabled, then the
    // output was originally extracted from ISP
    // else it was extracted from capture
    // return the buffer to the correct source
    if (NULL != pPipeTop->isp) {
        status = ISPPutOutput(pPipeTop->isp, pOutput, outputNum);
    } else {
        status = CapturePutOutput(pPipeTop->capture, pOutput);
    }

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: failed to return output frame to ISP %d\n",
                __func__,
                pipeNum);
    }

    return status;
}

NvMediaStatus CaptureAndProcessStart (void *pHandle)
{
    NvMediaStatus status;

    status = startStop((CaptureAndProcessTop *)pHandle, NVMEDIA_TRUE);

    return status;
}

NvMediaStatus CaptureAndProcessStop (void *pHandle)
{
    NvMediaStatus status;

    status = startStop((CaptureAndProcessTop *)pHandle, NVMEDIA_FALSE);

    return status;
}

void CaptureAndProcessDestroy(void *pHandle)
{
    doHouseKeeping((CaptureAndProcessTop *)pHandle);

    return;
}
