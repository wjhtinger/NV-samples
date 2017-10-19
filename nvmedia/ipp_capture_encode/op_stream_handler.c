/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "op_stream_handler.h"
#include "log_utils.h"


#define MAX_OUTPUT_STREAM_HANDLER_QUEUE_SIZE                20
#define QUEUE_DEQUEUE_TIMEOUT                               100

// wait for 500ms at max to retrieve all image contexts
// back into the image context queue, bail out with an
// error message if this limit is breached.
// Prevents the application from hanging if there is an error
#define OP_STREAM_HANDLER_MAX_TIME_TO_WAIT_FOR_CLEANUP_US   500000
#define OP_STREAM_HANDLER_SLEEP_PERIOD_FOR_QUEUE_CHECK_US   1000

#define MAX_CONSUMERS               2

typedef struct {
    /* output stream handler context */
    NvThread                *pFeedThread[MAX_CONSUMERS];
    NvQueue                 *pImageContextQueue;
    NvU32                   imageContextBufferCount;
    NvQueue                 *pFeed[MAX_CONSUMERS];
    NvU32                   numOutputs;
    NvU32                   pipeNum;
    NvU32                   skipInitialFramesCount;
    void                    *pFeedThreadContext[MAX_CONSUMERS];
    volatile NvMediaBool    *pQuit;
    NvMediaStatus           (*pGetOutput)(NvMediaIPPComponentOutput *pOutput, NvU32 pipeNum, NvU32 outputNum);
    NvMediaStatus           (*pPutOutput)(NvMediaIPPComponentOutput *pOutput, NvU32 pipeNum, NvU32 outputNum);
} NvOpStreamHandlerContext;

typedef struct {
    NvOpStreamHandlerContext    *pOpStreamHandlerCtx;
    NvU32                       outputNumber;
    NvQueue                     *pFeed;
} ThreadContext;

NvMediaStatus OpStreamHandlerPutBuffer(OutputBufferContext *pBufferContext)
{
    NvMediaIPPComponentOutput   *output = NULL;
    NvOpStreamHandlerContext    *pCtx  = NULL;
    NvU32                       pipeNum;
    NvMediaStatus               status;
    NvU32                       outputNum;

    if (NULL == pBufferContext) {
        // nothing to return
        LOG_WARN("%s: NULL buffer context\n",
                __func__);

        return NVMEDIA_STATUS_OK;
    }

    pCtx = (NvOpStreamHandlerContext *)pBufferContext->pOpaqueContext;
    output = &pBufferContext->output;
    pipeNum = pCtx->pipeNum;
    outputNum = pBufferContext->outputNum;

    // return the buffer back to streaming engine
    status = pCtx->pPutOutput(output, pipeNum, outputNum);

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: error in returning buffer back to IPP\n", __func__);
        *(pCtx->pQuit) = NVMEDIA_TRUE;
    }

    // return the buffer back to the output stream handler queue
    LOG_DBG("%s: returning buffer context %p\n",
            __func__,
            pBufferContext);

    status = NvQueuePut(pCtx->pImageContextQueue,
                        (void *)&pBufferContext,
                        0);

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: error in returning buffer %p\n",
                __func__,
                pBufferContext);
    }

    return status;
}

static void _prepareImageTag (
        NvOpStreamHandlerContext    *pCtx,
        OutputBufferContext         *pOutput,
        NvU32                       outputNum)
{
    pOutput->pOpaqueContext = (void *)pCtx;
    pOutput->outputNum = outputNum;

    return;
}

