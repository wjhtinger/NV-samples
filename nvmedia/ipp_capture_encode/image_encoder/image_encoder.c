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
#include "log_utils.h"
#include "nvmedia_iep.h"
#include "image_encoder.h"


#define IMAGE_ENCODER_BUFFER_CONATINER_QUEUE_SIZE       20
#define IMAGE_ENCODER_OUTPUT_QUEUE_SIZE                 20
#define IMAGE_ENCODER_QUEUE_DEQUEUE_TIMEOUT             100
#define IMAGE_ENCODER_QUEUE_ENQUEUE_TIMEOUT             100
#define IMAGE_ENCODER_ENCODED_FRAME_AVLBL_MAX_TIMEOUT   100

// wait for 500ms at max to retrieve all encoded buffer
// containers back into the buffer container queue, bail
// out with an error message if this limit is breached.
// Prevents the application from hanging if there is an error
#define IMAGE_ENCODER_MAX_TIME_TO_WAIT_FOR_CLEANUP_US   500000
#define IMAGE_ENCODER_SLEEP_PERIOD_FOR_QUEUE_CHECK_US   1000

typedef struct {
    NvMediaDevice                               *device;
    NvMediaIEP                                  *pImageEncoder;
    RunTimePicParamsCallback                    pGetRunTimePicParams;
    NvMediaRect                                 sourceRect;
    NvU32                                       frameCount;

    NvThread                                    *pImageEncoderFeedThread;
    NvThread                                    *pImageEncoderOutputThread;
    NvQueue                                     *pInputImageQueue;
    NvQueue                                     *pOutputQueue;
    NvQueue                                     *pEncodedBufferContainerQueue;
    NvU32                                       bufferContainerCount;

    NvU32                                       encodedFrameCount;
    void                                        *pHostContext;

    ImageEncoderReturnUncompressedBufferFunc    pReturnUncompressedBufferFunc;
    volatile NvMediaBool                        *pQuit;
} NvImageEncoderContext;

static NvMediaStatus
_putBufferContainer (void *pBufferContainer)
{
    NvImageEncoderContext   *pImageEncoderCtx = NULL;
    EncodedBufferContainer  *pEncodedBufferContainer = NULL;
    NvMediaStatus           status;

    pEncodedBufferContainer = (EncodedBufferContainer *)pBufferContainer;

    if ((NULL == pEncodedBufferContainer) ||
        (NULL == pEncodedBufferContainer->pImageEncoderCtx)) {
        LOG_ERR("%s: invalid arguments\n",
                __func__);

        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pImageEncoderCtx = pEncodedBufferContainer->pImageEncoderCtx;

    // reset the buffer size and return the encoded buffer
    // container back to the pEncodedBufferContainerQueue
    pEncodedBufferContainer->encodedBufferSizeBytes = 0;
    status = NvQueuePut(pImageEncoderCtx->pEncodedBufferContainerQueue,
                        (void *)&pEncodedBufferContainer,
                        0);

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: Failed to queue buffer container \n", __func__);
    }

    return status;
}

