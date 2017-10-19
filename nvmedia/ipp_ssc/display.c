/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "log_utils.h"
#include "misc_utils.h"
#include "surf_utils.h"
#include "display.h"

#define IMAGE_BUFFERS_POOL_SIZE 3
#define BUFFER_POOL_TIMEOUT 100

#define SET_X_OFFSET(xoffsets, red, clear1, clear2, blue) \
            xoffsets[red] = 0;\
            xoffsets[clear1] = 1;\
            xoffsets[clear2] = 0;\
            xoffsets[blue] = 1

#define SET_Y_OFFSET(yoffsets, red, clear1, clear2, blue) \
            yoffsets[red] = 0;\
            yoffsets[clear1] = 0;\
            yoffsets[clear2] = 1;\
            yoffsets[blue] = 1

#define CALCULATE_PIXEL(buf, pitch, x, y, xoffset, yoffset) \
            (buf[(pitch * (y + yoffset)) + (2 * (x + xoffset)) + 1] << 2) | \
            (buf[(pitch * (y + yoffset)) + (2 * (x + xoffset))] >> 6)

enum PixelColor {
    RED,
    CLEAR1,
    CLEAR2,
    BLUE,
    NUM_PIXEL_COLORS
};

static NvMediaBool
CheckDisplayCamSelected(IPPCtx *ctx, NvU32 pipelineNum)
{
    NvMediaBool selected;

    NvMutexAcquire(ctx->displayCycleMutex);
    selected = ctx->displayCameraId == pipelineNum ? NVMEDIA_TRUE: NVMEDIA_FALSE;
    NvMutexRelease(ctx->displayCycleMutex);

    return selected;
}

