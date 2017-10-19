/*
 * eglimageproducer.c
 *
 * Copyright (c) 2015-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple EGL stream producer app for Image2D
//

#include "nvmimg_producer.h"

extern NvBool signal_stop;
Image2DTestArgs g_testArgs;

// Set2DParams() is a helper function for setting
// the 2D processing parameters in the Image2DTestArgs
// structure with default settings
static NvMediaStatus
Set2DParams (Image2DTestArgs *args)
{
    NvMedia2DBlitParameters *blitParams = args->blitParams;

    //set default blit params
    blitParams->validFields = 0;
    blitParams->flags = 0;
    blitParams->filter = 1;     //NVMEDIA_2D_STRETCH_FILTER_OFF (Disable the horizontal and vertical filtering)
    blitParams->validFields |= blitParams->filter > 1 ?
                                NVMEDIA_2D_BLIT_PARAMS_FILTER :
                                0;
    blitParams->blend.func = 1; //NVMEDIA_2D_BLEND_FUNC_COPY. Blending is disabled
    blitParams->blend.perPixelAlpha = 0;  //NVMEDIA_2D_PER_PIXEL_ALPHA_DISABLED
    blitParams->blend.constantAlpha = 70; //Constant alpha value. ConstAlpha = alpha_value / 255
    blitParams->validFields |= blitParams->blend.func > 1 ?
                                NVMEDIA_2D_BLIT_PARAMS_BLEND :
                                0;
    blitParams->dstTransform = 6; //NVMEDIA_2D_TRANSFORM_FLIP_VERTICAL
    blitParams->validFields |= blitParams->dstTransform > 0 ?
                                NVMEDIA_2D_BLIT_PARAMS_DST_TRANSFORM :
                                0;
    blitParams->srcOverride.Override = 0; //NVMEDIA_2D_SRC_OVERRIDE_DISABLED (Source override disabled)
    blitParams->srcOverride.alpha = 0;    //Source override alpha value
    blitParams->validFields |= blitParams->srcOverride.Override > 0 ?
                                NVMEDIA_2D_BLIT_PARAMS_SRC_OVERRIDE :
                                0;
    blitParams->colorStandard = 0; //NVMEDIA_2D_COLOR_STANDARD_ITUR_BT_601
    blitParams->colorRange = 0;    //NVMEDIA_2D_COLOR_RANGE_STANDARD_TO_STANDARD

    SetRect(args->srcRect, 0, 0, args->inputWidth, args->inputHeight);

    SetRect(args->dstRect, 0, 0, args->outputWidth, args->outputHeight);

    return NVMEDIA_STATUS_OK;
}

static NvU32
GetFramesCount(Image2DTestArgs *ctx)
{
    NvU32 count = 0, minimalFramesCount = 0, imageSize = 0;
    FILE **inputFile = NULL;

    inputFile = calloc(1, sizeof(FILE *));
    if(!inputFile) {
        LOG_ERR("%s: Out of memory", __func__);
        return 0;
    }

    // Get input file frames count
    if(strstr(ctx->inputImages, ".png")) {
        minimalFramesCount = 1;
    } else {
        if(strstr(ctx->inputImages, ".rgba")) {
            imageSize = ctx->inputWidth * ctx->inputHeight *4;
        } else {
            switch(ctx->inputSurfType) {
                case NvMediaSurfaceType_Image_YUV_420:
                    imageSize =  ctx->inputWidth * ctx->inputHeight * 3 / 2;
                    break;
                case NvMediaSurfaceType_Image_Y10:
                    imageSize =  ctx->inputWidth * ctx->inputHeight * 2;
                    break;
                default:
                    LOG_ERR("%s: Invalid image surface type %u\n", __func__, ctx->inputSurfType);
                    goto done;
            }
        }
        if(imageSize == 0) {
            LOG_ERR("%s: Bad image parameters", __func__);
            minimalFramesCount = 0;
            goto done;
        }

        if(!(inputFile[0] = fopen(ctx->inputImages, "rb"))) {
            LOG_ERR("%s: Failed opening file %s", __func__,
                    ctx->inputImages);
            minimalFramesCount = 0;
            goto done;
        }

        fseek(inputFile[0], 0, SEEK_END);
        count = ftell(inputFile[0]) / imageSize;
        minimalFramesCount = count;

        fclose(inputFile[0]);
    }

done:
    if (inputFile) {
        free(inputFile);
        inputFile = NULL;
    }
    return minimalFramesCount;
}

static NvU32
ProcessImageThread(void * args)
{
    Image2DTestArgs *testArgs = (Image2DTestArgs *)args;
    ImageBuffer *inputImageBuffer = NULL;
    ImageBuffer *outputImageBuffer = NULL;
    ImageBuffer *releaseBuffer = NULL;
    NvMediaImage *releaseImage = NULL;
    NvMediaStatus status;
    NvU32 framesCount = 1;
    NvU32 readFrame = 0;
    char *inputFileName = NULL;
    NvMediaTime timeStamp ={0, 0};
    framesCount = GetFramesCount(testArgs);
    if(framesCount == 0) {
        LOG_ERR("%s: GetFramesCount() failed", __func__);
        goto done;
    }

    inputFileName = testArgs->inputImages;

    while(!signal_stop) {
        // Acquire Image from inputBufferpool
        if(IsFailed(BufferPool_AcquireBuffer(testArgs->inputBuffersPool,
                                             (void *)&inputImageBuffer))){
            LOG_DBG("%s: Input BufferPool_AcquireBuffer waiting \n", __func__);
            goto getImage;
        }

        // Acquire Image from outputBufferpool
        if(testArgs->outputSurfType == NvMediaSurfaceType_Image_RGBA &&
           IsFailed(BufferPool_AcquireBuffer(testArgs->outputBuffersPool,
                                            (void *)&outputImageBuffer))){
            BufferPool_ReleaseBuffer(inputImageBuffer->bufferPool,
                                     inputImageBuffer);
            LOG_DBG("%s: Output BufferPool_AcquireBuffer waiting \n",__func__);
            goto getImage;
        }

        if(readFrame >= framesCount && testArgs->loop) {
           readFrame = 0;
           testArgs->loop--;
           if (testArgs->loop == 0)
               goto done;
        }
        //Read Image into inputImageBuffer
        LOG_DBG("%s: Reading image %u/%u\n", __func__, readFrame + 1, framesCount);
        if(IsFailed(ReadImage(inputFileName,
                              readFrame,
                              testArgs->inputWidth,
                              testArgs->inputHeight,
                              inputImageBuffer->image,
                              NVMEDIA_TRUE,                 //inputUVOrderFlag,
                              1))) {                        //rawInputBytesPerPixel
            LOG_ERR(": Failed reading frame %u", readFrame);
            BufferPool_ReleaseBuffer(inputImageBuffer->bufferPool,
                                     inputImageBuffer);
            if(testArgs->outputSurfType == NvMediaSurfaceType_Image_RGBA) {
               BufferPool_ReleaseBuffer(outputImageBuffer->bufferPool, outputImageBuffer);
            }
            goto done;
        }
        readFrame++;
        if (testArgs->frameCount && readFrame > testArgs->frameCount)
            goto done;

        if (testArgs->outputSurfType == NvMediaSurfaceType_Image_RGBA) {
            //Blit operation (YUV-RGB conversion,2D_TRANSFORM_FLIP_VERTICAL)
            if(IsFailed(NvMedia2DBlit(testArgs->blitter,
                                      outputImageBuffer->image,
                                      testArgs->dstRect,
                                      inputImageBuffer->image,
                                      testArgs->srcRect,
                                      testArgs->blitParams))) {
                LOG_ERR("ProcessImageThread: NvMedia2DBlit failed\n");
                BufferPool_ReleaseBuffer(inputImageBuffer->bufferPool, inputImageBuffer);
                BufferPool_ReleaseBuffer(outputImageBuffer->bufferPool, outputImageBuffer);
                goto done;
            }
        }

#ifdef EGL_NV_stream_metadata
        //Test for metaData
        if(testArgs->metadataEnable) {
            unsigned char buf[256] = {0};
            static unsigned char frameId = 0;
            int blockIdx = 0;

            memset(buf, 16, 256);
            buf[0] = frameId;
            frameId ++;
            if (frameId == 255)
                frameId =0;

            for(blockIdx=0; blockIdx<4; blockIdx++) {
                buf[1] = blockIdx;
                status = NvMediaEglStreamProducerPostMetaData(
                               testArgs->producer,
                               blockIdx,            //blockIdx
                               (void *)buf,         //dataBuf
                               blockIdx*16,         //offset
                               256);                //size
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: NvMediaEglStreamProducerPostMetaData failed, blockIdx %d\n", __func__, blockIdx);
                    goto done;
                }
            }
        }
#endif //EGL_NV_stream_metadata

        // Post outputImage to egl-stream
        status = NvMediaEglStreamProducerPostImage(testArgs->producer,
                           testArgs->outputSurfType == NvMediaSurfaceType_Image_RGBA?
                           outputImageBuffer->image:inputImageBuffer->image,
                           &timeStamp);

        if (testArgs->outputSurfType == NvMediaSurfaceType_Image_RGBA) {
            //Release the inputImageBuffer back to the bufferpool
            BufferPool_ReleaseBuffer(inputImageBuffer->bufferPool, inputImageBuffer);
        }

        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: NvMediaEglStreamProducerPostImage failed\n", __func__);
            goto done;
        }

getImage:
        // Get back from the egl-stream
        status = NvMediaEglStreamProducerGetImage(testArgs->producer,
                                                  &releaseImage,
                                                  0);
        if (status == NVMEDIA_STATUS_OK) {
            //Return the image back to the bufferpool
            releaseBuffer = (ImageBuffer *)releaseImage->tag;
            BufferPool_ReleaseBuffer(releaseBuffer->bufferPool,
                                     releaseBuffer);
         } else {
            LOG_DBG ("%s: NvMediaEglStreamProducerGetImage waiting\n", __func__);
            continue;
         }

    }

done:
    *testArgs->producerStop = NV_TRUE;

    return 0;
}

static NvMediaStatus
Image2D_Init (Image2DTestArgs *testArgs, NvMediaSurfaceType outputSurfaceType)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    ImageBufferPoolConfig imagesPoolConfig;

    if(!testArgs) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    testArgs->device = NvMediaDeviceCreate();
    if(!testArgs->device) {
        LOG_ERR("%s: Failed to create NvMedia device\n", __func__);
        goto failed;
    }

    testArgs->blitter = NvMedia2DCreate(testArgs->device);
    if(!testArgs->blitter) {
        LOG_ERR("%s: Failed to create NvMedia 2D blitter\n", __func__);
        goto failed;
    }

    testArgs->blitParams = calloc(1, sizeof(NvMedia2DBlitParameters));
    if(!testArgs->blitParams) {
        LOG_ERR("%s: Out of memory", __func__);
        status = NVMEDIA_STATUS_OUT_OF_MEMORY;
        goto failed;
    }

    testArgs->srcRect = calloc(1, sizeof(NvMediaRect));
    testArgs->dstRect = calloc(1, sizeof(NvMediaRect));
    if(!testArgs->srcRect || !testArgs->dstRect) {
        LOG_ERR("%s: Out of memory", __func__);
        status = NVMEDIA_STATUS_OUT_OF_MEMORY;
        goto failed;
    }

    if (!testArgs->pitchLinearOutput) {
        LOG_DBG("Image producer block linear used\n");
        testArgs->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_UNMAPPED;  //block linear used
        testArgs->outputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_UNMAPPED; //block linear used
    } else {
        LOG_DBG("Image producer Pitch linear used\n");
    }
    memset(&imagesPoolConfig, 0, sizeof(ImageBufferPoolConfig));
    imagesPoolConfig.width = testArgs->inputWidth;
    imagesPoolConfig.height = testArgs->inputHeight;
    imagesPoolConfig.surfType = testArgs->inputSurfType;
    imagesPoolConfig.surfAttributes = testArgs->inputSurfAttributes |
                                      NVMEDIA_IMAGE_ATTRIBUTE_SEMI_PLANAR;
    imagesPoolConfig.surfAdvConfig = testArgs->inputSurfAdvConfig;
    imagesPoolConfig.imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
    imagesPoolConfig.imagesCount = 1;
    imagesPoolConfig.device = testArgs->device;
    if(testArgs->eglOutput)
        imagesPoolConfig.surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_DISPLAY;
    if(IsFailed(BufferPool_Create(&testArgs->inputBuffersPool, // Buffer pool
                                  IMAGE_BUFFERS_POOL_SIZE,     // Capacity
                                  BUFFER_POOL_TIMEOUT,         // Timeout
                                  IMAGE_BUFFER_POOL,           // Buffer pool type
                                  &imagesPoolConfig))) {       // Config
        LOG_ERR("Image2D_Init: BufferPool_Create failed");
        goto failed;
    }

    memset(&imagesPoolConfig, 0, sizeof(ImageBufferPoolConfig));
    imagesPoolConfig.width = testArgs->outputWidth;
    imagesPoolConfig.height = testArgs->outputHeight;
    imagesPoolConfig.surfType = outputSurfaceType;
    imagesPoolConfig.surfAttributes = testArgs->outputSurfAttributes;
    imagesPoolConfig.surfAdvConfig = testArgs->outputSurfAdvConfig;
    imagesPoolConfig.imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
    imagesPoolConfig.imagesCount = 1;
    imagesPoolConfig.device = testArgs->device;
    if(testArgs->eglOutput)
        imagesPoolConfig.surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_DISPLAY;
    if(IsFailed(BufferPool_Create(&testArgs->outputBuffersPool, // Buffer pool
                                  IMAGE_BUFFERS_POOL_SIZE,      // Capacity
                                  BUFFER_POOL_TIMEOUT,          // Timeout
                                  IMAGE_BUFFER_POOL,            // Buffer pool type
                                  &imagesPoolConfig))) {        // Config
        LOG_ERR("Image2D_Init: BufferPool_Create failed");
        goto failed;
    }

    if(IsFailed(Set2DParams(testArgs)))
        goto failed;

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed", __func__);
    Image2DDeinit();
    return status;
}

int
Image2DInit(volatile NvBool *imageProducerStop,
            EGLDisplay eglDisplay,
            EGLStreamKHR eglStream,
            TestArgs *args)
{
    Image2DTestArgs *testArgs = &g_testArgs;

    memset(testArgs, 0, sizeof(Image2DTestArgs));
    testArgs->inputImages = args->infile;
    testArgs->inputWidth = args->inputWidth;
    testArgs->inputHeight = args->inputHeight;
    switch(args->prodSurfaceType) {
    case NvMediaSurfaceType_Image_Y10:
        testArgs->inputSurfType = NvMediaSurfaceType_Image_Y10;
        break;
    default:
        testArgs->inputSurfType = NvMediaSurfaceType_Image_YUV_420;
    }

    //output dimension same as input dimension
    testArgs->outputWidth = args->inputWidth;
    testArgs->outputHeight = args->inputHeight;
    testArgs->outputSurfType = args->prodSurfaceType;
    testArgs->pitchLinearOutput = args->pitchLinearOutput;
    testArgs->loop              = args->prodLoop? args->prodLoop : 1;
    testArgs->frameCount        = args->prodFrameCount;
    testArgs->eglOutput         = args->egloutputConsumer ? 1 : 0;
    if (args->prodLoop)
        testArgs->frameCount = 0;  //Only use loop count if it is valid

    testArgs->eglDisplay = eglDisplay;
    testArgs->eglStream = eglStream;
    testArgs->producerStop = imageProducerStop;
    testArgs->metadataEnable = args->metadata;

    if(IsFailed(Image2D_Init(testArgs, args->prodSurfaceType))) {
        *testArgs->producerStop = 1;
        return 0;
    }

    // Create EGLStream-Producer
    testArgs->producer = NvMediaEglStreamProducerCreate(testArgs->device,
                                                        testArgs->eglDisplay,
                                                        testArgs->eglStream,
                                                        args->prodSurfaceType,
                                                        testArgs->outputWidth,
                                                        testArgs->outputHeight);
    if(!testArgs->producer) {
        LOG_ERR("Image2DInit: Unable to create producer\n");
        Image2DDeinit();
        return 0;
    }

    if (IsFailed(NvThreadCreate(&testArgs->thread, &ProcessImageThread, (void*)testArgs, NV_THREAD_PRIORITY_NORMAL))) {
        LOG_ERR("Image2DInit: Unable to create ProcessImageThread\n");
        return 0;
    }

    return 1;
}


void Image2DDeinit()
{
    Image2DTestArgs *testArgs = &g_testArgs;

    if(testArgs->inputBuffersPool)
        BufferPool_Destroy(testArgs->inputBuffersPool);

    if(testArgs->outputBuffersPool)
        BufferPool_Destroy(testArgs->outputBuffersPool);

    if(testArgs->blitter)
        NvMedia2DDestroy(testArgs->blitter);

    if(testArgs->dstRect) {
        free(testArgs->dstRect);
        testArgs->dstRect = NULL;
    }

    if(testArgs->srcRect) {
        free(testArgs->srcRect);
        testArgs->srcRect = NULL;
    }

    if(testArgs->blitParams) {
        free(testArgs->blitParams);
        testArgs->blitParams = NULL;
    }

    if(testArgs->producer) {
        NvMediaEglStreamProducerDestroy(testArgs->producer);
        testArgs->producer = NULL;
    }

    if (testArgs->device) {
        NvMediaDeviceDestroy(testArgs->device);
        testArgs->device = NULL;
    }
}

void Image2DproducerStop() {
    Image2DTestArgs *testArgs = &g_testArgs;

    LOG_DBG("Image2DDeinit, wait for thread stop\n");
    if(testArgs->thread) {
        NvThreadDestroy(testArgs->thread);
    }
    LOG_DBG("Image2DDeinit, thread stop\n");
}

void Image2DproducerFlush() {
    Image2DTestArgs *testArgs = &g_testArgs;
    NvMediaImage *releaseImage = NULL;
    ImageBuffer *releaseBuffer = NULL;

    while (NvMediaEglStreamProducerGetImage(
           testArgs->producer, &releaseImage, 0) == NVMEDIA_STATUS_OK) {
        releaseBuffer = (ImageBuffer *)releaseImage->tag;
        BufferPool_ReleaseBuffer(releaseBuffer->bufferPool,
                                 releaseBuffer);
    }
}
