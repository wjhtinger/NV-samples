/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <limits.h>
#include <math.h>

#include "capture.h"
#include "process2d.h"

NvMediaStatus CaptureInit(NvMainContext *mainCtx)
{
    NvCaptureContext *captureCtx = NULL;
    UtilUsbSensorConfig *config = NULL;
    NvMediaImage *image = NULL;
    NvU32 i = 0;

    mainCtx->ctxs[USB_CAPTURE]= malloc(sizeof(NvCaptureContext));
    if(!mainCtx->ctxs[USB_CAPTURE]){
        LOG_ERR("%s: Failed to allocate memory for capture context\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    captureCtx = mainCtx->ctxs[USB_CAPTURE];
    memset(captureCtx,0,sizeof(NvCaptureContext));

    /* initialize capture context*/
    captureCtx->width = mainCtx->width;
    captureCtx->height = mainCtx->height;
    captureCtx->quit = &mainCtx->quit;

    /* initializing config for USB capture */
    config = &captureCtx->config;
    config->devPath = (mainCtx->devPath)? mainCtx->devPath: "/dev/video0";
    config->width   =  mainCtx->width;
    config->height  =  mainCtx->height;
    config->fmt     = V4L2_PIX_FMT_YUYV;

    captureCtx->captureDevice = UtilUsbSensorInit(config);
    if(!captureCtx->captureDevice) {
        LOG_ERR("%s: Failed to create image capture context\n", __func__);
        return NVMEDIA_STATUS_NOT_INITIALIZED;
    }

    /* get the actual width, height and format set by the usb camera driver */
    captureCtx->width  = config->width;
    captureCtx->height = config->height;

    /* set input formats */
    switch(config->fmt)
    {
        case V4L2_PIX_FMT_YUYV:
            strcpy(captureCtx->fmt,"yuyv");
            captureCtx->inputSurfType = NvMediaSurfaceType_Image_YUYV_422;
            break;
        case V4L2_PIX_FMT_YUV420:
            strcpy(captureCtx->fmt,"420p");
            captureCtx->inputSurfType = NvMediaSurfaceType_Image_YUV_420;
            captureCtx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_SEMI_PLANAR;
            break;
        default:
            LOG_ERR("Input feed from the camera: Unsupported format\n");
            return NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    /*flag to allocate capture surfaces*/
    captureCtx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_CAPTURE;

    /* update the width and height in the main context */
    mainCtx->width  = captureCtx->width;
    mainCtx->height = captureCtx->height;
    strcpy(mainCtx->inpFmt,captureCtx->fmt);

    /* create NvMedia device */
    captureCtx->device = NvMediaDeviceCreate();
    if(!captureCtx->device) {
        LOG_ERR("%s: Failed to create NvMedia device\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    /*Create Capture Input Queue*/
    if(IsFailed(NvQueueCreate(&captureCtx->inputQueue,
                              IMAGE_BUFFERS_POOL_SIZE,
                              sizeof(NvMediaImage *)))) {
        return NVMEDIA_STATUS_ERROR;
    }

    for(i = 0; i < IMAGE_BUFFERS_POOL_SIZE; i++) {
        image = NvMediaImageCreate(captureCtx->device,               // device
                                   captureCtx->inputSurfType,        // surface type
                                   NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE, // image class
                                   1,                                // images count
                                   captureCtx->width,                // surf width
                                   captureCtx->height,               // surf height
                                   captureCtx->inputSurfAttributes,  // attributes
                                   &captureCtx->inputSurfAdvConfig); // config
        if(!image) {
            return NVMEDIA_STATUS_ERROR;
        }
        image->tag = captureCtx->inputQueue;

        if(IsFailed(NvQueuePut(captureCtx->inputQueue,
                (void *)&image, NV_TIMEOUT_INFINITE)))
            return NVMEDIA_STATUS_ERROR;
    }

    LOG_DBG("%s: Capture Queue: %ux%u, images: %u \n",
        __func__, captureCtx->width, captureCtx->height,
        IMAGE_BUFFERS_POOL_SIZE);

    return NVMEDIA_STATUS_OK;
}


NvMediaStatus CaptureFini (NvMainContext *mainCtx)
{
    NvCaptureContext *captureCtx = mainCtx->ctxs[USB_CAPTURE];
    NvMediaImage *image = NULL;

    /*Flush the input queue*/
    if(captureCtx->inputQueue) {
        while(IsSucceed(NvQueueGet(captureCtx->inputQueue, &image, 0))) {
            if (image) {
                NvMediaImageDestroy(image);
                image = NULL;
            }
        }
        LOG_DBG("\n Destroying capture input queue");
        NvQueueDestroy(captureCtx->inputQueue);
    }

    if(captureCtx->captureDevice)
        UtilUsbSensorDeinit(captureCtx->captureDevice);

    if(captureCtx->device)
        NvMediaDeviceDestroy(captureCtx->device);

    if(captureCtx)
        free(captureCtx);

    return NVMEDIA_STATUS_OK;
}


void CaptureProc (void* data, void* user_data)
{
    NvMainContext *mainCtx        = (NvMainContext *)data;
    NvU32 framesCount = 0;

    if(!mainCtx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return;
    }

    NvCaptureContext *captureCtx  = mainCtx->ctxs[USB_CAPTURE];
    NvProcess2DContext *proc2dCtx = mainCtx->ctxs[PROCESS_2D];
    NvMediaImage *capturedImage = NULL;

    /* setting the queues */
    captureCtx->outputQueue = proc2dCtx->inputQueue;

    /* start capture */
    LOG_INFO("Capture thread is active\n");
    if(UtilUsbSensorStartCapture(captureCtx->captureDevice)) {
        LOG_ERR("%s: Start Capture failed\n", __func__);
        *captureCtx->quit = NVMEDIA_TRUE;
    }

    while(!(*captureCtx->quit)) {
        /* get capture image from input queue */
        if(IsFailed(NvQueueGet(captureCtx->inputQueue,
                               (void *)&capturedImage,
                               DEQUEUE_TIMEOUT))) {
            LOG_ERR("%s: Capture input queue empty\n", __func__);
            goto loop_done;
        }

        /* read image from USB camera sensor */
        if(UtilUsbSensorGetFrame(captureCtx->captureDevice,  // capture context
                                  capturedImage,             // image buffer
                                  GET_FRAME_TIMEOUT))        // timeout
        {
            LOG_ERR("%s: UtilUsbSensorGetFrame failed\n", __func__);
            *captureCtx->quit = NVMEDIA_TRUE;
            goto loop_done;
        }
        LOG_DBG("%s: Captured image %u\n", __func__, framesCount+1);

        /* push captured frame on the output queue */
        if(IsFailed(NvQueuePut(captureCtx->outputQueue,
                               (void *)&capturedImage,
                               ENQUEUE_TIMEOUT))) {
            LOG_INFO("%s: Capture output queue full\n", __func__);
            goto loop_done;
        }
        capturedImage = NULL;
        framesCount++;

    loop_done:
        if(capturedImage) {
            NvQueuePut((NvQueue *)capturedImage->tag,
                               (void *)&capturedImage,
                               ENQUEUE_TIMEOUT);
        }
    }

    /* stop capture */
    if(UtilUsbSensorStopCapture(captureCtx->captureDevice)){
        LOG_ERR("%s: Stop Capture failed\n", __func__);
    }

    *captureCtx->quit = NV_TRUE;
    mainCtx->threadsExited[USB_CAPTURE] = NV_TRUE;
    LOG_DBG("\n Capture thread  finished \n");
}
