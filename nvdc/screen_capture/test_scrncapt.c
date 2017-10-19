/*
 * Copyright (c) 2016 NVIDIA Corporation.  All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <nvscrncapt.h>

#include "nvscrncapt_utils.h"

/** Current Log Verbosity Level */
int currentLogLevel = 0;

static void
ShowWindowSummary(
    NvScrncaptWindowState *pWindowState)
{
    LOG_INFO("Window: %d, enabled: %d, blendMode %d, winDepth: %u, "
            "surfaceLayout: %d, colorFormat: %d\n",
            pWindowState->windowIdx,
            pWindowState->enabled,
            pWindowState->blendMode,
            pWindowState->winDepth,
            pWindowState->surfaceMap.surfaceLayout,
            pWindowState->surfaceMap.pixelFormat);

    LOG_DBG("blockHeight: %d, lengthRGB: %d, strideRGB: %d, "
            "strideY/UV: %d/%d, lengthY/U/V: %d/%d/%d\n",
            pWindowState->surfaceMap.blockHeight,
            pWindowState->surfaceMap.lengthRGB,
            pWindowState->surfaceMap.strideRGB,
            pWindowState->surfaceMap.strideY,
            pWindowState->surfaceMap.strideUV,
            pWindowState->surfaceMap.lengthY,
            pWindowState->surfaceMap.lengthU,
            pWindowState->surfaceMap.lengthV);
}

static void
ShowHeadSummary(
    NvScrncaptHeadState *pHeadState)
{
    int i;

    /* Top-level head information */
    LOG_INFO("Head: %d, Enabled: %d, numWins, %d, Resolution: %dx%d\n",
            pHeadState->headIdx,
            pHeadState->enabled,
            pHeadState->numWins,
            pHeadState->resolutionH,
            pHeadState->resolutionV);

    /* Per-window information */
    for (i = 0; i < pHeadState->numWins; i++)
        ShowWindowSummary(&pHeadState->wins[i]);
}

static void
ShowCaptureSummary(
    NvScrncaptResult *pCaptureResult)
{
    int i;

    LOG_INFO("Capture Summary\n");

    /* Top-level summary */
    LOG_INFO("# of heads: %d\n", pCaptureResult->numHeads);
    LOG_INFO("Capture time (us): %u\n", pCaptureResult->stats.captureTimeUsec);
    LOG_INFO("Allocated memory (bytes): %u\n", pCaptureResult->stats.memSize);

    /* Per-head info */
    for (i = 0; i < pCaptureResult->numHeads; i++)
        ShowHeadSummary(&pCaptureResult->heads[i]);

    LOG_INFO("End Capture Summary\n");
}

static void
ConvertYUVToRGB(
    const NvScrncaptPixel *pYUV,
    NvScrncaptPixel *pRGB)
{
    unsigned char y;
    signed char u, v;

    y = pYUV->y;
    u = (signed char)pYUV->u - 128;
    v = (signed char)pYUV->v - 128;

    pRGB->r = y + v + (v >> 2) + (v >> 3) + (v >> 5);
    pRGB->g = y - ((u >> 2) + (u >> 4) + (u >> 5)) - ((v >> 1) +
            (v >> 3) + (v >> 4) + (v >> 5));
    pRGB->b = y + u + (u >> 1) + (u >> 2) + (u >> 6);
    pRGB->alpha = pYUV->alpha;
}

static void
GetPixelRGB(
    NvScrncaptSurfaceMap *pSurfaceMap,
    NvScrncaptPixel *pPixel,
    int x,
    int y) {

    /* bps = bytes per sample (in each plane) */
    int bpsRGB;
    unsigned char *pRGB = pSurfaceMap->pRGB;;

    switch (pSurfaceMap->pixelFormat) {
        /* Add additional pixel formats here */
        case NvScrncaptColorFormat_B8G8R8A8:
        case NvScrncaptColorFormat_R8G8B8A8:
            bpsRGB = 4;
            break;
        default:
            LOG_WARN("Unsupported pixel format (%d)\n",
                    pSurfaceMap->pixelFormat);
            return;
    }

    switch (pSurfaceMap->surfaceLayout) {
        case NvScrncaptSurfaceLayout_PitchLinear:
            pRGB += (y * pSurfaceMap->strideRGB) + (x * bpsRGB);
            break;
        case NvScrncaptSurfaceLayout_BlockLinear:
            pRGB += NvScrncaptGetBlocklinearOffset(x * bpsRGB, y,
                    pSurfaceMap->strideRGB, pSurfaceMap->blockHeight);
            break;
        default:
            LOG_WARN("Unsupported surface format (%d)\n",
                    pSurfaceMap->surfaceLayout);
            return;
    }

    switch (pSurfaceMap->pixelFormat) {
        /* Add additional pixel formats here */
        case NvScrncaptColorFormat_B8G8R8A8:
            pPixel->r = pRGB[2];
            pPixel->g = pRGB[1];
            pPixel->b = pRGB[0];
            pPixel->alpha = pRGB[3];
            break;
        case NvScrncaptColorFormat_R8G8B8A8:
            pPixel->r = pRGB[0];
            pPixel->g = pRGB[1];
            pPixel->b = pRGB[2];
            pPixel->alpha = pRGB[3];
            break;
        default:
            LOG_WARN("Unsupported pixel format (%d)\n",
                    pSurfaceMap->pixelFormat);
            return;
    }
}

