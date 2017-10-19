/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <string.h>

#include "capture.h"
#include "interop.h"
#include "img_producer.h"

static void
*ProcessImageThread(void * args)
{
    ImageProducerTestArgs *imgProducer = (ImageProducerTestArgs *)args;
    NvMediaImage *inputImage = NULL;
    NvMediaImage *releaseImage = NULL;
    NvMediaStatus status;
    NvU32 readFrame = 0;
    NvMediaTime timeStamp ={0, 0};

    while(!(*imgProducer->quit)) {

        /* acquire Image from inputQueue */
        if(IsFailed(NvQueueGet(imgProducer->inputQueue,
                               (void *)&inputImage,
                               100))) {
            LOG_INFO("%s: Image Producer input queue empty\n", __func__);
            goto getImage;
        }

        /* read Image into inputImageBuffer */
        if((imgProducer->frameCount && (readFrame+1) > imgProducer->frameCount) ) {
            LOG_DBG("Image Producer read image done\n");
            goto done;
        }

        LOG_DBG("Image Producer: Reading image %u\n", readFrame+1);

        readFrame++;
        //WriteImage("producerimage.yuv",inputImage, NVMEDIA_TRUE, NVMEDIA_TRUE, 1);

        /* post image to egl-stream */
        status = NvMediaEglStreamProducerPostImage(imgProducer->producer,
                                                   inputImage,
                                                   &timeStamp);

        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("Image Producer: NvMediaEglStreamProducerPostImage failed, status=%d\n", status);
            goto done;
        }

        getImage:
        /* Get back from the egl-stream */
        status = NvMediaEglStreamProducerGetImage(imgProducer->producer,
                                                  &releaseImage,
                                                  100);
        if (status == NVMEDIA_STATUS_OK) {
            NvQueuePut((NvQueue *)releaseImage->tag,
                               (void *)&releaseImage,
                               ENQUEUE_TIMEOUT);
        } else {
            LOG_DBG ("Image Producer: NvMediaEglStreamProducerGetImage waiting\n");
            continue;
        }
    }
done:
    *imgProducer->producerStop = NV_TRUE;
    return NULL;
}

int
ImageProducerInit(volatile NvBool *imageProducerStop,
                  ImageProducerTestArgs *imgProducer,
                  EglStreamClient *streamClient,
                  InteropArgs *args)
{

    memset(imgProducer, 0, sizeof(ImageProducerTestArgs));
    imgProducer->inputImages = NULL;
    imgProducer->width = args->width;
    imgProducer->height = args->height;

    imgProducer->frameCount  = args->frameCount;
    imgProducer->surfaceType = args->prodSurfaceType;
    imgProducer->eglDisplay  = streamClient->display;
    imgProducer->eglStream   = streamClient->eglStream;
    imgProducer->producerStop = imageProducerStop;
    imgProducer->quit = args->quit;
    imgProducer->device = args->device;

    if(!imgProducer->device) {
        LOG_ERR("%s: Failed to create NvMedia device\n", __func__);
        return 0;
    }

    imgProducer->inputQueue = args->inputQueue;

    /* create EGLStream-Producer */
    imgProducer->producer = NvMediaEglStreamProducerCreate(imgProducer->device,
                                                        imgProducer->eglDisplay,
                                                        imgProducer->eglStream,
                                                        imgProducer->surfaceType,
                                                        imgProducer->width,
                                                        imgProducer->height);
    if(!imgProducer->producer) {
        LOG_ERR("ImageProducerInit: Unable to create producer\n");
        return 0;
    }

    ProcessImageThread((void *)imgProducer);

    return 1;
}


void ImageProducerFini(ImageProducerTestArgs *imgProducer)
{
    LOG_DBG("ImageProducerFini: start\n");

    if(imgProducer->producer) {
       NvMediaEglStreamProducerDestroy(imgProducer->producer);
       imgProducer->producer = NULL;
    }

    LOG_DBG("ImageProducerFini: end\n");
}

void ImageProducerFlush(ImageProducerTestArgs *imgProducer)
{
    /* image producer to get the image back from EGLStream, and release them */
    NvMediaImage *releaseImage = NULL;

    while (NvMediaEglStreamProducerGetImage(
           imgProducer->producer, &releaseImage, 0) == NVMEDIA_STATUS_OK) {
           NvQueuePut((NvQueue *)releaseImage->tag,
                   (void *)&releaseImage,
                   ENQUEUE_TIMEOUT);
    }
}
