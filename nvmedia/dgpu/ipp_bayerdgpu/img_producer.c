/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "img_producer.h"
#include "buffer_utils.h"
#include "eglstrm_setup.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif


NvMediaStatus
ImageProducerProc (
    void *data,
    NvMediaImage *image,
    NvMediaImage **retimage,
    NvU32 ippNum)
{
    ImageProducerCtx *ctx = (ImageProducerCtx *)data;
    NvMediaStatus status, getimagestatus;
    NvU32 timeoutMS = EGL_PRODUCER_TIMEOUT_MS;
    int retry = EGL_PRODUCER_GET_IMAGE_MAX_RETRIES;
    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

#ifdef EGL_NV_stream_metadata
    if(ctx->checkcrc) {
        NvU32 crc = 0;
        int blockIdx = 0;
        NvU32 rawBytesPerPixel;
        //Posting  metadata on the stream
        rawBytesPerPixel = 2;
        GetImageCrc(image, ctx->width, ctx->height, &crc, rawBytesPerPixel);
        status = NvMediaEglStreamProducerPostMetaData(
                           ctx->eglProducer[ippNum],
                           blockIdx,            //blockIdx
                           (void *)(&crc),      //dataBuf
                           0,                   //offset
                           4);                  //size
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: NvMediaEglStreamProducerPostMetaData failed, blockIdx %d\n", __func__, blockIdx);
            return status;
        }
    }
#endif //EGL_NV_stream_metadata

    if(IsFailed((status = NvMediaEglStreamProducerPostImage(ctx->eglProducer[ippNum],
                                                  image, NULL)))) {
        LOG_ERR("%s: NvMediaEglStreamProducerPostImage failed\n", __func__);
        return  status;
    }

    // The first ProducerGetImage() has to happen
    // after the second ProducerPostImage()
    if(!ctx->eglProducerGetImageFlag[ippNum]) {
        ctx->eglProducerGetImageFlag[ippNum] = NVMEDIA_TRUE;
        return status;
    }

    // get image from eglstream and release it
    do {
         getimagestatus = NvMediaEglStreamProducerGetImage(ctx->eglProducer[ippNum],
                                                  retimage,
                                                  timeoutMS);
         if(getimagestatus == NVMEDIA_STATUS_BAD_PARAMETER) {
             LOG_ERR("%s: Bad parameters to NvMediaEglStreamProducerGetImage, \
                  producer %p, &image %p\n", __func__, ctx->eglProducer[ippNum], retimage);
             break;
         }
        retry--;
    } while(retry >= 0 && !retimage && !(*(ctx->quit)));


    return status;
}

ImageProducerCtx*
ImageProducerInit(NvMediaDevice *device,
                  EglStreamClient *streamClient,
                  NvU32 width, NvU32 height,
                  InteropContext *interopCtx,
                  TestArgs *args)
{
    NvU32 i;
    ImageProducerCtx *client = NULL;

    if(!device) {
        LOG_ERR("%s: invalid NvMedia device\n", __func__);
        return NULL;
    }

    client = malloc(sizeof(ImageProducerCtx));
    if (!client) {
        LOG_ERR("%s:: failed to alloc memory\n", __func__);
        return NULL;
    }
    memset(client, 0, sizeof(ImageProducerCtx));

    client->device = device;
    client->width = width;
    client->height = height;
    client->ippNum = interopCtx->ippNum;
    client->surfaceType = interopCtx->eglProdSurfaceType;
    client->eglDisplay = streamClient->display;
    client->producerExited = &interopCtx->producerExited;
    client->quit = interopCtx->quit;
    client->showTimeStamp = interopCtx->showTimeStamp;
    client->showMetadataFlag = interopCtx->showMetadataFlag;
    client->checkcrc = args->checkcrc;
    for(i=0; i< interopCtx->ippNum; i++) {
        client->outputComponent[i] = interopCtx->outputComponent[i];
        // Create EGL stream producer
        EGLint streamState = 0;
        client->eglStream[i]   = streamClient->eglStream[i];
        while(streamState != EGL_STREAM_STATE_CONNECTING_KHR) {
           if(!eglQueryStreamKHR(streamClient->display,
                                 streamClient->eglStream[i],
                                 EGL_STREAM_STATE_KHR,
                                 &streamState)) {
               LOG_ERR("eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
            }
        }

        client->eglProducer[i] = NvMediaEglStreamProducerCreate(client->device,
                                                                client->eglDisplay,
                                                                client->eglStream[i],
                                                                client->surfaceType,
                                                                client->width,
                                                                client->height);
        if(!client->eglProducer[i]) {
            LOG_ERR("%s: Failed to create EGL producer\n", __func__);
            goto fail;
        }
    }
    return client;
fail:
    ImageProducerFini(client);
    return NULL;
}

NvMediaStatus ImageProducerFini(ImageProducerCtx *ctx)
{
    NvU32 i;
    NvMediaImage *retImage = NULL;
    NvMediaIPPComponentOutput output;
    LOG_DBG("ImageProducerFini: start\n");
    if(ctx) {
        for(i = 0; i < ctx->ippNum; i++) {
            // Finalize
            do {
                retImage = NULL;
                NvMediaEglStreamProducerGetImage(ctx->eglProducer[i],
                                                 &retImage,
                                                 0);
                if(retImage) {
                    LOG_DBG("%s: EGL producer: Got image %p outcomp %p\n", __func__, retImage, ctx->outputComponent[i]);
                    output.image = retImage;
                    NvMediaIPPComponentReturnOutput(ctx->outputComponent[i], //component
                                                        &output);                //output image
                }
            } while(retImage);
        }

        for(i=0; i<ctx->ippNum; i++) {
            if(ctx->eglProducer[i])
                NvMediaEglStreamProducerDestroy(ctx->eglProducer[i]);
        }
        free(ctx);
    }
    LOG_DBG("ImageProducerFini: end\n");
    return NVMEDIA_STATUS_OK;
}