static void
GetPixelYUV(
    NvScrncaptSurfaceMap *pSurfaceMap,
    NvScrncaptPixel *pPixel,
    int x,
    int y) {

    /* bps = bytes per sample (in each plane) */
    int bpsY, bpsU, bpsV;
    int xU = 0, xV = 0, yU = 0, yV = 0;
    unsigned char *pY = pSurfaceMap->pY;
    unsigned char *pU = pSurfaceMap->pU;
    unsigned char *pV = pSurfaceMap->pV;

    switch (pSurfaceMap->pixelFormat) {
        /* Add additional pixel formats here */
        case NvScrncaptColorFormat_YCbCr420SP:
            bpsY = 1;
            bpsU = 2;
            bpsV = 0;
            /*
             * YUV420 is subsample by factor of 2
             * for both U & V chroma
             */
            xU = x >> 1;
            yU = y >> 1;
            break;
        default:
            LOG_WARN("Unsupported pixel format (%d)\n",
                    pSurfaceMap->pixelFormat);
            return;
    }

    switch (pSurfaceMap->surfaceLayout) {
        case NvScrncaptSurfaceLayout_PitchLinear:
            pY += (y * pSurfaceMap->strideY) + (x * bpsY);
            pU += (yU * pSurfaceMap->strideUV) + (xU * bpsU);
            pV += (yV * pSurfaceMap->strideUV) + (xV * bpsV);
            break;
        case NvScrncaptSurfaceLayout_BlockLinear:
            pY += NvScrncaptGetBlocklinearOffset(x * bpsY, y,
                    pSurfaceMap->strideY, pSurfaceMap->blockHeight);
            pU += NvScrncaptGetBlocklinearOffset(xU * bpsU, yU,
                    pSurfaceMap->strideUV, pSurfaceMap->blockHeight);
            pV += NvScrncaptGetBlocklinearOffset(xV * bpsV, yV,
                    pSurfaceMap->strideUV, pSurfaceMap->blockHeight);
            break;
        default:
            LOG_ERR("Unsupported surface format (%d)\n",
                    pSurfaceMap->surfaceLayout);
            return;
    }

    switch (pSurfaceMap->pixelFormat) {
        /* Add additional pixel formats here */
        case NvScrncaptColorFormat_YCbCr420SP:
            pPixel->y = pY[0];
            pPixel->u = pU[0];
            pPixel->v = pU[1];
            pPixel->alpha = ~0;
            break;
        default:
            LOG_WARN("Unsupported pixel format (%d)\n",
                    pSurfaceMap->pixelFormat);
            return;
    }
}

static void
GetPixel(
    NvScrncaptSurfaceMap *pSurfaceMap,
    NvScrncaptPixel* pPixel,
    int x,
    int y) {

    NvScrncaptPixel yuvPixel = {{0}};

    switch (pSurfaceMap->pixelFormat) {
        /* Add additional pixel formats here */
        case NvScrncaptColorFormat_YCbCr420SP:
            GetPixelYUV(pSurfaceMap, &yuvPixel, x, y);
            ConvertYUVToRGB(&yuvPixel, pPixel);
            break;
        case NvScrncaptColorFormat_B8G8R8A8:
        case NvScrncaptColorFormat_R8G8B8A8:
            GetPixelRGB(pSurfaceMap, pPixel, x, y);
            break;
        default:
            LOG_WARN("Unsupported pixel format (%d)\n",
                    pSurfaceMap->pixelFormat);
    }
}

static NvScrncaptStatus
SaveWindowRGB(
    NvScrncaptWindowState *pWindow,
    FILE *pCaptureFile) {

    unsigned char *pLineBuffer = NULL;
    int lineBufferIdx = 0;
    NvScrncaptPixel rgbPixel = {{0}};
    int fbWidth, fbHeight;
    int i, j;
    int x, y;

    fbWidth = pWindow->fbAperture.width;
    fbHeight = pWindow->fbAperture.height;

    LOG_DBG("Saving window %d: width %d height %d\n",
            pWindow->windowIdx,
            fbWidth,
            fbHeight);

    /* Allocate 3x width for R/G/B planes */
    pLineBuffer = malloc(3 * fbWidth);
    if (!pLineBuffer) {
        return NVSCRNCAPT_STATUS_OUT_OF_MEMORY;
    }

    for (i = 0; i < fbHeight; i++) {

        lineBufferIdx = 0;

        for (j = 0; j < fbWidth; j++) {

            /* Check for surface inversion */
            x = pWindow->invertH ? (fbWidth - j - 1) : j;
            y = pWindow->invertV ? (fbHeight - i - 1) : i;

            /* Account for aperture offset */
            x += pWindow->fbAperture.startX;
            y += pWindow->fbAperture.startY;

            /* Get the RGB pixel for this coordinate */
            GetPixel(&pWindow->surfaceMap, &rgbPixel, x, y);

            pLineBuffer[lineBufferIdx++] = rgbPixel.r;
            pLineBuffer[lineBufferIdx++] = rgbPixel.g;
            pLineBuffer[lineBufferIdx++] = rgbPixel.b;

        }

        if (!fwrite(pLineBuffer, lineBufferIdx, 1, pCaptureFile)) {
            LOG_ERR("Failed to write linebuffer to capture file - (%d)\n",
                    errno);
            return NVSCRNCAPT_STATUS_ERROR;
        }
    }

    return NVSCRNCAPT_STATUS_OK;
}

