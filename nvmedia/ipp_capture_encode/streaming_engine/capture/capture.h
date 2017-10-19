/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _CAPTURE_H_
#define _CAPTURE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "nvmedia_ipp.h"
#include "nvmedia_icp.h"
#include "nvcommon.h"

// data structure to map all the capture configuration parameters
typedef struct {
    //// input interface specification ////

    // number of CSI lanes
    NvU32                       interfaceLanesCount;

    // pixel clock frequency in Hz
    NvU32                       pixelFrequency_Hz;

    // MIPI THS-SETTLE time
    NvU32                       thsSettle;


    //// input image specification ////

    // number of aggregated streams per captured frame
    // for multi-camera use case. Set to 1 for single camera
    // streaming, else set to number of frames aggregated
    NvU32                       streamCount;

    // Set to NVMEDIA_TRUE in multi-camera use case
    // when virtual channel streaming has to be used
    NvMediaBool                 useVirtualChannels;

    // ICP interface
    NvMediaICPInterfaceType     inputInterfaceType;

    // input image format
    NvMediaICPInputFormatType   inputFormatType;

    // input bits per pixel (applicable only for inputFormatType == RAW)
    NvMediaBitsPerPixel         inputBitsPerPixel;

    // input pixel order (applicable only for inputFormatType == RAW)
    NvMediaRawPixelOrder        inputPixelOrder;


    //// output data specification ////

    // surface type of the output data
    NvMediaICPSurfaceFormatType outputSurfaceFormatType;

    // output bits per pixel (applicable only for surfaceFormatType == RAW)
    NvMediaBitsPerPixel         outputBitsPerPixel;

    // output pixel order (applicable only for surfaceFormatType == RAW)
    NvMediaRawPixelOrder        outputPixelOrder;


    //// frame dimension ////

    // width in pixels of each of the streams
    NvU32                       width;

    // height in pixels of each of the streams
    NvU32                       height;

    // horizontal start position
    NvU32                       startX;

    // vertical start position
    NvU32                       startY;

    // embedded data lines at the top of the frames
    NvU32                       embeddedDataLinesTop;

    // embedded data lines at the bottom of the frames
    NvU32                       embeddedDataLinesBottom;

    // configure the number of internal buffers to be allocated
    // for capture buffer pool
    NvU32                       numOfBuffersInPool;


    //// output data control ////

    // control to allow data from capture to be extracted to
    // the outside world
    // if extracting data directly from capture is not needed
    // set this control to NVMEDIA_FALSE
    NvBool                      enableOuptut;

} CaptureParams;

void * CaptureCreate(
        NvMediaIPPPipeline  *pIppPipeline,
        CaptureParams       *pCaptureConfig);

void CaptureDelete(void *pCapture);

NvMediaStatus CaptureGetOutput (
        void                        *pCapture,
        NvMediaIPPComponentOutput   *pOutput);

NvMediaStatus CapturePutOutput (
        void                        *pCapture,
        NvMediaIPPComponentOutput   *pOutput);

NvMediaIPPComponent * CaptureGetIPPComponent(
        void  *pCapture);

#ifdef __cplusplus
};      /* extern "C" */
#endif

#endif // _CAPTURE_H_