static NvU32
_opStreamHandlerFeedThreadFunc (void *pData)
{
    ThreadContext               *pThreadContext = (ThreadContext *)pData;
    NvOpStreamHandlerContext    *pOpStreamHandlerCtx;
    NvU32                       pipeNum;
    OutputBufferContext         *pBufferContext;
    NvMediaStatus               status;
    NvU32                       skipInitialFramesCount;

    if (NULL == pThreadContext) {
        LOG_ERR("%s: invalid argument\n",
                __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pOpStreamHandlerCtx = pThreadContext->pOpStreamHandlerCtx;

    if (NULL == pOpStreamHandlerCtx) {
        LOG_ERR("%s: invalid argument\n",
                __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pipeNum = pOpStreamHandlerCtx->pipeNum;
    skipInitialFramesCount = pOpStreamHandlerCtx->skipInitialFramesCount;

    while (NVMEDIA_FALSE == *(pOpStreamHandlerCtx->pQuit)) {
        pBufferContext = NULL;

        /* Acquire buffer context for storing output images from the engine*/
        while (NvQueueGet(pOpStreamHandlerCtx->pImageContextQueue,
                          (void *)&pBufferContext,
                          QUEUE_DEQUEUE_TIMEOUT) != NVMEDIA_STATUS_OK) {
            LOG_WARN("%s: output stream handler buffer context queue is empty\n", __func__);
            if (*pOpStreamHandlerCtx->pQuit)
                goto loop_done;
        }

        // extract the output for feeding to output
        do {
            status = pOpStreamHandlerCtx->pGetOutput((NvMediaIPPComponentOutput *)&pBufferContext->output,
                                                       pipeNum,
                                                       pThreadContext->outputNumber);
            if (*pOpStreamHandlerCtx->pQuit) {
                if (NVMEDIA_STATUS_OK == status) {
                    // we have got a valid image buffer, but the application is
                    // about to shutdown, return the buffer back to IPP
                    _prepareImageTag(pOpStreamHandlerCtx, pBufferContext, pThreadContext->outputNumber);
                    OpStreamHandlerPutBuffer(pBufferContext);
                } else {
                    // we don't have a valid frame from IPP,
                    // simply return the output buffer back to
                    // output stream handler queue
                    NvQueuePut(pOpStreamHandlerCtx->pImageContextQueue,
                               (void *)&pBufferContext,
                               0);
                }

                pBufferContext = NULL;
                goto loop_done;
            }
        } while (NVMEDIA_STATUS_OK != status);
        // this point onwards, we have a valid image buffer from streaming engine
        // and we have also acquired a buffer context from the buffer context queue
        // if the operation is interrupted midway, then  we must safely return the
        // image buffer and buffer context

        // if the initial frames have to be skipped, return the image buffer back to IPP
        if (skipInitialFramesCount) {
            _prepareImageTag(pOpStreamHandlerCtx, pBufferContext, pThreadContext->outputNumber);
            OpStreamHandlerPutBuffer(pBufferContext);

            skipInitialFramesCount -= 1;
            continue;
        }

        // prepare the buffer context
        _prepareImageTag(pOpStreamHandlerCtx, pBufferContext, pThreadContext->outputNumber);

        // push the image buffer to the output feed queue
        status = NvQueuePut(pThreadContext->pFeed,
                            (void *)&pBufferContext,
                            0);

        // if the push to feed queue fails, do not wait, simply return
        // the buffer context and the image buffer back
        if (NVMEDIA_STATUS_OK != status) {
            LOG_ERR("%s: Failed to push image buffer to output. Queue FULL?\n",
                    __func__);
            OpStreamHandlerPutBuffer(pBufferContext);
        }

        // If image buffer is already pushed to feed queue, or is given
        // back to output stream hadler queue, set it NULL because if quit flag
        // is set here same pBufferContext will be returned twice to the
        // output stream handler queue
        pBufferContext = NULL;
    }

loop_done:

    // if we have a valid buffer context and a valid image, return them back
    if (NULL != pBufferContext) {
        OpStreamHandlerPutBuffer(pBufferContext);
    }

    LOG_INFO("%s: output stream handler feed thread exited\n", __func__);

    return NVMEDIA_STATUS_OK;
}

void *
OpStreamHandlerInit(
        OpStreamHandlerParams   *pParams,
        NvQueue                 **pFeedQueueRef)
{
    NvOpStreamHandlerContext    *pOpStreamHandlerCtx  = NULL;
    NvMediaStatus               status;
    NvU32                       count;
    OutputBufferContext         *pBufferContext = NULL;

    if ((NULL == pParams) ||
        (NULL == pParams->pGetOutput) ||
        (NULL == pParams->pPutOutput) ||
        (NULL == pParams->pQuit)) {
        LOG_ERR ("%s : invalid inputs\n",
                __func__);
        return NULL;
    }

    if (pParams->numOutputs > MAX_CONSUMERS) {
        LOG_ERR("%s: Number of outputs desired is more than max number of consumers supported\n",
                __func__);
        return NULL;
    }

   /* allocating command handler context */
    pOpStreamHandlerCtx = calloc(1, sizeof(NvOpStreamHandlerContext));
    if (NULL == pOpStreamHandlerCtx){
        LOG_ERR("%s: Failed to allocate memory for output stream handler\n", __func__);
        return NULL;
    }

    /* initialize context */
    pOpStreamHandlerCtx->pQuit                  = pParams->pQuit;
    pOpStreamHandlerCtx->numOutputs             = pParams->numOutputs;

    // create the feed thread contexts, feed queue
    for (count = 0; count < pOpStreamHandlerCtx->numOutputs; count ++) {
        // create a feed queue
        status = NvQueueCreate(&pOpStreamHandlerCtx->pFeed[count],
                               MAX_OUTPUT_STREAM_HANDLER_QUEUE_SIZE,
                               sizeof(NvMediaImage *));

        if (status != NVMEDIA_STATUS_OK) {
           LOG_ERR("%s: Failed to create feed queue\n",
                   __func__);
           OpStreamHandlerFini(pOpStreamHandlerCtx);
           return NULL;
       }

        // create a thread context
        pOpStreamHandlerCtx->pFeedThreadContext[count] = calloc (1, sizeof(ThreadContext));

        if (NULL == pOpStreamHandlerCtx->pFeedThreadContext[count]) {
            LOG_ERR("%s: failed to allocate feed thread context\n",
                    __func__);

            OpStreamHandlerFini(pOpStreamHandlerCtx);
            return NULL;
        }
    }

    /* Create image context queue */
    status = NvQueueCreate(&pOpStreamHandlerCtx->pImageContextQueue,
                           MAX_OUTPUT_STREAM_HANDLER_QUEUE_SIZE,
                           sizeof(OutputBufferContext *));

    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create buffer context queue for output stream handler\n",
                __func__);
        OpStreamHandlerFini(pOpStreamHandlerCtx);
        return NULL;
    }

    pOpStreamHandlerCtx->imageContextBufferCount = 0;

    for (count = 0; count < MAX_OUTPUT_STREAM_HANDLER_QUEUE_SIZE; count++) {
        pBufferContext = (OutputBufferContext *)calloc (1, sizeof(OutputBufferContext));

        if (NULL == pBufferContext) {
            LOG_ERR("%s: failed to allocate output buffer context\n",
                    __func__);
            OpStreamHandlerFini(pOpStreamHandlerCtx);
            return NULL;
        }

        status = NvQueuePut(pOpStreamHandlerCtx->pImageContextQueue,
                            (void *)&pBufferContext,
                             0);

        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: failed to create image queue entries\n", __func__);

            // free pBufferContext, it did not make its way into the queue
            free (pBufferContext);
            OpStreamHandlerFini(pOpStreamHandlerCtx);
            return NULL;
        }

        pOpStreamHandlerCtx->imageContextBufferCount = count+1;
    }

    pOpStreamHandlerCtx->pipeNum                = pParams->pipeNum;
    pOpStreamHandlerCtx->pGetOutput             = pParams->pGetOutput;
    pOpStreamHandlerCtx->pPutOutput             = pParams->pPutOutput;
    pOpStreamHandlerCtx->skipInitialFramesCount = pParams->skipInitialFramesCount;

    // if the host wants to collect the references to the output queue,
    // send them out
    if (NULL != pFeedQueueRef) {
        for (count = 0; count < pParams->numOutputs; count++) {
            pFeedQueueRef[count] = pOpStreamHandlerCtx->pFeed[count];
        }
    }

    return (void *)pOpStreamHandlerCtx;
}

static void
_destroyThread(
        NvOpStreamHandlerContext *pOpStreamHandlerCtx)
{
    NvMediaStatus   status;
    NvU32           count;

    if (NULL == pOpStreamHandlerCtx) {
        return;
    }

    *(pOpStreamHandlerCtx->pQuit) = NVMEDIA_TRUE;

    for (count = 0; count < pOpStreamHandlerCtx->numOutputs; count++) {
        if (pOpStreamHandlerCtx->pFeedThread[count]) {
            status = NvThreadDestroy(pOpStreamHandlerCtx->pFeedThread[count]);
            pOpStreamHandlerCtx->pFeedThread[count] = NULL;
            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to destroy output stream handler feed thread\n",
                        __func__);
            }
        }
    }

    return;
}