static NvMediaStatus
_ImageEncoderOutputThreadFunc (void *data)
{
    NvImageEncoderContext   *pImageEncoderCtx;
    NvMediaStatus           status;
    NvU32                   bytesAvailable = 0;
    EncodedBufferContainer  *pEncodedBufferContainer = NULL;

    pImageEncoderCtx = (NvImageEncoderContext *)data;

    if (NULL == pImageEncoderCtx) {
        LOG_ERR("%s: Invalid argument to encoder thread\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pImageEncoderCtx->encodedFrameCount = 0;

    while (!(*pImageEncoderCtx->pQuit)) {
        // check to see if there are encoded frames available
        do {
            status = NvMediaIEPBitsAvailable(pImageEncoderCtx->pImageEncoder,
                                             &bytesAvailable,
                                             NVMEDIA_ENCODE_BLOCKING_TYPE_IF_PENDING,
                                             IMAGE_ENCODER_ENCODED_FRAME_AVLBL_MAX_TIMEOUT);

            if (NVMEDIA_TRUE == *pImageEncoderCtx->pQuit) {
                goto loop_done;
            }

        } while ((NVMEDIA_STATUS_PENDING == status) ||
                 (NVMEDIA_STATUS_NONE_PENDING == status) ||
                 (NVMEDIA_STATUS_TIMED_OUT == status));

        if (NVMEDIA_STATUS_OK != status) {
            LOG_ERR("%s: error in extracting encoded frame status\n",
                    __func__);

            *pImageEncoderCtx->pQuit = NVMEDIA_TRUE;
            goto loop_done;
        }

        // check to ensure that we have enough buffer space to store 'bytesAvailable' bytes
        if (bytesAvailable > MAX_ENCODED_BUFFER_SIZE) {
            // skipping an encoded frame is bad, but it is still better than writing to illegal
            // memory addresses
            LOG_ERR("%s: encoded frame size: %d, maximum buffer size: %d, skipping\n",
                    __func__,
                    bytesAvailable,
                    MAX_ENCODED_BUFFER_SIZE);

            continue;
        }

        // fetch a free buffer container
        // get an uncompressed buffer from the input queue
        while (NvQueueGet(pImageEncoderCtx->pEncodedBufferContainerQueue,
                          (void *)&pEncodedBufferContainer,
                          IMAGE_ENCODER_QUEUE_DEQUEUE_TIMEOUT) != NVMEDIA_STATUS_OK) {

            LOG_INFO("%s: empty buffer container not available\n",
                    __func__);
            if (*pImageEncoderCtx->pQuit) {
                goto loop_done;
            }
        }

        // this point onwards, we have a valid buffer,
        // container, ensure that it is freed before exiting

        pEncodedBufferContainer->encodedBufferSizeBytes = bytesAvailable;

        // fetch the encoded buffer from the encoder
        status = NvMediaIEPGetBits(pImageEncoderCtx->pImageEncoder,
                                   &bytesAvailable,
                                   (void *)pEncodedBufferContainer->encodedBuffer);

        if (NVMEDIA_STATUS_OK != status) {
            // there was an error fetching the encoded frame from the encoder,
            // return the buffer container back to its queue
            LOG_ERR("%s: error in fetching the encoded frame from encoder\n",
                    __func__);

            *pImageEncoderCtx->pQuit = NVMEDIA_TRUE;
            _putBufferContainer(pEncodedBufferContainer);

            goto loop_done;
        }

        LOG_DBG("%s: frame count = %d, num_of_bytes = %d\n",
                 __func__,
                 pImageEncoderCtx->encodedFrameCount,
                 bytesAvailable);

        // put the encoded buffer into output queue
        status = NvQueuePut(pImageEncoderCtx->pOutputQueue,
                            (void *)&pEncodedBufferContainer,
                            IMAGE_ENCODER_QUEUE_ENQUEUE_TIMEOUT);

        if (NVMEDIA_STATUS_OK != status) {
            // we could not push to the output queue
            LOG_WARN("%s: failed to push the encoded buffer into output queue\n",
                    __func__);

            // return the buffer container
            _putBufferContainer(pEncodedBufferContainer);
            *pImageEncoderCtx->pQuit = NVMEDIA_TRUE;
            goto loop_done;
        }
        else {
            pImageEncoderCtx->encodedFrameCount += 1;
        }
    }

loop_done:

    LOG_INFO("%s: image encoder output thread exited\n", __func__);
    return NVMEDIA_STATUS_OK;
}

static NvU32
_ImageEncoderFeedThreadFunc(void *data)
{
    NvImageEncoderContext   *pImageEncoderCtx;
    NvMediaImage            *pImage;
    NvMediaStatus           status;
    ImageEncoderPicParams   *pEncodePicParams;
    OutputBufferContext     *pBufferContext;

    pImageEncoderCtx = (NvImageEncoderContext *)data;

    if (NULL == pImageEncoderCtx) {
        LOG_ERR("%s: Invalid argument to encoder thread\n",
                __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    while (!(*pImageEncoderCtx->pQuit)) {
        pBufferContext = NULL;

        // get an uncompressed buffer from the input queue
        while (NvQueueGet(pImageEncoderCtx->pInputImageQueue,
                          (void *)&pBufferContext,
                          IMAGE_ENCODER_QUEUE_DEQUEUE_TIMEOUT) != NVMEDIA_STATUS_OK) {
            LOG_INFO("%s: input image queue is empty\n",
                    __func__);
            if (*pImageEncoderCtx->pQuit) {
                goto loop_done;
            }
        }

        pImage = pBufferContext->output.image;

        // this point onwards, we have a valid pImage
        // ensure to return it if we quit the thread

        pEncodePicParams = NULL;
        pEncodePicParams = pImageEncoderCtx->pGetRunTimePicParams(
                pImageEncoderCtx->pHostContext,
                pImageEncoderCtx->frameCount);

        if (NULL == pEncodePicParams) {
            LOG_ERR("%s: invalid PIC params received",
                    __func__);

            // return the image buffer back before exiting
            pImageEncoderCtx->pReturnUncompressedBufferFunc(pBufferContext);
            pBufferContext = NULL;
            *pImageEncoderCtx->pQuit = NVMEDIA_TRUE;
            goto loop_done;
        }

        // feed the uncompressed image buffer to encoder
        // the encoder may reject the feed if it runs out
        // of internal buffers to hold encoded streams
        do {
            status = NvMediaIEPFeedFrame(pImageEncoderCtx->pImageEncoder,
                                         pImage,
                                         &pImageEncoderCtx->sourceRect,
                                         (void *)pEncodePicParams);

            if (*pImageEncoderCtx->pQuit) {
                // return the image buffer back before exiting
                if (NVMEDIA_STATUS_OK != pImageEncoderCtx->pReturnUncompressedBufferFunc(pBufferContext)) {

                    LOG_ERR("%s: failed to return uncompressed buffer back\n",
                            __func__);
                }

                pBufferContext = NULL;
                goto loop_done;
            }
        } while (NVMEDIA_STATUS_INSUFFICIENT_BUFFERING == status);

        // there was an error (other than insufficient buffering) in feeding
        // uncompressed frame to encoder
        if (NVMEDIA_STATUS_OK != status) {
            LOG_ERR("%s: error feeding uncompressed frame to encoder, status = %d\n",
                    __func__,
                    status);

            // return the image buffer back before exiting
            pImageEncoderCtx->pReturnUncompressedBufferFunc(pBufferContext);
            pBufferContext = NULL;
            *pImageEncoderCtx->pQuit = NVMEDIA_TRUE;
            goto loop_done;
        }
        else {
            pImageEncoderCtx->frameCount += 1;
        }

        // we can return the uncompressed buffer back to its owner
        // without waiting for the encoding to complete
        // the NvMedia layer ensures that it is reused only
        // after the encode fence is reached
        status = pImageEncoderCtx->pReturnUncompressedBufferFunc(pBufferContext);

        if (NVMEDIA_STATUS_OK != status) {
            LOG_ERR("%s: error returning uncompressed frame to encoder\n", __func__);
            pBufferContext = NULL;
            *pImageEncoderCtx->pQuit = NVMEDIA_TRUE;
            goto loop_done;
        }
    }

loop_done:

    LOG_INFO("%s: image encoder feed thread exited\n", __func__);
    return NVMEDIA_STATUS_OK;
}

static void
_destroyThread (NvImageEncoderContext *pImageEncoderCtx)
{
    NvMediaStatus status;

    if (NULL == pImageEncoderCtx) {
        return;
    }

    *(pImageEncoderCtx->pQuit) = NVMEDIA_TRUE;

    /* Destroy the threads */
    if (pImageEncoderCtx->pImageEncoderFeedThread) {
        status = NvThreadDestroy(pImageEncoderCtx->pImageEncoderFeedThread);
        if (status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy image encoder feed thread\n",
                    __func__);
    }

    if (pImageEncoderCtx->pImageEncoderOutputThread) {
        status = NvThreadDestroy(pImageEncoderCtx->pImageEncoderOutputThread);
        if (status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to destroy image encoder output thread\n",
                    __func__);
    }

    pImageEncoderCtx->pImageEncoderFeedThread   = NULL;
    pImageEncoderCtx->pImageEncoderOutputThread = NULL;

    return;
}

static void
doHouseKeeping (
        NvImageEncoderContext *pImageEncoderCtx)
{
    EncodedBufferContainer  *pEncodedBufferContainer = NULL;
    NvU32                   size;
    NvU32                   loopCountDown;

    if (NULL == pImageEncoderCtx) {
        return;
    }

    _destroyThread(pImageEncoderCtx);

    // do not drain/destroy the pInputImageQueue
    // the owner will do it at a safe time

    // drain and destroy the output queue
    if (NULL != pImageEncoderCtx->pOutputQueue) {
        while (NvQueueGet(pImageEncoderCtx->pOutputQueue,
                          (void *)&pEncodedBufferContainer,
                          0) == NVMEDIA_STATUS_OK) {
            _putBufferContainer(pEncodedBufferContainer);
        }

        NvQueueDestroy(pImageEncoderCtx->pOutputQueue);
    }

    if (NULL != pImageEncoderCtx->pEncodedBufferContainerQueue) {
        loopCountDown = IMAGE_ENCODER_MAX_TIME_TO_WAIT_FOR_CLEANUP_US/IMAGE_ENCODER_SLEEP_PERIOD_FOR_QUEUE_CHECK_US;
        if (0 == loopCountDown) {
            loopCountDown = 1;   // iterate at least once
        }
        // wait till all the buffer containers are not available
        do {
            NvQueueGetSize(pImageEncoderCtx->pEncodedBufferContainerQueue,
                           &size);

            if (IMAGE_ENCODER_BUFFER_CONATINER_QUEUE_SIZE != size) {
                // sleep for sometime if all the buffer containers are not
                // back to the queue
                usleep(IMAGE_ENCODER_SLEEP_PERIOD_FOR_QUEUE_CHECK_US);

                NvQueueGetSize(pImageEncoderCtx->pEncodedBufferContainerQueue,
                               &size);
            }

            loopCountDown -= 1;
        } while ((pImageEncoderCtx->bufferContainerCount != size) &&
                 (0 != loopCountDown));

        if (pImageEncoderCtx->bufferContainerCount != size) {
            // we exhausted the time limit to wait for all the buffer
            // containers to be back, flag error, but free whatever
            // buffer containers are available in the queue
            LOG_ERR("%s: All buffer containers not returned to queue, bad buffer container management\n",
                    __func__);
        }

        // now free all the buffer containers
        while (NvQueueGet(pImageEncoderCtx->pEncodedBufferContainerQueue,
                         (void *)&pEncodedBufferContainer,
                          0) == NVMEDIA_STATUS_OK) {
            free(pEncodedBufferContainer);
        }

        // free the buffer container queue itself
        NvQueueDestroy(pImageEncoderCtx->pEncodedBufferContainerQueue);
    }

    if (NULL != pImageEncoderCtx->pImageEncoder) {
        NvMediaIEPDestroy(pImageEncoderCtx->pImageEncoder);
    }

    if (NULL != pImageEncoderCtx->device) {
        NvMediaDeviceDestroy(pImageEncoderCtx->device);
    }

    free (pImageEncoderCtx);

    return;
}

void *
ImageEncoderInit(
        ImageEncoderParams      *pImageEncoderParams,
        volatile NvMediaBool    *pQuit,
        NvQueue                 **pOutputQueueRef)
{
    NvMediaStatus           status;
    NvImageEncoderContext   *pImageEncoderCtx = NULL;
    NvU32                   count;
    EncodedBufferContainer  *pEncodedBufferContainer;

    if ((NULL == pImageEncoderParams) ||
        (NULL == pQuit) ||
        (NULL == pImageEncoderParams->pReturnUncompressedBufferFunc) ||
        (NULL == pImageEncoderParams->pInputImageQueue)) {
        LOG_ERR("%s: Bad arguments\n", __func__);
        return NULL;
    }

    // create the imageEncoderContext
    pImageEncoderCtx =
            (NvImageEncoderContext *)calloc(1, sizeof(NvImageEncoderContext));

    if (NULL == pImageEncoderCtx) {
        LOG_ERR("%s: Failed to allocate image encoder context\n", __func__);
        return NULL;
    }

    pImageEncoderCtx->pQuit = pQuit;
    pImageEncoderCtx->pReturnUncompressedBufferFunc = pImageEncoderParams->pReturnUncompressedBufferFunc;

    // create the input image queue
    pImageEncoderCtx->pInputImageQueue = pImageEncoderParams->pInputImageQueue;

    // create the output queue
    status = NvQueueCreate(&pImageEncoderCtx->pOutputQueue,
                           IMAGE_ENCODER_OUTPUT_QUEUE_SIZE,
                           sizeof(EncodedBufferContainer *));

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: Failed to create output image queue\n", __func__);
        doHouseKeeping(pImageEncoderCtx);
        return NULL;
    }

    // create pEncodedBufferContainerQueue
    status = NvQueueCreate(&pImageEncoderCtx->pEncodedBufferContainerQueue,
                           IMAGE_ENCODER_BUFFER_CONATINER_QUEUE_SIZE,
                           sizeof(EncodedBufferContainer *));

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: Failed to create buffer container queue\n", __func__);
        doHouseKeeping(pImageEncoderCtx);
        return NULL;
    }

    pImageEncoderCtx->bufferContainerCount = 0;
    // create the individual buffer containers
    for (count = 0; count < IMAGE_ENCODER_BUFFER_CONATINER_QUEUE_SIZE; count++) {
        pEncodedBufferContainer = (EncodedBufferContainer *)calloc (1, sizeof(EncodedBufferContainer));

        if (NULL == pEncodedBufferContainer) {
            LOG_ERR("%s: Failed to allocate buffer container \n", __func__);
            doHouseKeeping(pImageEncoderCtx);
            return NULL;
        }

        pEncodedBufferContainer->pImageEncoderCtx = pImageEncoderCtx;
        pEncodedBufferContainer->pPutBufferFunc = &_putBufferContainer;
        status = _putBufferContainer(pEncodedBufferContainer);

        if (NVMEDIA_STATUS_OK != status) {
            LOG_ERR("%s: Failed to queue buffer container \n", __func__);

            // free pEncodedBufferContainer, it did not make its ways into
            // buffer container queue
            free (pEncodedBufferContainer);
            doHouseKeeping(pImageEncoderCtx);
            return NULL;
        }

        pImageEncoderCtx->bufferContainerCount += 1;
    }

    // create NvMedia device
    pImageEncoderCtx->device = NvMediaDeviceCreate();

    if (NULL == pImageEncoderCtx->device) {
        LOG_ERR("%s: failed to create NvMedia device context for encoder\n",
                __func__);
        doHouseKeeping(pImageEncoderCtx);
        return NULL;
    }

    // create an instance of image encoder
    LOG_INFO("%s: videoCodec = %d, inputFormat = %d, maxInputBuffering = %d, maxOutputBuffering = %d\n",
            __func__,
            pImageEncoderParams->videoCodec,
            pImageEncoderParams->inputFormat,
            pImageEncoderParams->maxInputBuffering,
            pImageEncoderParams->maxOutputBuffering);

    LOG_INFO("%s: profile = %d, level = %d, maxNumRefFrames = %d\n",
            __func__,
            pImageEncoderParams->encoderInitParams.encoderInitParamsH264.profile,
            pImageEncoderParams->encoderInitParams.encoderInitParamsH264.level,
            pImageEncoderParams->encoderInitParams.encoderInitParamsH264.maxNumRefFrames);

    pImageEncoderCtx->pImageEncoder =
            NvMediaIEPCreate(pImageEncoderCtx->device,                              // device
                             pImageEncoderParams->videoCodec,                       // codec
                             (void *)(&pImageEncoderParams->encoderInitParams),     // init params
                             pImageEncoderParams->inputFormat,                      // inputFormat
                             pImageEncoderParams->maxInputBuffering,                // maxInputBuffering
                             pImageEncoderParams->maxOutputBuffering,               // maxOutputBuffering
                             NULL);                                                 // device

    if (NULL == pImageEncoderCtx->pImageEncoder) {
        LOG_ERR("%s: failed to create NvMedia encoder instance\n",
                __func__);
        doHouseKeeping(pImageEncoderCtx);
        return NULL;
    }

    LOG_INFO("%s: features = %d, gopLength = %d, rateControlMode = %d, numBFrames = %d, repeatSPSPPS = %d, idrPeriod = %d\n",
            __func__,
            pImageEncoderParams->encodeConfig.encodeConfigParamsH264.features,
            pImageEncoderParams->encodeConfig.encodeConfigParamsH264.gopLength,
            pImageEncoderParams->encodeConfig.encodeConfigParamsH264.rcParams.rateControlMode,
            pImageEncoderParams->encodeConfig.encodeConfigParamsH264.rcParams.numBFrames,
            pImageEncoderParams->encodeConfig.encodeConfigParamsH264.repeatSPSPPS,
            pImageEncoderParams->encodeConfig.encodeConfigParamsH264.idrPeriod);
    // set the encoder configuration
    status = NvMediaIEPSetConfiguration(pImageEncoderCtx->pImageEncoder,
                                        (void *)(&pImageEncoderParams->encodeConfig));

    if (NVMEDIA_STATUS_OK != status) {
        LOG_ERR("%s: failed to set encoder configuration\n",
                __func__);
        doHouseKeeping(pImageEncoderCtx);
        return NULL;
    }


    // keep the source rectangle and encodePicParams for future use
    memcpy((void *)&pImageEncoderCtx->sourceRect,
           (void *)&pImageEncoderParams->sourceRect,
           sizeof(NvMediaRect));

    // if the host wants to collect the reference to the output queue,
    // send it out
    if (NULL != pOutputQueueRef) {
        *pOutputQueueRef = pImageEncoderCtx->pOutputQueue;
    }

    return (void *)pImageEncoderCtx;
}

void
ImageEncoderStop(void *pHandle)
{
    NvImageEncoderContext *pImageEncoderCtx = NULL;

    if (NULL == pHandle) {
        return;
    }

    pImageEncoderCtx = (NvImageEncoderContext *)pHandle;

    _destroyThread(pImageEncoderCtx);

    return;
}

NvMediaStatus
ImageEncoderFini(void *pHandle)
{
    NvImageEncoderContext   *pImageEncoderCtx = NULL;

    if (NULL == pHandle) {
        return NVMEDIA_STATUS_OK;
    }

    pImageEncoderCtx = (NvImageEncoderContext *)pHandle;

    if ((NULL != pImageEncoderCtx->pImageEncoderFeedThread) ||
        (NULL != pImageEncoderCtx->pImageEncoderOutputThread)) {
        LOG_ERR("%s: Encoder threads still active, invoke ImageEncoderStop first\n",
                        __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    doHouseKeeping(pImageEncoderCtx);

    LOG_MSG("%s: done, uncompressed frame count = %d, encoded frame count = %d\n",
            __func__,
            pImageEncoderCtx->frameCount,
            pImageEncoderCtx->encodedFrameCount);

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ImageEncoderStart(void *pHandle)
{
    NvMediaStatus           status;
    NvImageEncoderContext   *pImageEncoderCtx = NULL;

    if (NULL == pHandle) {
        LOG_ERR("%s: Bad arguments\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pImageEncoderCtx = (NvImageEncoderContext *)pHandle;

    /* Create image encoder thread */
    status = NvThreadCreate(&pImageEncoderCtx->pImageEncoderFeedThread,
                            &_ImageEncoderFeedThreadFunc,
                            (void *)pImageEncoderCtx,
                            NV_THREAD_PRIORITY_NORMAL);

    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create image encoder thread\n",
                __func__);
        return status;
    }

    /* Create image output thread */
    status = NvThreadCreate(&pImageEncoderCtx->pImageEncoderOutputThread,
                            &_ImageEncoderOutputThreadFunc,
                            (void *)pImageEncoderCtx,
                            NV_THREAD_PRIORITY_NORMAL);

    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to create image output thread\n",
                __func__);

        return status;
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ImageEncoderRegisterRunTimePicParamsCallback(
        void                        *pHandle,
        RunTimePicParamsCallback    pGetRunTimePicParams,
        void                        *pHostContext)
{
    NvImageEncoderContext   *pImageEncoderCtx = NULL;

    if ((NULL == pHandle) ||
        (NULL == pGetRunTimePicParams)) {
        LOG_ERR("%s: Bad parameters\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pImageEncoderCtx = (NvImageEncoderContext *)pHandle;
    pImageEncoderCtx->pGetRunTimePicParams = pGetRunTimePicParams;
    pImageEncoderCtx->pHostContext = pHostContext;

    return NVMEDIA_STATUS_OK;
}
