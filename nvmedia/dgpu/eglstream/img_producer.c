/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <string.h>
#include "img_producer.h"

extern NvBool signal_stop;

static NvU32 GetFramesCount(char* fileName, int imageSize)
{
    NvU32 count = 0;
    FILE *inputFile = NULL;

    if(imageSize == 0) {
       LOG_ERR("%s: Bad image parameters", __func__);
       return 0;
    }

    if(!(inputFile = fopen(fileName, "rb"))) {
       LOG_ERR("%s: Failed opening file %s", __func__,
               fileName);
       goto done;
    }

    fseek(inputFile, 0, SEEK_END);
    count = ftell(inputFile) / imageSize;

    fclose(inputFile);

done:
    return count;
}

static void
*ProcessImageThread(void * args)
{
    ImageProducerCtx *imgProducer = (ImageProducerCtx *)args;
    NvMediaImage *inputImage = NULL;
    NvMediaImage *releaseImage = NULL;
    NvMediaStatus status;
    NvU32 readFrame = 0;
    NvU32 BytesPerPixel = 1;
    NvMediaTime timeStamp ={0, 0};
    NvMediaImage *filereadin = NULL;
    if(imgProducer->surfaceType == NvMediaSurfaceType_Image_RAW){
        BytesPerPixel = 2;
    }
    while(!signal_stop) {

        /* acquire Image from inputQueue */
        if(IsFailed(NvQueueGet(imgProducer->inputQueue,
                               (void *)&inputImage,
                               100))) {
            LOG_INFO("%s: Image Producer input queue empty\n", __func__);
            goto getImage;
        }

        //Read Image into input queue
        if((imgProducer->frameCount && (readFrame+1) > imgProducer->frameCount)) {
            LOG_DBG("Image Producer read image done\n");
            goto done;
        }

        LOG_DBG("Image Producer: Reading image %u/%u\n", readFrame+1, imgProducer->frameCount);
        filereadin = imgProducer->useblitpath ? imgProducer->fileimage :
                        inputImage;
        if(IsFailed(ReadImage(imgProducer->inpFileName,
                              readFrame,
                              imgProducer->width,
                              imgProducer->height,
                              filereadin,
                              NVMEDIA_TRUE,                 //inputUVOrderFlag,
                              BytesPerPixel))) {                        //rawInputBytesPerPixel
            LOG_ERR("Image Producer: Failed reading frame %u", readFrame);
            NvQueuePut((NvQueue *)inputImage->tag,
                       (void *)&inputImage,
                       ENQUEUE_TIMEOUT);
            goto done;
        }

        readFrame++;

        /* Test mode */
        if(imgProducer->testModeParams->isTestMode) {
            if(imgProducer->testModeParams->isGenCrc) {
                LOG_DBG("\n Calculating CRC %d\n\n",readFrame);
                NvU32 outcrc = 0;
                imgProducer->testModeParams->isCrcMatched = NV_TRUE;
                GetImageCrc(filereadin,imgProducer->width,imgProducer->height,&outcrc,1);
                fprintf(imgProducer->testModeParams->refCrcFile,"%8x\n",outcrc);
            }
        }

        if(imgProducer->useblitpath) {
            if(IsFailed(NvMedia2DBlit(imgProducer->blitter,
                                      inputImage,
                                      NULL,
                                      filereadin,
                                      NULL,
                                      NULL))) {
                LOG_ERR("%s: image 2DBlit failed \n",__func__);
                goto done;
            }
        }

        // Post image to egl-stream
        status = NvMediaEglStreamProducerPostImage(imgProducer->producer,
                                                   inputImage,
                                                   &timeStamp);

        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("Image Producer: NvMediaEglStreamProducerPostImage failed, status=%d\n", status);
            goto done;
        }

getImage:
        // Get back from the egl-stream
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
    usleep(2000000);
    *imgProducer->producerDone = NV_TRUE;

    return NULL;
}

