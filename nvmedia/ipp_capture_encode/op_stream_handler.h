/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __OP_STREAM_HANDLER_H__
#define __OP_STREAM_HANDLER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "thread_utils.h"
#include "nvmedia_ipp.h"

typedef struct {

    NvU32                   pipeNum;
    NvU32                   numOutputs;
    NvU32                   skipInitialFramesCount;
    NvMediaStatus           (*pGetOutput)(NvMediaIPPComponentOutput *pOutput, NvU32 pipeNum, NvU32 outputNum);
    NvMediaStatus           (*pPutOutput)(NvMediaIPPComponentOutput *pOutput, NvU32 pipeNum, NvU32 outputNum);
    volatile NvMediaBool    *pQuit;
}OpStreamHandlerParams;

typedef struct {
    NvMediaIPPComponentOutput   output;
    void                        *pOpaqueContext;
    NvU32                       outputNum;
} OutputBufferContext;

// pFeedQueueRef is an array of NvQueue * used to
// collect a reference to the output queues used by the output
// stream handler instance. Set it to NULL if it is not
// needed to collect the references to the output queues used
// by the output stream handler.
// The depth of pFeedQueueRef should be at least equal to
// pParams->numOutputs element
void *
OpStreamHandlerInit(
        OpStreamHandlerParams   *pParams,
        NvQueue                 **pFeedQueueRef);

NvMediaStatus
OpStreamHandlerFini(void *pHandle);

NvMediaStatus
OpStreamHandlerStart(void *pHandle);

NvMediaStatus
OpStreamHandlerPutBuffer(OutputBufferContext *pBufferContext);

void
OpStreamHandlerStop(void *pHandle);

#ifdef __cplusplus
}
#endif

#endif // __OP_STREAM_HANDLER_H__