static NvScrncaptStatus
SaveCaptureRGB(
    NvScrncaptResult *pCaptureResult,
    char *filePrefix)
{
    int i, j;
    char fileName[MAX_STRING_SIZE];
    FILE *pCaptureFile = NULL;
    NvScrncaptStatus status;
    NvScrncaptHeadState *pHead;
    NvScrncaptWindowState *pWindow;
    struct timeval tm_from, tm_to;

    LOG_DBG("Saving capture to RGB files\n");

    for (i = 0; i < pCaptureResult->numHeads; i++) {

        pHead = &pCaptureResult->heads[i];

        for (j = 0; j < pHead->numWins; j++) {
            pWindow = &pHead->wins[j];

            /* Create the file handle for the window */
            snprintf(fileName, MAX_STRING_SIZE,
                    "%s-hd%d-win%d-%dx%d.rgb24",
                    filePrefix,
                    pHead->headIdx,
                    pWindow->windowIdx,
                    pWindow->fbAperture.width,
                    pWindow->fbAperture.height);

            pCaptureFile = fopen(fileName, "wb");
            if (pCaptureFile == NULL) {
                LOG_ERR("Could not open file, %s, for writing, errno %d\n",
                        fileName,
                        errno);
                return NVSCRNCAPT_STATUS_ERROR;
            }

            LOG_INFO("Saving head %d, window %d to %s\n",
                    pHead->headIdx,
                    pWindow->windowIdx,
                    fileName);

            gettimeofday(&tm_from, NULL);
            status = SaveWindowRGB(pWindow, pCaptureFile);
            fclose(pCaptureFile);
            gettimeofday(&tm_to, NULL);
            pCaptureFile = NULL;
            LOG_INFO("Saving time (us): %ld\n",
                (tm_to.tv_sec - tm_from.tv_sec) * 1000000
                + tm_to.tv_usec - tm_from.tv_usec);

            if (status) {
                LOG_ERR("SaveWindowRGB failed for head %d, window %d\n",
                        pHead->headIdx,
                        pWindow->windowIdx);
                return status;
            }
        }
    }

    return NVSCRNCAPT_STATUS_OK;
}

int main(int argc, char *argv[])
{
    TestArgs testArgs;
    NvScrncaptStatus status;
    NvScrncaptResult *pCaptureResult = NULL;
    void *pUserMemory = NULL;

    if (ParseArgs(argc, argv, &testArgs)) {
        return NVSCRNCAPT_STATUS_ERROR;
    }

    currentLogLevel = testArgs.logLevel;

    /* Initialize capture context */
    status = NvScrncaptInit(&pCaptureResult);
    if (status) {
        LOG_ERR("NvScrncaptInit failed (%d)\n", status);
        return status;
    }

    if (testArgs.preAllocateMemory) {
        LOG_DBG("Pre-allocating memory pool\n");
        pUserMemory = malloc(MAX_MEMPOOL_SIZE);
        if (!pUserMemory) {
            LOG_ERR("Memory pre-allocation failed\n");
            LOG_ERR("Falling back to library allocation\n");
        } else {
            LOG_DBG("Memory pre-allocation succeeded\n");
            pCaptureResult->userAddress = pUserMemory;
            pCaptureResult->userDataSize = MAX_MEMPOOL_SIZE;
        }
    }

    /* Capture */
    status = NvScrncaptCapture(pCaptureResult, testArgs.headMask);
    if (status) {
        LOG_ERR("NvScrncaptCapture failed (%d)\n", status);
        NvScrncaptCleanup(pCaptureResult);
        return status;
    }

    /* Display capture summary */
    ShowCaptureSummary(pCaptureResult);

    /* Save capture to RGB file */
    status = SaveCaptureRGB(pCaptureResult, testArgs.filePrefix);

    /* Cleanup */
    status = NvScrncaptCleanup(pCaptureResult);
    if (status) {
        LOG_ERR("NvScrncaptCleanup failed (%d)\n", status);
        return status;
    }

    if (testArgs.preAllocateMemory) {
        if (pUserMemory) {
            LOG_DBG("Freeing pre-allocated memory\n");
            free(pUserMemory);
        }
    }

    return NVSCRNCAPT_STATUS_OK;
}