int ImageProducerInit (volatile NvBool *producerDone,
                      ImageProducerCtx *imgProducer,
                      EglStreamClient *streamClient,
                      TestArgs *args)
{
    NvMediaImage *image = NULL;
    NvU32 surfAttributes = 0;
    NvMediaImageAdvancedConfig surfAdvConfig;
    NvU32 i = 0;
    NvU32 imageSize = 0;
    memset(imgProducer, 0, sizeof(ImageProducerCtx));
    memset(&surfAdvConfig, 0, sizeof(NvMediaImageAdvancedConfig));
    imgProducer->inpFileName    = args->inpFileName;
    imgProducer->width          = args->width;
    imgProducer->height         = args->height;
    imgProducer->frameCount     = args->frameCount;
    imgProducer->display        = streamClient->display;
    imgProducer->eglStream      = streamClient->eglStream;
    imgProducer->producerDone   = producerDone;
    imgProducer->testModeParams = &args->testModeParams;
    imgProducer->testModeParams->isCrcMatched = NV_FALSE;
    imgProducer->useblitpath = args->useblitpath;

    /* set producer surface Format*/
    if(args->imagetype == IMAGE_TYPE_RGBA) {
        imgProducer->surfaceType = NvMediaSurfaceType_Image_RGBA;

    } else if (args->imagetype == IMAGE_TYPE_RAW) {
        imgProducer->surfaceType = NvMediaSurfaceType_Image_RAW;
        surfAttributes = NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL | NVMEDIA_IMAGE_ATTRIBUTE_RAW_PIXEL_ORDER;
        surfAdvConfig.embeddedDataLinesTop = 0;
        surfAdvConfig.embeddedDataLinesBottom = 0;
        surfAdvConfig.pixelOrder = NVMEDIA_RAW_PIXEL_ORDER_GRBG;
        surfAdvConfig.bitsPerPixel =  NVMEDIA_BITS_PER_PIXEL_12;
        surfAdvConfig.extraMetaDataSize = 0;

    } else{
          imgProducer->surfaceType = NvMediaSurfaceType_Image_YUV_420;
          surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_SEMI_PLANAR;
    }

    if(!args->pitchLinearOutput){
        surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_UNMAPPED;  //block linear used
    }
    imgProducer->device = NvMediaDeviceCreate();
    if(!imgProducer->device) {
        LOG_ERR("%s: Failed to create NvMedia device\n", __func__);
        return 1;
    }

    if(imgProducer->useblitpath) {
        imgProducer->blitter = NvMedia2DCreate(imgProducer->device);
        if(!imgProducer->blitter) {
            LOG_ERR("%s: Failed to create NvMedia 2D blitter\n", __func__);
            return 1;
        }
    }

    /* get the total number of frames*/
    if(!imgProducer->frameCount) {
        switch(args->imagetype){
            case IMAGE_TYPE_RGBA :
                imageSize = imgProducer->width * imgProducer->height * 4;
                break;
            case IMAGE_TYPE_RAW :
                imageSize = imgProducer->width * imgProducer->height * 2;
                break;
            case IMAGE_TYPE_YUV420 :
                imageSize = imgProducer->width * imgProducer->height * 1.5;
                break;
            default :
                LOG_ERR("Invalid image type\n");
                return 1;
        }

       imgProducer->frameCount = GetFramesCount(imgProducer->inpFileName,imageSize);
    }
    LOG_DBG("\n Frame count = %d\n",imgProducer->frameCount);

    /* create Producer Input Queue*/
    if(IsFailed(NvQueueCreate(&imgProducer->inputQueue,
                              IMAGE_BUFFERS_POOL_SIZE,
                              sizeof(NvMediaImage *)))) {
        return 1;
    }

    for(i = 0; i < IMAGE_BUFFERS_POOL_SIZE; i++) {
        image = NvMediaImageCreate(imgProducer->device,               // device
                                   imgProducer->surfaceType,        // surface type
                                   NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE, // image class
                                   1,                                // images count
                                   imgProducer->width,                // surf width
                                   imgProducer->height,               // surf height
                                   surfAttributes,  // attributes
                                   &surfAdvConfig); // config
        if(!image) {
            return 1;
        }
        image->tag = imgProducer->inputQueue;

        if(IsFailed(NvQueuePut(imgProducer->inputQueue,
                (void *)&image, NV_TIMEOUT_INFINITE)))
            return 1;
    }

    //Create a NvMediaImage if Blit path has to be used
    if(imgProducer->useblitpath) {
        imgProducer->fileimage = NvMediaImageCreate(imgProducer->device,               // device
                                   imgProducer->surfaceType,        // surface type
                                   NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE, // image class
                                   1,                                // images count
                                   imgProducer->width,                // surf width
                                   imgProducer->height,               // surf height
                                   surfAttributes,  // attributes
                                   &surfAdvConfig); // config
        if(!imgProducer->fileimage) {
            LOG_ERR("Failed in creating NvMediaImage for File Read\n");
            return 1;
        }
    }
    // Create EGLStream-Producer
    imgProducer->producer = NvMediaEglStreamProducerCreate(imgProducer->device,
                                                           imgProducer->display,
                                                           imgProducer->eglStream,
                                                           imgProducer->surfaceType,
                                                           imgProducer->width,
                                                           imgProducer->height);
    if(!imgProducer->producer) {
        LOG_ERR("ImageProducerInit: Unable to create producer\n");
        return 1;
    }

    /* Test mode */
    if(imgProducer->testModeParams->isTestMode) {
        if(imgProducer->testModeParams->isGenCrc) {
            char str[128];
            strcpy(str,imgProducer->inpFileName);
            strcat(str,"_crc.txt");
            imgProducer->testModeParams->refCrcFile = fopen(str, "wb");
            if(!imgProducer->testModeParams->refCrcFile) {
                LOG_ERR("GenCrc: Error opening file: %s\n", str);
                return 1;
            }
        }
    }

    pthread_create(&imgProducer->procThread, NULL,
                   ProcessImageThread, (void *)imgProducer);
    if(!imgProducer->procThread) {
        LOG_ERR("ImageProducerInit: Unable to create ProcessImageThread\n");
        return 1;
    }

    return 0;
}


