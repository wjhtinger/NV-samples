/* Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer_utils.h"
#include "log_utils.h"
#include "nvmedia.h"
#include "nvmedia_image.h"
#include "surf_utils.h"

NvMediaStatus
BufferPool_Create (
    BufferPool **poolOut,
    NvU32 capacity,
    NvU32 timeout,
    BufferPoolType type,
    void *config)
{
    NvMediaStatus status;
    BufferPool *pool;
    NvU32 i, j;

    if(!poolOut)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(capacity == 0 || capacity > BUFFERS_POOL_MAX_CAPACITY)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    pool = calloc(1, sizeof(BufferPool));

    if(!pool)
        return NVMEDIA_STATUS_OUT_OF_MEMORY;

    pool->capacity = capacity;
    pool->timeout = timeout;
    pool->type = type;

    switch (pool->type) {
        case IMAGE_BUFFER_POOL:
        {
            ImageBuffer *buffer = NULL;
            ImageBufferPoolConfig *imageBufferconfig;
            pool->config = calloc(1, sizeof(ImageBufferPoolConfig));
            if (!pool->config) {
                LOG_ERR("Failed to allocate bufferpool config\n");
                status = NVMEDIA_STATUS_OUT_OF_MEMORY;
                goto failed;
            }
            memcpy(pool->config, config, sizeof(ImageBufferPoolConfig));
            imageBufferconfig = (ImageBufferPoolConfig *)pool->config;

            status = NvQueueCreate(&pool->queue, pool->capacity, sizeof(ImageBuffer *));
            if(status != NVMEDIA_STATUS_OK)
                goto failed;

            for(i = 0; i < pool->capacity; i++) {
                buffer = calloc(1, sizeof(ImageBuffer));
                if(!buffer) {
                    status = NVMEDIA_STATUS_OUT_OF_MEMORY;
                    goto failed;
                }

                status = NvMutexCreate(&buffer->mutex);
                if (status != NVMEDIA_STATUS_OK)
                    goto failed;

                buffer->bufferPool = pool;
                buffer->image = NvMediaImageCreate(imageBufferconfig->device,          // device
                                                   imageBufferconfig->surfType,        // surface type
                                                   imageBufferconfig->imageClass,      // image class
                                                   imageBufferconfig->imagesCount,     // images count
                                                   imageBufferconfig->width,           // surf width
                                                   imageBufferconfig->height,          // surf height
                                                   imageBufferconfig->surfAttributes,  // attributes
                                                   &imageBufferconfig->surfAdvConfig); // config
                if(!buffer->image) {
                    status = NVMEDIA_STATUS_ERROR;
                    goto failed;
                }
                status =  InitImage(buffer->image,
                                    imageBufferconfig->surfAdvConfig.bitsPerPixel > NVMEDIA_BITS_PER_PIXEL_8 ?
                                    2 : 1);
                if (status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Init image failed \n", __func__);
                    goto failed;
                }

                buffer->imagesCount = imageBufferconfig->imagesCount;
                if(imageBufferconfig->imageClass == NVMEDIA_IMAGE_CLASS_AGGREGATE_IMAGES &&
                   imageBufferconfig->createSiblingsFlag) {
                    buffer->siblingImages = calloc(1, sizeof(NvMediaImage *) * buffer->imagesCount);
                    if (!buffer->siblingImages) {
                        LOG_ERR("Failed to allocate sibling images\n");
                        status = NVMEDIA_STATUS_OUT_OF_MEMORY;
                        goto failed;
                    }
                    for(j = 0; j < buffer->imagesCount; j++) {
                        buffer->siblingImages[j] = NvMediaImageSiblingCreate(buffer->image,                         // Parent image
                                                                             j,                                     // Image Index
                                                                             imageBufferconfig->siblingAttributes); // Attributes
                        if(!buffer->siblingImages[j]) {
                            status = NVMEDIA_STATUS_ERROR;
                            goto failed;
                        }

                        buffer->siblingImages[j]->tag = buffer;
                    }
                }

                buffer->image->tag = buffer;
                buffer->processingStartTime = ULONG_MAX;
                buffer->processingEndTime = 0;

                status = NvQueuePut(pool->queue, (void *)&buffer, NV_TIMEOUT_INFINITE);
                if(status != NVMEDIA_STATUS_OK)
                    goto failed;
            }
            break;
        }
        case SIBLING_BUFFER_POOL:
        {
            ImageBuffer *buffer = NULL;
            SiblingBufferPoolConfig *siblingBufferconfig;
            pool->config = calloc(1, sizeof(SiblingBufferPoolConfig));
            memcpy(pool->config, config, sizeof(SiblingBufferPoolConfig));
            siblingBufferconfig = (SiblingBufferPoolConfig *)pool->config;

            status = NvQueueCreate(&pool->queue, pool->capacity, sizeof(ImageBuffer *));
            if(status != NVMEDIA_STATUS_OK)
                goto failed;

            for(i = 0; i < pool->capacity; i++) {
                buffer = calloc(1, sizeof(ImageBuffer));
                if(!buffer) {
                    status = NVMEDIA_STATUS_OUT_OF_MEMORY;
                    goto failed;
                }

                status = NvMutexCreate(&buffer->mutex);
                if (status != NVMEDIA_STATUS_OK)
                    goto failed;

                buffer->bufferPool = pool;
                buffer->image = NvMediaImageSiblingCreate(siblingBufferconfig->parentImage,  // Parent image
                                                                           siblingBufferconfig->imageIndex,   // Image Index
                                                                           siblingBufferconfig->attributes);  // Attributes
                if(!buffer->image) {
                    status = NVMEDIA_STATUS_ERROR;
                    goto failed;
                }
                buffer->image->tag = buffer;
                buffer->processingStartTime = ULONG_MAX;
                buffer->processingEndTime = 0;

                status = NvQueuePut(pool->queue, &buffer, NV_TIMEOUT_INFINITE);
                if(status != NVMEDIA_STATUS_OK)
                    goto failed;
            }
            break;
        }
        default:
            return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    *poolOut = pool;
    return NVMEDIA_STATUS_OK;

failed:
    BufferPool_Destroy(pool);
    *poolOut = NULL;
    return status;
}

NvMediaStatus
BufferPool_Destroy (
    BufferPool *pool)
{
    void *buffer = NULL;
    NvU32 i;

    if(!pool)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    while((NvQueueGet(pool->queue, &buffer, 0) == NVMEDIA_STATUS_OK) && buffer) {
        switch (pool->type) {
            case IMAGE_BUFFER_POOL:
                if(((ImageBuffer *)buffer)->siblingImages) {
                    for(i = 0; i < ((ImageBuffer *)buffer)->imagesCount; i++) {
                        if(((ImageBuffer *)buffer)->siblingImages[i])
                            NvMediaImageDestroy(((ImageBuffer *)buffer)->siblingImages[i]);
                    }
                    free(((ImageBuffer *)buffer)->siblingImages);
                }
                NvMediaImageDestroy(((ImageBuffer *)buffer)->image);
                break;
            case SIBLING_BUFFER_POOL:
                NvMediaImageDestroy(((ImageBuffer *)buffer)->image);
                break;
            default:
                return NVMEDIA_STATUS_BAD_PARAMETER;
        }

        NvMutexDestroy(((ImageBuffer*)buffer)->mutex);
        free(buffer);
    }

    NvQueueDestroy(pool->queue);

    free(pool);

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
BufferPool_AcquireBuffer (
    BufferPool *pool,
    void **bufferOut)
{
    void *buffer = NULL;

    if(!pool || !bufferOut)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(NvQueueGet(pool->queue, &buffer, pool->timeout) != NVMEDIA_STATUS_OK) {
        *bufferOut = NULL;
        return NVMEDIA_STATUS_ERROR;
    }

    NvMutexAcquire(((ImageBuffer*)buffer)->mutex);
    ((ImageBuffer*)buffer)->refCount++;
    NvMutexRelease(((ImageBuffer*)buffer)->mutex);

    *bufferOut = buffer;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
BufferPool_ReleaseBuffer (
    BufferPool *pool,
    void *buffer)
{
    ImageBuffer *imageBuffer = NULL;
    if(!pool || !buffer)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    imageBuffer = (ImageBuffer*)buffer;

    NvMutexAcquire(imageBuffer->mutex);
    if (imageBuffer->refCount > 0) {
        imageBuffer->refCount--;
        if (imageBuffer->refCount == 0) {
            NvMutexRelease(imageBuffer->mutex);
            if(NvQueuePut(pool->queue, &buffer, pool->timeout) != NVMEDIA_STATUS_OK)
                return NVMEDIA_STATUS_ERROR;
        } else {
            NvMutexRelease(imageBuffer->mutex);
        }
    } else {
        NvMutexRelease(imageBuffer->mutex);
        LOG_ERR("ReleaseBuffer invoked on zero refCount\n");
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
BufferPool_DoActionOnAllBuffers (
    BufferPool *pool,
    void *context,
    NvMediaStatus (* callbackFunc)(void *context, void *buffer))
{
    NvU32 i;
    void *buffer;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;

    if(!pool || !context || !callbackFunc)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    for(i = 0; i < pool->capacity; i++) {
        if(NvQueueGet(pool->queue, &buffer, 0) != NVMEDIA_STATUS_OK)
            return NVMEDIA_STATUS_ERROR;

        LOG_DBG("Do Action on buffer: %p, image: %p\n", buffer, ((ImageBuffer *)buffer)->image);
        status = callbackFunc(context, buffer);
        if(IsFailed(status))
            return status;

        if(NvQueuePut(pool->queue, &buffer, 0) != NVMEDIA_STATUS_OK)
            return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
BufferPool_AddRefToBuffer (
    ImageBuffer *buffer)
{
    if (!buffer)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    NvMutexAcquire(buffer->mutex);
    buffer->refCount++;
    NvMutexRelease(buffer->mutex);

    return NVMEDIA_STATUS_OK;
}
