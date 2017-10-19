/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
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
#include "write.h"

NvMediaStatus
WriteRawImage(IPPCtx *ctx, NvU32 pipelineNum, NvMediaImage *image)
{
    NvMediaImageSurfaceMap surfaceMap;
    unsigned char *pBuff = NULL, *pDstBuff[3] = {NULL};
    unsigned int dstPitches[3];
    unsigned int xScale = 1, yScale = 1, width, height;
    int indexU = 1, indexV = 2;
    FILE *file = NULL;

    if (ctx->saveEnabled) {
        memset(ctx->writeImageBuffer[pipelineNum], 0, ctx->writeImageBufferSize);

        if (!(file = fopen(ctx->imageFileName[pipelineNum], "ab"))) {
            LOG_ERR("%s: file open failed\n", __func__);
            goto failed;
        }

        if(IsFailed(NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap))) {
            LOG_ERR("%s: NvMediaImageLock failed\n", __func__);
            goto failed;
        }

        if (image->type != NvMediaSurfaceType_Image_RAW) {
            NvMediaImageUnlock(image);
            LOG_ERR("%s: Invalid surface type\n", __func__);
            goto failed;
        }

        height = surfaceMap.height;
        width  = surfaceMap.width;

        dstPitches[0] = width * ctx->rawBytesPerPixel;
        pBuff = ctx->writeImageBuffer[pipelineNum];
        pDstBuff[0] = pBuff;
        pDstBuff[indexV] = pDstBuff[0] + width * height;
        pDstBuff[indexU] = pDstBuff[indexV] + (width * height) / (xScale * yScale);

        dstPitches[indexU] = width / xScale;
        dstPitches[indexV] = width / xScale;

        if (IsFailed(NvMediaImageGetBits(image, NULL, (void **)pDstBuff, dstPitches))) {
            NvMediaImageUnlock(image);
            LOG_ERR("%s: NvMediaVideoSurfaceGetBits() failed\n", __func__);
            goto failed;
        }

        if (fwrite(pDstBuff[0], ctx->writeImageBufferSize, 1, file) != 1) {
            NvMediaImageUnlock(image);
            LOG_ERR("%s: file write failed\n", __func__);
            goto failed;
        }

        NvMediaImageUnlock(image);
        fclose(file);
    }

    return NVMEDIA_STATUS_OK;

failed:
    if (file) {
        fclose(file);
    }

    LOG_ERR("%s: Failed to write image\n", __func__);
    return NVMEDIA_STATUS_ERROR;
}

static void
CreateImageFileName(IPPCtx *ctx)
{
    NvU32 i;
    char filename[MAX_STRING_SIZE] = {0};
    char buf[MAX_STRING_SIZE] = {0};

    for (i = 0; i < ctx->imagesNum; i++) {
        memset(filename, 0, sizeof(filename));
        memset(buf, 0, sizeof(buf));

        strncpy(filename, ctx->filename, MAX_STRING_SIZE);
        strcat(filename, "_cam_");
        sprintf(buf, "%d", i);
        strcat(filename, buf);
        strcat(filename, ".raw");

        memcpy(ctx->imageFileName[i], filename, MAX_STRING_SIZE);
    }
}

NvMediaStatus
WriteImageInit(IPPCtx *ctx)
{
    NvU32 i;

    memset(ctx->imageFileName, 0, sizeof(ctx->imageFileName[0][0]) * MAX_AGGREGATE_IMAGES * MAX_STRING_SIZE);
    memset(ctx->writeImageBuffer, 0, sizeof(ctx->writeImageBuffer));

    if (ctx->saveEnabled) {
        ctx->writeImageBufferSize = ctx->inputWidth * ctx->inputHeight * ctx->rawBytesPerPixel;

        // Allocate additional memory for embedded data lines
        ctx->writeImageBufferSize += ctx->inputWidth * ctx->rawBytesPerPixel * ctx->embeddedLinesTop;
        ctx->writeImageBufferSize += ctx->inputWidth * ctx->rawBytesPerPixel * ctx->embeddedLinesBottom;

        for (i = 0; i < ctx->imagesNum; i++) {
            ctx->writeImageBuffer[i] = calloc(1, ctx->writeImageBufferSize);
            if (!ctx->writeImageBuffer[i]) {
                LOG_ERR("%s: Failed to allocate memory for image write buffer\n", __func__);
                goto failed;
            }
        }

        // Create image filenames
        CreateImageFileName(ctx);
    }

    return NVMEDIA_STATUS_OK;

failed:
    LOG_ERR("%s: Failed to initialize image writing\n", __func__);
    return NVMEDIA_STATUS_ERROR;
}

void
WriteImageFini(IPPCtx *ctx)
{
    NvU32 i;

    for (i = 0; i < ctx->imagesNum; i++) {
        if (ctx->writeImageBuffer[i]) {
            free(ctx->writeImageBuffer[i]);
        }
    }
}