void ImageProducerFini(ImageProducerCtx *imgProducer)
{
    NvMediaImage *image = NULL;
    LOG_DBG("ImageProducerFini: start\n");
    /*Destroy the input queue*/
    if(imgProducer->inputQueue) {
        while(IsSucceed(NvQueueGet(imgProducer->inputQueue, &image, 0))) {
            if (image) {
                NvMediaImageDestroy(image);
                image = NULL;
            }
        }
        LOG_DBG("\n Destroying producer input queue");
        NvQueueDestroy(imgProducer->inputQueue);
    }

    if(imgProducer->useblitpath && imgProducer->fileimage) {
        NvMediaImageDestroy(imgProducer->fileimage);
        imgProducer->fileimage = NULL;
    }

    if(imgProducer->useblitpath && imgProducer->blitter) {
        NvMedia2DDestroy(imgProducer->blitter);
        imgProducer->blitter = NULL;
    }

    if(imgProducer->producer) {
       NvMediaEglStreamProducerDestroy(imgProducer->producer);
       imgProducer->producer = NULL;
    }

    if(imgProducer->device) {
       NvMediaDeviceDestroy(imgProducer->device);
       imgProducer->device = NULL;
    }

    /* Test mode */
    if(imgProducer->testModeParams->isTestMode) {
        if(imgProducer->testModeParams->isGenCrc) {
            if(imgProducer->testModeParams->isCrcMatched)
                printf(" ---- Image CRC file generated successfully ---- \n");
            else
                printf(" ---- Image CRC file could not be generated ---- \n");
            fclose(imgProducer->testModeParams->refCrcFile);
        }
    }
    LOG_DBG("ImageProducerFini: end\n");
}

//Stop the imge producer thread
void ImageProducerStop(ImageProducerCtx *imgProducer)
{
    LOG_DBG("ImageProducerStop, wait for thread stop\n");
    if(imgProducer->procThread) {
        pthread_join(imgProducer->procThread, NULL);
    }
    LOG_DBG("ImageProducerStop, thread stop\n");
}

//Image producer to get the image back from EGLStream, and release them
void ImageProducerFlush(ImageProducerCtx *imgProducer)
{
    NvMediaImage *releaseImage = NULL;

    while (NvMediaEglStreamProducerGetImage(
           imgProducer->producer, &releaseImage, 0) == NVMEDIA_STATUS_OK) {
        NvQueuePut((NvQueue *)releaseImage->tag,
                  (void *)&releaseImage,
                  ENQUEUE_TIMEOUT);
    }
}
