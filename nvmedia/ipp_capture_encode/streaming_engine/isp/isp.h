/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _ISP_H_
#define _ISP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "nvmedia_ipp.h"
#include "nvmedia_ipp.h"
#include "nvmedia_icp.h"
#include "nvcommon.h"


// data structure to map all the capture configuration parameters
typedef struct {

    //// IPP ISP configuration params ////

    // control to specify the number of concurrent
    // outputs to be enabled for the ISP. It cannot
    // be set to 0
    NvU32                           numOutputs;

    // control to enable/disable processing
    NvBool                          enableProcessing;

    // configure the number of internal buffers to be allocated
    // for ISP buffer pool
    NvU32                           numOfBuffersInISPPool;

    // target ISP hardware pipe, not applicable for t186
    NvMediaISPSelect                ispSelect;

    // width of the image in pixels
    NvU32                           width;

    // height of the image in pixels
    NvU32                           height;

    //// algorithm control specification ////

    // raw pixel order (used for algorithm processing)
    NvMediaRawPixelOrder            pixelOrder;

    // bits per pixel (used for algorithm processing)
    NvMediaBitsPerPixel             bitsPerPixel;

    // algorithm control plugin function
    NvMediaIPPPluginFuncs           *pPluginFuncs;

    //// configuration for sensor control ////

    NvMediaISCRootDevice            *iscRootDevice;

    NvMediaISCDevice                *iscAggregatorDevice;

    NvMediaISCDevice                *iscSerializerDevice;

    NvMediaISCDevice                *iscSensorDevice;

    // configure the number of internal buffers to be allocated
    // for algorithm control buffer pool
    NvU32                           numOfBuffersInAlgorithmControlPool;

} ISPParams;

void * ISPCreate(
        NvMediaIPPPipeline  *pIPPPipeline,
        ISPParams           *pISPConfig,
        NvMediaIPPComponent *pImageDataInput,
        NvU32               pipeNum);

NvMediaStatus ISPGetOutput (
        void                        *pISP,
        NvMediaIPPComponentOutput   *pOoutput,
        NvU32                       outputNum);

NvMediaStatus ISPPutOutput (
        void                        *pISP,
        NvMediaIPPComponentOutput   *pOutput,
        NvU32                       outputNum);

void ISPDelete(void *pISP);

#ifdef __cplusplus
};      /* extern "C" */
#endif

#endif // _ISP_H_