void
OpStreamHandlerStop(void *pHandle)
{
    NvOpStreamHandlerContext *pOpStreamHandlerCtx  = NULL;

    if (NULL == pHandle) {
        return;
    }

    pOpStreamHandlerCtx = (NvOpStreamHandlerContext *)pHandle;
    _destroyThread(pOpStreamHandlerCtx);

    return;
}

NvMediaStatus
OpStreamHandlerFini(void *pHandle)
{
    NvOpStreamHandlerContext    *pOpStreamHandlerCtx  = NULL;
    NvU32                       numElements = 0;
    OutputBufferContext         *pBufferContext = NULL;
    NvU32                       loopCountDown;
    NvU32                       contextCount;

    if (NULL == pHandle)
        return NVMEDIA_STATUS_OK;

    pOpStreamHandlerCtx = (NvOpStreamHandlerContext *)pHandle;

    for (contextCount = 0; contextCount < pOpStreamHandlerCtx->numOutputs; contextCount++) {
        if (NULL != pOpStreamHandlerCtx->pFeedThread[contextCount]) {
            LOG_ERR("%s: Output stream handler threads active, invoke OpStreamHandlerStop first\n",
                            __func__);

            return NVMEDIA_STATUS_ERROR;
        }
    }

    _destroyThread(pOpStreamHandlerCtx);

    // we are producer, drain and destroy the feed threads
    for (contextCount = 0; contextCount < MAX_CONSUMERS; contextCount++) {
        if (NULL != pOpStreamHandlerCtx->pFeed[contextCount]) {
            while (NvQueueGet(pOpStreamHandlerCtx->pFeed[contextCount],
                              (void *)&pBufferContext,
                              0) == NVMEDIA_STATUS_OK) {
                OpStreamHandlerPutBuffer(pBufferContext);
            }
            NvQueueDestroy(pOpStreamHandlerCtx->pFeed[contextCount]);
        }
    }

    /* Destroy output stream handler queue AFTER all its buffers have been returned back*/
    if (pOpStreamHandlerCtx->pImageContextQueue) {

        loopCountDown = OP_STREAM_HANDLER_MAX_TIME_TO_WAIT_FOR_CLEANUP_US/OP_STREAM_HANDLER_SLEEP_PERIOD_FOR_QUEUE_CHECK_US;
        if (0 == loopCountDown) {
            loopCountDown = 1;   // iterate at least once
        }

        do {
            NvQueueGetSize(pOpStreamHandlerCtx->pImageContextQueue, &numElements);

            if (numElements != pOpStreamHandlerCtx->imageContextBufferCount) {
                usleep(OP_STREAM_HANDLER_SLEEP_PERIOD_FOR_QUEUE_CHECK_US);
                NvQueueGetSize(pOpStreamHandlerCtx->pImageContextQueue, &numElements);
            }
        } while ((numElements != pOpStreamHandlerCtx->imageContextBufferCount) &&
                 (0 != loopCountDown));

        if (numElements != pOpStreamHandlerCtx->imageContextBufferCount) {
            // we exhausted the time limit to wait for all the image
            // contexts to be back, flag error, but free whatever
            // image contexts are available in the queue
            LOG_ERR("%s: All image contexts not returned to queue, bad image context management\n",
                    __func__);
        }

        // now extract each element of the queue and free it
        while (NvQueueGet(pOpStreamHandlerCtx->pImageContextQueue,
                         (void *)&pBufferContext,
                         0) == NVMEDIA_STATUS_OK) {
            free (pBufferContext);
        }
        NvQueueDestroy(pOpStreamHandlerCtx->pImageContextQueue);
    }

    // destroy the thread contexts
    for (contextCount = 0; contextCount < MAX_CONSUMERS; contextCount++) {
        if (NULL != pOpStreamHandlerCtx->pFeedThreadContext[contextCount]) {
            free (pOpStreamHandlerCtx->pFeedThreadContext[contextCount]);
        }
    }

    free(pOpStreamHandlerCtx);

    LOG_INFO("%s: done\n", __func__);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
OpStreamHandlerStart(void *pHandle)
{
    NvOpStreamHandlerContext    *pOpStreamHandlerCtx  = NULL;
    NvMediaStatus               status;
    ThreadContext               *pThreadContext;
    NvU32                       threadContextCount;

    pOpStreamHandlerCtx = (NvOpStreamHandlerContext *)pHandle;

    if (NULL == pOpStreamHandlerCtx) {
        LOG_ERR("%s: invalid context\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    status = NVMEDIA_STATUS_OK;

    for (threadContextCount = 0; threadContextCount < pOpStreamHandlerCtx->numOutputs; threadContextCount++) {
        pThreadContext = (ThreadContext *)pOpStreamHandlerCtx->pFeedThreadContext[threadContextCount];

        if (NULL == pThreadContext) {
            LOG_ERR("%s: FeedThreadContext not initialized. Call OpStreamHandlerInit first.\n",
                    __func__);
            status = NVMEDIA_STATUS_ERROR;
            break;
        }

        pThreadContext->pOpStreamHandlerCtx = pOpStreamHandlerCtx;
        pThreadContext->outputNumber        = threadContextCount;
        pThreadContext->pFeed               = pOpStreamHandlerCtx->pFeed[threadContextCount];

        status = NvThreadCreate(&pOpStreamHandlerCtx->pFeedThread[threadContextCount],
                                &_opStreamHandlerFeedThreadFunc,
                                (void *)pThreadContext,
                                NV_THREAD_PRIORITY_NORMAL);

        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to create output stream handler feed thread\n",
                    __func__);
            break;
        }
    }

    return status;
}

