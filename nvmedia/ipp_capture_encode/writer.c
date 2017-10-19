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
#include "writer.h"
#include "log_utils.h"
#include "image_encoder.h"

#define QUEUE_DEQUEUE_TIMEOUT           100
#define ENCODED_OUTPUT_FILE_NAME_SIZE   100

typedef struct {
    /* output stream handler context */
    NvThread                *pWriterThread;
    NvQueue                 *pInputQueue;
    char                    encodedOutputFileName[ENCODED_OUTPUT_FILE_NAME_SIZE];
    FILE                    *pEncodedOutputFile;
    NvU32                   frameCount;
    volatile NvMediaBool    *pQuit;
} NvWriterContext;

static void
_destroyThread(
        NvWriterContext *pWriterContext)
{
    if (NULL == pWriterContext) {
         // nothing to do, return
         return;
    }

    *(pWriterContext->pQuit) = NVMEDIA_TRUE;

    // destroy the thread data structure
    if (NULL != pWriterContext->pWriterThread) {
        NvThreadDestroy(pWriterContext->pWriterThread);
    }

    pWriterContext->pWriterThread = NULL;
    return;
}

static void
doHouseKeeping (
        NvWriterContext *pWriterContext)
{
    if (NULL == pWriterContext) {
        // nothing to do, return
        return;
    }

    _destroyThread(pWriterContext);

    // if a file has been opened, close it
    if (NULL != pWriterContext->pEncodedOutputFile) {
        fclose(pWriterContext->pEncodedOutputFile);
    }

    // no need to drain pInputQueue, it does not belong
    // to the writer, the owner will drain it and destroy
    // it safely

    // free the context data structure
    free (pWriterContext);

    return;
}

static NvU32
_writerThreadFunc(void *pData)
{
    NvWriterContext                     *pWriterContext = NULL;
    EncodedBufferContainer              *pEncodedBufferContainer = NULL;
    void                                *pDataBuffer;
    NvU32                               bufferSize;
    ImageEncoderPutBufferToEncoderFunc  pPutBufferFunc;

    if (NULL == pData) {
        LOG_ERR("%s: Invalid argument to encoder thread\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pWriterContext = (NvWriterContext *)pData;
    pWriterContext->frameCount = 0;

    while (!(*pWriterContext->pQuit)) {
        // extract buffer to be stored to file from input queue
        // get an uncompressed buffer from the input queue
        while (NvQueueGet(pWriterContext->pInputQueue,
                          (void *)&pEncodedBufferContainer,
                          QUEUE_DEQUEUE_TIMEOUT) != NVMEDIA_STATUS_OK) {
            LOG_INFO("%s: input image queue is empty\n",
                    __func__);

            if (*pWriterContext->pQuit) {
                goto loop_done;
            }
        }

        // we have a valid encoded buffer to write
        // extract the buffer size and dump to file
        bufferSize      = pEncodedBufferContainer->encodedBufferSizeBytes;
        pDataBuffer     = (void *)pEncodedBufferContainer->encodedBuffer;
        pPutBufferFunc  = pEncodedBufferContainer->pPutBufferFunc;

        if ((NULL == pDataBuffer) ||
            (NULL == pPutBufferFunc)) {
            LOG_WARN("%s: invalid buffer container dropped\n",
                    __func__);
            continue;
        }

        LOG_DBG("%s: frame count = %d, buffer size = %d",
                __func__,
                pWriterContext->frameCount,
                bufferSize);

        fwrite (pDataBuffer,
                1,
                bufferSize,
                pWriterContext->pEncodedOutputFile);

        // return free the buffer
        pPutBufferFunc(pEncodedBufferContainer);
        pWriterContext->frameCount += 1;
    }

loop_done:
    return NVMEDIA_STATUS_OK;
}

void *
WriterInit(
        TestArgs                *pAllArgs,
        NvQueue                 *pInputEncodedImageQueue,
        NvU32                   streamNum,
        volatile NvMediaBool    *pQuit)
{

    NvWriterContext *pWriterContext = NULL;

    if ((NULL == pAllArgs)  ||
        (NULL == pQuit)     ||
        (NULL == pInputEncodedImageQueue)) {
        LOG_ERR ("%s: invalid inputs\n",
                __func__);
        return NULL;
    }

    pWriterContext = (NvWriterContext *)calloc (1, sizeof(NvWriterContext));
    if (NULL == pWriterContext) {
        LOG_ERR ("%s: Failed to allocate writer context\n",
                __func__);

        return NULL;
    }

    pWriterContext->pInputQueue = pInputEncodedImageQueue;
    sprintf(pWriterContext->encodedOutputFileName,
            "%s_%d",
            pAllArgs->encodeOutputFileName,
            streamNum);

    // open the encoded output file
    pWriterContext->pEncodedOutputFile =
            fopen(pWriterContext->encodedOutputFileName,
                  "wb");

    if (NULL == pWriterContext->pEncodedOutputFile) {
        LOG_ERR("%s: Error opening output encoded file %s\n",
                __func__,
                pWriterContext->encodedOutputFileName);
        doHouseKeeping(pWriterContext);
        return NULL;
    }

    pWriterContext->pQuit = pQuit;

    return (void *)pWriterContext;
}

void
WriterStop(void *pHandle)
{
    NvWriterContext *pWriterContext;

    if (NULL == pHandle) {
        return;
    }

    pWriterContext = (NvWriterContext *)pHandle;

    // destroy the thread data structure
    _destroyThread(pWriterContext);

    return;
}

NvMediaStatus
WriterFini(void *pHandle)
{
    NvWriterContext     *pWriterContext = NULL;

    if (NULL == pHandle) {
        return NVMEDIA_STATUS_OK;
    }

    pWriterContext = (NvWriterContext *)pHandle;

    if (NULL != pWriterContext->pWriterThread) {
        LOG_ERR("%s: Writer thread  active, invoke WriterStop first\n",
                __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    doHouseKeeping(pWriterContext);

    LOG_MSG("%s: done, total number of frames stored = %d\n",
            __func__,
            pWriterContext->frameCount);

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
WriterStart(void *pHandle)
{
    NvWriterContext     *pWriterContext = NULL;
    NvMediaStatus       status;

    if (NULL == pHandle) {
        LOG_ERR ("%s: invalid inputs\n",
                __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pWriterContext = (NvWriterContext *)pHandle;

    // create the writer thread
    status = NvThreadCreate(&pWriterContext->pWriterThread,
                            &_writerThreadFunc,
                            (void *)pWriterContext,
                            NV_THREAD_PRIORITY_NORMAL);

    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create image encoder thread\n",
                __func__);

        return status;
    }

    return NVMEDIA_STATUS_OK;
}

