/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _STREAM_TOP_H_
#define _STREAM_TOP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "nvmedia_ipp.h"
#include "capture.h"
#include "img_dev.h"

#define MAX_NUM_OF_PIPES    4

typedef struct {
    // number of concurrent outputs required
    // e.g. one output to feed to display, and another
    // to feed encoder. It cannot be set to 0 i.e.
    // at least one output has to be enabled
    NvU32                           numOutputs;

    // control to enable/disable processing
    NvBool                          enableProcessing;
} EnablePOutputs;

// data structure to provide configuration parameters for
// streaming engine
typedef struct {

    CaptureParams                   captureParams;

    // reference to sensor-serializer-deserializer driver
    ExtImgDevice                    *extImgDevice;

    ExtImgDevMapInfo                *camMap;

    // number of aggregated streams to capture
    // streamCount == 1 ==> no aggregation
    // streamCount == N ==> aggregating N images
    NvU32                           streamCount;

    // reference to callback function for event handler
    NvMediaIPPEventCallback         *pEventCallback;

    // reference to user context to be provided
    // for event callback
    void                            *pEventCallbackContext;

    // pointer to callback function to provide timestamping
    NvMediaIPPGetAbsoluteGlobalTime *pGlobalTimeStamp;

    // reference to user context to be provided
    // for time stamp callback
    void                            *pGlobalTimeStampCallbackContext;

    // pointer to plugin functions for algorithm control
    NvMediaIPPPluginFuncs           *pPluginFuncs;

    // control to enable/disable outputs for a given pipe
    // of the streaming engine. enablePipeOutputs[N]
    // corresponds to pipe N of the streaming engine
    EnablePOutputs                  enablePipeOutputs[MAX_NUM_OF_PIPES];

} StreamConfigParams;

void *CaptureAndProcessCreate(StreamConfigParams *captureSS);

NvMediaStatus CaptureAndProcessStart (void *pHandle);

NvMediaStatus CaptureAndProcessStop (void *pHandle);

void CaptureAndProcessDestroy(void *pHandle);

NvMediaStatus CaptureAndProcessGetOutput (
        void                        *pHandle,
        NvMediaIPPComponentOutput   *pOutput,
        NvU32                       pipeNum,
        NvU32                       outputNum);

NvMediaStatus CaptureAndProcessPutOutput (
        void                        *pHandle,
        NvMediaIPPComponentOutput   *pOutput,
        NvU32                       pipeNum,
        NvU32                       outputNum);

#ifdef __cplusplus
};      /* extern "C" */
#endif

#endif // _STREAM_TOP_H_