NvMediaStatus
DisplayImage(IPPCtx *ctx, NvU32 pipelineNum, NvMediaImage *image)
{
    NvMediaImage *releaseFrames[IMAGE_BUFFERS_POOL_SIZE] = {0};
    NvMediaImage **releaseList = &releaseFrames[0];
    NvMediaImageSurfaceMap inputSurfaceMap, outputSurfaceMap;
    ImageBuffer *buffer;
    NvU32 srcPitch = 0, dstPitch = 0;
    NvU32 srcHeight, srcWidth, dstWidth;
    NvU32 xOffsets[NUM_PIXEL_COLORS] = {0}, yOffsets[NUM_PIXEL_COLORS] = {0};
    NvU32 i, j, index = 0;
    NvU8 *map;
    NvU8 r, c1, c2, b, alpha = 0xFF;

    if (ctx->displayEnabled) {
        // Check if camera is selected for display
        if (CheckDisplayCamSelected(ctx, pipelineNum)) {
            memset(ctx->displayCpuBuffer, 0, ctx->displayCpuBufferSize);

            // Acquire image buffer
            BufferPool_AcquireBuffer(ctx->displayPool, (void **)&buffer);
            if (!buffer) {
                LOG_ERR("%s: Failed to acquire buffer from display pool\n", __func__);
                goto failed;
            }

            // Lock input image for read
            if (IsFailed(NvMediaImageLock(image,
                                          NVMEDIA_IMAGE_ACCESS_WRITE,
                                          &inputSurfaceMap))) {
                LOG_ERR("%s: Failed to lock image for CPU access\n", __func__);
                goto failed;
            }

            // Lock output image for write
            if (IsFailed(NvMediaImageLock(buffer->image,
                                          NVMEDIA_IMAGE_ACCESS_WRITE,
                                          &outputSurfaceMap))) {
                LOG_ERR("%s: Failed to lock display buffer for write\n", __func__);
                NvMediaImageUnlock(image);
                goto failed;
            }

            map = (NvU8 *)inputSurfaceMap.surface[0].mapping;
            srcHeight = inputSurfaceMap.height;
            srcWidth = inputSurfaceMap.width;
            srcPitch = inputSurfaceMap.surface[0].pitch;

            dstWidth = outputSurfaceMap.width;
            dstPitch = dstWidth * 4;

            // Set RCCB pixel order (green -> clear)
            switch (ctx->inputSurfFormat.pixelOrder) {
                case NVMEDIA_RAW_PIXEL_ORDER_RGGB:
                    SET_X_OFFSET(xOffsets, RED, CLEAR1, CLEAR2, BLUE);
                    SET_Y_OFFSET(yOffsets, RED, CLEAR1, CLEAR2, BLUE);
                    break;

                case NVMEDIA_RAW_PIXEL_ORDER_BGGR:
                    SET_X_OFFSET(xOffsets, BLUE, CLEAR1, CLEAR2, RED);
                    SET_Y_OFFSET(yOffsets, BLUE, CLEAR1, CLEAR2, RED);
                    break;

                case NVMEDIA_RAW_PIXEL_ORDER_GBRG:
                    SET_X_OFFSET(xOffsets, CLEAR1, BLUE, RED, CLEAR2);
                    SET_Y_OFFSET(yOffsets, CLEAR1, BLUE, RED, CLEAR2);
                    break;

                case NVMEDIA_RAW_PIXEL_ORDER_GRBG:
                default:
                    SET_X_OFFSET(xOffsets, CLEAR1, RED, BLUE, CLEAR2);
                    SET_Y_OFFSET(yOffsets, CLEAR1, RED, BLUE, CLEAR2);
                    break;
            }

            // Convert image from RCCB to RGBA and write to CPU buffer
            switch (ctx->rawCompressionFormat) {
                case RAW1x12:
                    for (i = 0; i < srcHeight; i += 2) {
                        for (j = 0; j < srcWidth; j += 2) {
                            // RED
                            r = CALCULATE_PIXEL(map, srcPitch, j, i, xOffsets[RED], yOffsets[RED]);
                            // CLEAR1
                            c1 = CALCULATE_PIXEL(map, srcPitch, j, i, xOffsets[CLEAR1], yOffsets[CLEAR1]);
                            // CLEAR2
                            c2 = CALCULATE_PIXEL(map, srcPitch, j, i, xOffsets[CLEAR2], yOffsets[CLEAR2]);
                             // BLUE
                            b = CALCULATE_PIXEL(map, srcPitch, j, i, xOffsets[BLUE], yOffsets[BLUE]);

                            // RED
                            ctx->displayCpuBuffer[index++] = r;
                            // GREEN (average of CLEAR1 and CLEAR2)
                            ctx->displayCpuBuffer[index++] = (c1 + c2) / 2;
                            // BLUE
                            ctx->displayCpuBuffer[index++] = b;
                            // ALPHA
                            ctx->displayCpuBuffer[index++] = alpha;
                        }
                    }
                    break;

                default:
                    LOG_ERR("%s: Unsupported compression format: %d\n", __func__,
                            ctx->rawCompressionFormat);
                    goto failed;
            }

            // Write to image buffer
            NvMediaImagePutBits(buffer->image, NULL, (void **)&ctx->displayCpuBuffer,
                                &dstPitch);

            NvMediaImageUnlock(buffer->image);
            NvMediaImageUnlock(image);

            // Render on display
            NvMediaIDPFlip(ctx->display,
                           buffer->image,
                           NULL,
                           NULL,
                           releaseList,
                           NULL);

            while (*releaseList) {
                buffer = (*releaseList)->tag;
                BufferPool_ReleaseBuffer(buffer->bufferPool, buffer);
                releaseList++;
            }
        }
    }

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed to display image\n", __func__);
    return NVMEDIA_STATUS_ERROR;
}

void
DisplayCycleCamera(IPPCtx *ctx)
{
    if (ctx->displayEnabled) {
        if (ctx->imagesNum > 1) {
            NvMutexAcquire(ctx->displayCycleMutex);
            ctx->displayCameraId = (ctx->displayCameraId + 1) % ctx->imagesNum;
            NvMutexRelease(ctx->displayCycleMutex);
        }

        else {
            printf("Only one camera available\n");
        }
    }
}

