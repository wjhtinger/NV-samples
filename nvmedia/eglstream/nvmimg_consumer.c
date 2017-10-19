/*
 * nvmimage_consumer.c
 *
 * Copyright (c) 2013-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple image consumer rendering sample app
//

#include "nvm_consumer.h"
#include "log_utils.h"

#define IMAGE_BUFFERS_POOL_SIZE      4
#define BUFFER_POOL_TIMEOUT          100

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

static NvU32
procThreadFunc (
    void *data)
{
    test_nvmedia_consumer_display_s *display = (test_nvmedia_consumer_display_s *)data;
    NvMediaTime timeStamp;
    NvMediaImage *image = NULL;
    NvMediaImage *releaseFrames[IMAGE_BUFFERS_POOL_SIZE] = {0};
    NvMediaImage **releaseList = &releaseFrames[0];

    if(!display) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return 0;
    }

    LOG_DBG("NVMedia image consumer thread is active\n");

    while(!display->quit) {
        EGLint streamState = 0;
        if(!eglQueryStreamKHR(
                display->eglDisplay,
                display->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("Nvmedia image consumer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }

        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            LOG_DBG("Nvmedia image Consumer: - EGL_STREAM_STATE_DISCONNECTED_KHR received\n");
            display->quit = NV_TRUE;
            goto done;
        }
        if(streamState != EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
            usleep(1000);
            continue;
        }

        if(NVMEDIA_STATUS_ERROR ==
                NvMediaEglStreamConsumerAcquireImage(display->consumer, &image, 16, &timeStamp)) {
            LOG_DBG("Nvmedia image Consumer: - image acquire failed\n");
            display->quit = NV_TRUE;
            goto done;
        }

#ifdef EGL_NV_stream_metadata
        //Query the metaData
        if(display->metadataEnable && image) {
            unsigned char buf[256] = {0};
            static unsigned char frameId = 0;
            int blockIdx = 0;
            NvMediaStatus status;

            for (blockIdx=0; blockIdx<4; blockIdx++) {
                memset(buf, 0xff, 256);

                status = NvMediaEglStreamConsumerAcquireMetaData(
                               display->consumer,
                               blockIdx,            //blockIdx
                               (void *)buf,         //dataBuf
                               blockIdx*16,         //offset
                               256);                //size
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: NvMediaEglStreamConsumerAcquireMetaData failed, blockIdx %d\n", __func__, blockIdx);
                    display->quit = NV_TRUE;
                    goto done;
                }

                //Matching the metaData frameId
                if (buf[0] != frameId) {
                    LOG_ERR("%s: NvMediaEglStreamConsumerAcquireMetaData frameId %d, expected %d\n", __func__, buf[0], frameId);
                } else if (buf[1] != blockIdx) {
                    LOG_ERR("%s: NvMediaEglStreamConsumerAcquireMetaData blockIdx %d, expected %d\n", __func__, buf[1], blockIdx);
                } else {
                    LOG_DBG("Nvmedia image Consumer: metaData frame matching\n");
                }
            }
            frameId ++;
            if (frameId == 255)
                frameId =0;
        }
#endif //EGL_NV_stream_metadata

        if (image) {
            ImageBuffer *outputImageBuffer = NULL;
            //Create output buffer pool for image 2d blit for vertical flip
            if (display->outputBuffersPool == NULL) {
                ImageBufferPoolConfig imagesPoolConfig;
                memset(&imagesPoolConfig, 0, sizeof(ImageBufferPoolConfig));
                imagesPoolConfig.width = image->width;
                imagesPoolConfig.height = image->height;
                imagesPoolConfig.surfType = image->type; //args.consSurfaceType;
                imagesPoolConfig.imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
                imagesPoolConfig.imagesCount = 1;
                imagesPoolConfig.device = display->device;
                imagesPoolConfig.surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_DISPLAY;
                if(IsFailed(BufferPool_Create(&display->outputBuffersPool,    // Buffer pool
                                                     IMAGE_BUFFERS_POOL_SIZE, // Capacity
                                                     BUFFER_POOL_TIMEOUT,     // Timeout
                                                     IMAGE_BUFFER_POOL,       // Buffer pool type
                                                     &imagesPoolConfig))) {
                     LOG_ERR("%s: Create output buffer pool failed \n",__func__);
                     goto done;
                }
            }
            if(IsFailed(BufferPool_AcquireBuffer(display->outputBuffersPool,
                                                 (void *)&outputImageBuffer))){
                LOG_ERR("%s: Output BufferPool_AcquireBuffer failed \n",__func__);
                goto done;
            }

            if(IsFailed(NvMedia2DBlit(display->blitter,
                                      outputImageBuffer->image,
                                      NULL,
                                      image,
                                      NULL,
                                      display->blitParams))) {
                LOG_ERR("%s:  output image 2DBlit failed \n",__func__);
                goto done;
            }

            releaseList = &releaseFrames[0];
            NvMediaIDPFlip(display->imageDisplay,    // display
                           outputImageBuffer->image, // image
                           NULL,                     // source rectangle
                           NULL,                     // destination rectangle
                           releaseList,              // release list
                           &timeStamp);                    // time stamp
            NvMediaEglStreamConsumerReleaseImage(display->consumer, image);
            while (*releaseList) {
                ImageBuffer *buffer;
                buffer = (*releaseList)->tag;
                BufferPool_ReleaseBuffer(buffer->bufferPool, buffer);
                *releaseList = NULL;
                releaseList++;
            }
        }
    }
done:
    display->procThreadExited = NV_TRUE;
    *display->consumerDone = NV_TRUE;
    return 0;
}

//Image display init
int image_display_init(volatile NvBool *consumerDone,
                       test_nvmedia_consumer_display_s *display,
                       EGLDisplay eglDisplay, EGLStreamKHR eglStream,
                       TestArgs *args)
{
    NvMediaIDPDeviceParams outputs[MAX_OUTPUT_DEVICES];
    int outputDevicesNum = 0, i;

    display->consumerDone = consumerDone;
    display->metadataEnable = args->metadata;
    display->eglDisplay = eglDisplay;
    display->eglStream = eglStream;
    LOG_DBG("image_display_init: NvMediaDeviceCreate\n");
    display->device = NvMediaDeviceCreate();
    if (!display->device) {
        LOG_DBG("image_display_init: Unable to create device\n");
        return NV_FALSE;
    }

    if(IsFailed(NvMediaIDPQuery(&outputDevicesNum, outputs))) {
       LOG_DBG("%s: Failed querying for available displays\n", __func__);
       return NV_FALSE;
    }

    if (args->consSurfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin)
        args->consSurfaceType = NvMediaSurfaceType_Image_RGBA;
    else if (args->consSurfaceType == NvMediaSurfaceType_Video_420)
        args->consSurfaceType = NvMediaSurfaceType_Image_YUV_420;
    for(i = 0; i < outputDevicesNum; i++) {
       if(outputs[i].displayId == args->displayId) {
          LOG_DBG("image_display_init: IDP create 0\n");
          display->imageDisplay = NvMediaIDPCreate(args->displayId,      // display ID
                                                   args->windowId,       // window ID
                                                   args->consSurfaceType,// output surface type
                                                   NULL,                 // display preferences
                                                   outputs[i].enabled);  // already created flag
          break;
       }
    }

    if(!display->imageDisplay) {
        LOG_DBG("%s: Failed to create image display\n", __func__);
        return NV_FALSE;;
    }

    NvMediaIDPSetDepth(display->imageDisplay, 1); //Using default depth value: 1

    //image 2D
    display->blitter = NvMedia2DCreate(display->device);
    if(!display->blitter) {
        LOG_DBG("%s: Failed to create NvMedia 2D blitter\n", __func__);
        return NV_FALSE;;
    }

    display->blitParams = calloc(1, sizeof(NvMedia2DBlitParameters));
    if(!display->blitParams) {
        LOG_DBG("%s: Out of memory", __func__);
        return NV_FALSE;;
    }

    if(args->consSurfaceType == NvMediaSurfaceType_Image_YUV_420) {
        display->blitParams->dstTransform = 0;
    } else {
        display->blitParams->dstTransform = NVMEDIA_2D_TRANSFORM_FLIP_VERTICAL;
        display->blitParams->validFields |= NVMEDIA_2D_BLIT_PARAMS_DST_TRANSFORM;
    }

    display->outputBuffersPool = NULL;
    display->consumer = NvMediaEglStreamConsumerCreate(
        display->device,
        display->eglDisplay,
        display->eglStream,
        args->consSurfaceType);
    if(!display->consumer) {
        LOG_DBG("image_display_init: Unable to create consumer\n");
        return NV_FALSE;
    }

    if (IsFailed(NvThreadCreate(&display->procThread, &procThreadFunc, (void *)display, NV_THREAD_PRIORITY_NORMAL))) {
        LOG_ERR("Nvmedia image consumer init: Unable to create process thread\n");
        display->procThreadExited = NV_TRUE;
        return NV_FALSE;
    }
    return NV_TRUE;
}

void image_display_Deinit(test_nvmedia_consumer_display_s *display)
{
    LOG_DBG("image_display_Deinit: start\n");
    display->quit = NV_TRUE;

    //Close IDP
    if(display && display->imageDisplay) {
        NvMediaIDPDestroy(display->imageDisplay);
        display->imageDisplay = NULL;
    }

   if(display->blitter)
        NvMedia2DDestroy(display->blitter);

    if(display->blitParams) {
        free(display->blitParams);
        display->blitParams = NULL;
    }

    if(display->outputBuffersPool)
        BufferPool_Destroy(display->outputBuffersPool);

    if(display->consumer) {
        NvMediaEglStreamConsumerDestroy(display->consumer);
        display->consumer = NULL;
    }

    LOG_DBG("image_display_Deinit: consumer destroy\n");
    if(display->device) {
        NvMediaDeviceDestroy(display->device);
        display->device = NULL;
    }
    LOG_DBG("image_display_Deinit: done\n");
}

void image_display_Stop(test_nvmedia_consumer_display_s *display) {
    display->quit = 1;
    if (display->procThread) {
        LOG_DBG("wait for nvmedia image consumer thread exit\n");
        NvThreadDestroy(display->procThread);
    }
}

void image_display_Flush(test_nvmedia_consumer_display_s *display) {
    NvMediaImage *image = NULL;
    NvMediaTime timeStamp;
    NvMediaImage *releaseFrames[IMAGE_BUFFERS_POOL_SIZE] = {0};
    NvMediaImage **releaseList = &releaseFrames[0];
    EGLint streamState = 0;

    do {
        if(!eglQueryStreamKHR(
                display->eglDisplay,
                display->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("Nvmedia video consumer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }

        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            break;
        }

        NvMediaEglStreamConsumerAcquireImage(display->consumer,
                                             &image,
                                             16,
                                             &timeStamp);
        if(image) {
            LOG_DBG("%s: EGL Consumer: Release image %p (display->consumer)\n", __func__, image);
            NvMediaEglStreamConsumerReleaseImage(display->consumer, image);
        }
    } while(image);

    releaseList = &releaseFrames[0];
    NvMediaIDPFlip(display->imageDisplay,   // display
                   NULL,                    // image
                   NULL,                    // source rectangle
                   NULL,                    // destination rectangle,
                   releaseList,             // release list
                   NULL);                   // time stamp
    while (*releaseList) {
        ImageBuffer *buffer;
        buffer = (*releaseList)->tag;
        BufferPool_ReleaseBuffer(buffer->bufferPool, buffer);
        *releaseList = NULL;
        releaseList++;
    }
}