NvMediaStatus
DisplayInit(IPPCtx *ctx)
{
    ImageBufferPoolConfig imgBufferConfig;
    NvMediaIDPDeviceParams outputs[MAX_OUTPUT_DEVICES];
    int outputDevicesNum = 0;
    NvU32 i;

    if (ctx->displayEnabled) {
        memset(&imgBufferConfig, 0, sizeof(ImageBufferPoolConfig));

        // Buffer pool configuration
        imgBufferConfig.width = ctx->inputWidth / 2;
        imgBufferConfig.height = ctx->inputHeight / 2;
        imgBufferConfig.surfType = NvMediaSurfaceType_Image_RGBA;
        imgBufferConfig.imageClass = NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE;
        imgBufferConfig.imagesCount = 1;
        imgBufferConfig.device = ctx->device;
        imgBufferConfig.surfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_DISPLAY;

        if (IsFailed(BufferPool_Create(&ctx->displayPool,
                                       IMAGE_BUFFERS_POOL_SIZE,
                                       BUFFER_POOL_TIMEOUT,
                                       IMAGE_BUFFER_POOL, &imgBufferConfig))) {
            LOG_ERR("%s: BufferPool_Create for display buffer failed", __func__);
            goto failed;
        }

        // Query for display device and create IDP
        if (IsFailed(NvMediaIDPQuery(&outputDevicesNum, outputs))) {
            LOG_ERR("%s: Failed to query for available displays\n", __func__);
            goto failed;
        }

        for (i = 0; i < (NvU32)outputDevicesNum; i++) {
            if (outputs[i].displayId == ctx->displayId) {
                ctx->display = NvMediaIDPCreate(ctx->displayId,
                                                ctx->windowId,
                                                NvMediaSurfaceType_Image_RGBA,
                                                NULL,
                                                outputs[i].enabled);
                if (ctx->display) {
                    break;
                }
            }
        }

        if (!ctx->display) {
            LOG_ERR("%s: Failed to create IDP display\n", __func__);
            goto failed;
        }

        NvMediaIDPSetDepth(ctx->display, ctx->depth);

        // Create CPU buffer for image conversion for display
        ctx->displayCpuBufferSize = (ctx->inputWidth/2) * (ctx->inputHeight/2) * 4;
        ctx->displayCpuBuffer = calloc(1, ctx->displayCpuBufferSize);
        if (!ctx->displayCpuBuffer) {
            LOG_ERR("%s: Failed to allocate memory\n", __func__);
            goto failed;
        }

        // Display camera 0 by default
        ctx->displayCameraId = 0;

        // Create mutex for cycling camera
        NvMutexCreate(&ctx->displayCycleMutex);
    }

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed to initialize display\n", __func__);
    return NVMEDIA_STATUS_ERROR;
}

void
DisplayFini(IPPCtx *ctx)
{
    NvMediaImage *releaseFrames[IMAGE_BUFFERS_POOL_SIZE] = {0};
    NvMediaImage **releaseList = &releaseFrames[0];
    ImageBuffer *buffer;

    if (ctx->display) {
        // Flush display
        NvMediaIDPFlip(ctx->display,
                       NULL,
                       NULL,
                       NULL,
                       releaseList,
                       NULL);
        while (*releaseList) {
            buffer = (*releaseList)->tag;
            BufferPool_ReleaseBuffer(buffer->bufferPool, buffer);
            releaseList++;
        }

        NvMediaIDPDestroy(ctx->display);
    }

    if (ctx->displayCpuBuffer) {
        free(ctx->displayCpuBuffer);
    }

    if (ctx->displayPool) {
        BufferPool_Destroy(ctx->displayPool);
    }

    if (ctx->displayCycleMutex) {
        NvMutexDestroy(ctx->displayCycleMutex);
    }
}

