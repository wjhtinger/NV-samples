/*
 * Copyright (c) 2013-2017 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log_utils.h"
#include "surf_utils.h"

NvMediaStatus
WriteFrame(
    char *filename,
    NvMediaVideoSurface *videoSurface,
    NvMediaBool bOrderUV,
    NvMediaBool bAppend)
{
    static int surfCount = 0;
    NvMediaVideoSurfaceMap surfaceMap;
    unsigned char *pBuff = NULL, *pDstBuff[3] = {NULL}, *pChroma;
    unsigned int dstPitches[3];
    unsigned int FrameSize = 0, width, height;
    int indexU = 1, indexV = 2;
    int bytePerPixel = 1;
    NvMediaStatus status, result = NVMEDIA_STATUS_ERROR;
    int shift = 0; // 16 - bitdepth
    unsigned int i;
    NvU16 *psrc;
    unsigned int size;
    int xScale = 2, yScale = 2;
    FILE *file = NULL;
    if(!videoSurface || !filename) {
        LOG_ERR("WriteFrame: Bad parameter\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    width = videoSurface->width;
    height = videoSurface->height;

    switch(videoSurface->type) {
        case NvMediaSurfaceType_Video_420_10bit:
        case NvMediaSurfaceType_Video_422_10bit:
        case NvMediaSurfaceType_Video_444_10bit:
            shift = 6;
            bytePerPixel = 2;
            break;
        case NvMediaSurfaceType_Video_420_12bit:
        case NvMediaSurfaceType_Video_422_12bit:
        case NvMediaSurfaceType_Video_444_12bit:
            shift = 4;
            bytePerPixel = 2;
            break;
        default:
            bytePerPixel = 1;
            break;
    }

    file = fopen(filename, bAppend ? "ab" : "wb");
    if(!file) {
        LOG_ERR("WriteFrame: file open failed: %s\n", filename);
        perror(NULL);
        return NVMEDIA_STATUS_ERROR;
    }
    surfCount++;

    pBuff = malloc(width * height * bytePerPixel * 4);
    if(!pBuff) {
        LOG_ERR("WriteFrame: Failed to allocate image buffer\n");
        fclose(file);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    pDstBuff[0] = pBuff;

    switch(videoSurface->type) {
        case NvMediaSurfaceType_YV24:
        case NvMediaSurfaceType_Video_444_10bit:
        case NvMediaSurfaceType_Video_444_12bit:
            xScale = 1;
            yScale = 1;
            pDstBuff[indexV] = pDstBuff[0] + width * height * bytePerPixel;
            pDstBuff[indexU] = pDstBuff[indexV] + width * height * bytePerPixel;
            dstPitches[0] = dstPitches[indexU] = dstPitches[indexV] = width * bytePerPixel;
            break;
        case NvMediaSurfaceType_YV16:
        case NvMediaSurfaceType_YV16x2:
        case NvMediaSurfaceType_Video_422_10bit:
        case NvMediaSurfaceType_Video_422_12bit:
            xScale = 1;
            yScale = 2;
            pDstBuff[indexV] = pDstBuff[0] + width * height * bytePerPixel;
            pDstBuff[indexU] = pDstBuff[indexV] + width * height * bytePerPixel / 2;
            dstPitches[0] = width * bytePerPixel;
            dstPitches[indexU] = dstPitches[indexV] = width * bytePerPixel / 2;
            break;
        case NvMediaSurfaceType_YV12:
        case NvMediaSurfaceType_Video_420_10bit:
        case NvMediaSurfaceType_Video_420_12bit:
            pDstBuff[indexV] = pDstBuff[0] + width * height * bytePerPixel;
            pDstBuff[indexU] = pDstBuff[indexV] + width * height * bytePerPixel / 4;
            dstPitches[0] = width * bytePerPixel;
            dstPitches[indexU] = dstPitches[indexV] = width * bytePerPixel / 2;
            break;
        case NvMediaSurfaceType_R8G8B8A8:
        case NvMediaSurfaceType_R8G8B8A8_BottomOrigin:
            FrameSize = width * height * bytePerPixel* 4;
            dstPitches[0] = width * bytePerPixel * 4;
            break;
        default:
            LOG_ERR("WriteFrame: Invalid video surface type %d\n", videoSurface->type);
            goto done;
    }

    NvMediaVideoSurfaceLock(videoSurface, &surfaceMap);
    LOG_DBG("WriteFrame: %s Size: %dx%d Luma pitch: %d Chroma pitch: %d Chroma type: %d\n",
            filename, surfaceMap.lumaWidth, surfaceMap.lumaHeight, surfaceMap.pitchY, surfaceMap.pitchU, videoSurface->type);
    status = NvMediaVideoSurfaceGetBits(videoSurface, NULL, (void **)pDstBuff, dstPitches);
    NvMediaVideoSurfaceUnlock(videoSurface);

    if(status) {
        LOG_ERR("WriteFrame: NvMediaVideoSurfaceGetBits() failed\n");
        goto done;
    }

    // nvmedia 10bit format [000000a6a7 b0b1b2b3b4b5b6b7] b0-b7 is higher bits, a0 - a7 is lower bits
    // yuv 10bit format [a0a1a2a3a4a5a6a7 000000b6b7] a0-a7 is higher bits, b0 - b7 is lower bits
    if(videoSurface->type == NvMediaSurfaceType_Video_420_10bit ||
       videoSurface->type == NvMediaSurfaceType_Video_420_12bit ||
       videoSurface->type == NvMediaSurfaceType_Video_422_10bit ||
       videoSurface->type == NvMediaSurfaceType_Video_422_12bit ||
       videoSurface->type == NvMediaSurfaceType_Video_444_10bit ||
       videoSurface->type == NvMediaSurfaceType_Video_444_12bit) {
       // Y
       psrc = (NvU16*)(pDstBuff[0]);
       size = width * height;
       for(i = 0; i < size; i++) {
            *(psrc + i) = (*(psrc + i)) >> shift;
       }

       // U
       psrc = (NvU16*)(pDstBuff[indexU]);
       size = width * height / (xScale * yScale);
       for(i = 0; i < size; i++) {
            *(psrc + i) = (*(psrc + i)) >> shift;
       }

       // V
       psrc = (NvU16*)(pDstBuff[indexV]);
       size = width * height / (xScale * yScale);
       for(i = 0; i < size; i++) {
            *(psrc + i) = (*(psrc + i)) >> shift;
       }
    }

    switch(videoSurface->type) {
        case NvMediaSurfaceType_YV24:
        case NvMediaSurfaceType_Video_444_10bit:
        case NvMediaSurfaceType_Video_444_12bit:
        case NvMediaSurfaceType_YV16:
        case NvMediaSurfaceType_YV16x2:
        case NvMediaSurfaceType_Video_422_10bit:
        case NvMediaSurfaceType_Video_422_12bit:
        case NvMediaSurfaceType_YV12:
        case NvMediaSurfaceType_Video_420_10bit:
        case NvMediaSurfaceType_Video_420_12bit:
            if(fwrite(pDstBuff[0], width * height * bytePerPixel, 1, file) != 1) {
                LOG_ERR("WriteFrame: file write failed\n");
                goto done;
            }
            pChroma = bOrderUV ? pDstBuff[indexU] : pDstBuff[indexV];
            if(fwrite(pChroma, width * height * bytePerPixel / (xScale * yScale), 1, file) != 1) {
                LOG_ERR("WriteFrame: file write failed\n");
                goto done;
            }
            pChroma = bOrderUV ? pDstBuff[indexV] : pDstBuff[indexU];
            if(fwrite(pChroma, width * height * bytePerPixel / (xScale * yScale), 1, file) != 1) {
                LOG_ERR("WriteFrame: file write failed\n");
                goto done;
            }
            break;
        case NvMediaSurfaceType_R8G8B8A8:
        case NvMediaSurfaceType_R8G8B8A8_BottomOrigin:
            if(fwrite(pDstBuff[0], FrameSize, 1, file) != 1) {
                LOG_ERR("WriteFrame: file write failed\n");
                goto done;
            }
            break;
        default:
            LOG_ERR("WriteFrame: Invalid video surface type %d\n", videoSurface->type);
            goto done;
    }

    result = NVMEDIA_STATUS_OK;

done:
    if(pBuff)
        free(pBuff);

    if(file)
        fclose(file);

    return result;
}

NvMediaStatus
ReadFrame(
    char *fileName,
    NvU32 uFrameNum,
    NvU32 uWidth,
    NvU32 uHeight,
    NvMediaVideoSurface *pFrame,
    NvMediaBool bOrderUV)
{
    NvMediaStatus ret = NVMEDIA_STATUS_OK;
    if(pFrame->type == NvMediaSurfaceType_Video_420) {
        ret = ReadYUVFrame(fileName, uFrameNum, uWidth,
                           uHeight, pFrame, bOrderUV);
    } else if(pFrame->type == NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
        ret = ReadRGBAFrame(fileName, uFrameNum, uWidth,
                            uHeight, pFrame);
    } else {
        LOG_ERR("ReadFrame: Invalid video surface type %d\n", pFrame->type);
        ret = NVMEDIA_STATUS_ERROR;
    }
    return ret;
}

NvMediaStatus
ReadYUVFrame(
    char *fileName,
    NvU32 uFrameNum,
    NvU32 uWidth,
    NvU32 uHeight,
    NvMediaVideoSurface *pFrame,
    NvMediaBool bOrderUV)
{
    NvU8 *pBuff = NULL, *pYBuff, *pUBuff, *pVBuff, *pChroma;
    FILE *file = NULL;
    NvU32 uFrameSize = 0;
    NvMediaVideoSurfaceMap surfaceMap;
    NvU32 uHeightSurface, uWidthSurface, uSurfaceSize;
    NvMediaStatus ret = NVMEDIA_STATUS_OK;
    NvU8 *pYUVBuff[3];
    NvU32 YUVPitch[3];
    unsigned int i;
    int xScale = 2, yScale = 2;
    int bpp = 1;
    int shift = 0; // 16 - bitdepth
    NvU16 *psrc;
    unsigned int size;

    NvMediaVideoSurfaceLock(pFrame, &surfaceMap);

    uHeightSurface = surfaceMap.lumaHeight;
    uWidthSurface  = surfaceMap.lumaWidth;

    switch(pFrame->type) {
    case NvMediaSurfaceType_YV24:
        uSurfaceSize = uHeightSurface * uWidthSurface * 3;
        uFrameSize = uWidth * uHeight * 3;
        xScale = 1;
        yScale = 1;
        break;
    case NvMediaSurfaceType_YV12:
        uSurfaceSize = (uHeightSurface * uWidthSurface * 3) / 2;
        uFrameSize = (uWidth * uHeight * 3) / 2;
        break;
    case NvMediaSurfaceType_Video_420_10bit:
        shift = 6;
        bpp = 2;
        uSurfaceSize = (uHeightSurface * uWidthSurface * 3 * bpp) / 2;
        uFrameSize = (uWidth * uHeight * 3 * bpp) / 2;
        break;
    case NvMediaSurfaceType_Video_444_10bit:
        xScale = 1;
        yScale = 1;
        shift = 6;
        bpp = 2;
        uSurfaceSize = uHeightSurface * uWidthSurface * 3 * bpp;
        uFrameSize = uWidth * uHeight * 3 * bpp;
        break;
    default:
        LOG_ERR("ReadYUVFrame: Invalid video surface type %d\n", pFrame->type);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    if(!pFrame || uWidth > uWidthSurface || uHeight > uHeightSurface)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    pBuff = malloc(uSurfaceSize);
    if(!pBuff) {
        LOG_ERR("ReadYUVFrame: Failed to allocate image buffer\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    memset(pBuff, 0x10, uWidthSurface * uHeightSurface * bpp);
    memset(pBuff + uWidthSurface * uHeightSurface * bpp, 0x80, (uHeightSurface * uWidthSurface * bpp * 2) / (xScale * yScale));

    pYBuff = pBuff;

    //YVU order in the buffer
    pVBuff = pYBuff + uWidthSurface * uHeightSurface * bpp;
    pUBuff = pVBuff + uWidthSurface * uHeightSurface * bpp / (xScale * yScale);

    pYUVBuff[0] = pYBuff;
    pYUVBuff[1] = pUBuff;
    pYUVBuff[2] = pVBuff;

    YUVPitch[0] = uWidthSurface * bpp;
    YUVPitch[1] = uWidthSurface * bpp / xScale;
    YUVPitch[2] = uWidthSurface * bpp / xScale;

    file = fopen(fileName, "rb");
    if(!file) {
        LOG_ERR("ReadYUVFrame: Error opening file: %s\n", fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    if(fseeko(file, uFrameNum * (off_t)uFrameSize, SEEK_SET)) {
        LOG_ERR("ReadYUVFrame: Error seeking file: %s\n", fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }
    //read Y U V separately
    for(i = 0; i < uHeight; i++) {
        if(fread(pYBuff, uWidth * bpp, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %s\n", fileName);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pYBuff += uWidthSurface * bpp;
    }

    pChroma = bOrderUV ? pUBuff : pVBuff;
    for(i = 0; i < uHeight / yScale; i++) {
        if(fread(pChroma, uWidth * bpp / xScale, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %s\n", fileName);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pChroma += uWidthSurface * bpp / xScale;
    }

    pChroma = bOrderUV ? pVBuff : pUBuff;
    for(i = 0; i < uHeight / yScale; i++) {
        if(fread(pChroma, uWidth * bpp / xScale, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %s\n", fileName);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pChroma += uWidthSurface * bpp / xScale;
    }

    // nvmedia 10bit format [000000a6a7 b0b1b2b3b4b5b6b7] b0-b7 is higher bits, a0 - a7 is lower bits
    // yuv 10bit format [a0a1a2a3a4a5a6a7 000000b6b7] a0-a7 is higher bits, b0 - b7 is lower bits
    if(pFrame->type == NvMediaSurfaceType_Video_420_10bit ||
       pFrame->type == NvMediaSurfaceType_Video_420_12bit ||
       pFrame->type == NvMediaSurfaceType_Video_422_10bit ||
       pFrame->type == NvMediaSurfaceType_Video_422_12bit ||
       pFrame->type == NvMediaSurfaceType_Video_444_10bit ||
       pFrame->type == NvMediaSurfaceType_Video_444_12bit) {
       // Y
       psrc = (NvU16*)(pYUVBuff[0]);
       size = uHeightSurface * uWidthSurface;
       for(i = 0; i < size; i++) {
            *(psrc + i) = (*(psrc + i)) << shift;
       }

       // U
       psrc = (NvU16*)(pYUVBuff[1]);
       size = uHeightSurface * uWidthSurface / (xScale * yScale);
       for(i = 0; i < size; i++) {
            *(psrc + i) = (*(psrc + i)) << shift;
       }

       // V
       psrc = (NvU16*)(pYUVBuff[2]);
       size = uHeightSurface * uWidthSurface / (xScale * yScale);
       for(i = 0; i < size; i++) {
            *(psrc + i) = (*(psrc + i)) << shift;
       }
    }

    NvMediaVideoSurfacePutBits(pFrame, NULL, (void **)pYUVBuff, YUVPitch);
    NvMediaVideoSurfaceUnlock(pFrame);

done:
    if(pBuff)
        free(pBuff);

    if(file)
        fclose(file);

    return ret;
}

NvMediaStatus
ReadRGBAFrame(
    char *fileName,
    NvU32 uFrameNum,
    NvU32 uWidth,
    NvU32 uHeight,
    NvMediaVideoSurface *pFrame)
{

    NvU8 *pBuff,*pRGBA;
    FILE *file;
    NvU32 uFrameSize = (uWidth * uHeight * 4);
    NvMediaVideoSurfaceMap surfaceMap;
    NvU32 uHeightSurface, uWidthSurface, uSurfaceSize;
    NvMediaStatus ret = NVMEDIA_STATUS_OK;
    NvU8 *pRGBABuff[3];
    NvU32 RGBAPitch[3];
    unsigned int i;

    NvMediaVideoSurfaceLock(pFrame, &surfaceMap);

    uHeightSurface = surfaceMap.lumaHeight;
    uWidthSurface  = surfaceMap.lumaWidth;
    /*Above doesn't work*/
    uHeightSurface = pFrame->height;
    uWidthSurface  = pFrame->width;
    uSurfaceSize = (uHeightSurface * uWidthSurface * 4) ;
    RGBAPitch[0] = uWidthSurface*4;

    if(!pFrame || uWidth > uWidthSurface || uHeight > uHeightSurface)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    pBuff = malloc(uSurfaceSize);
    if(!pBuff) {
        LOG_ERR("ReadRGBAFrame: Failed to allocate image buffer\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }
    pRGBA = pBuff;

    memset(pBuff, 0x10, uSurfaceSize);

    pRGBABuff[0] = pRGBA;

    file = fopen(fileName, "rb");
    if(!file) {
        LOG_ERR("ReadRGBAFrame: Error opening file: %s\n", fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    if(fseek(file, uFrameNum * uFrameSize, SEEK_SET)) {
        LOG_ERR("ReadRGBAFrame: Error seeking file: %s\n", fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    //read RGBA
    for(i = 0; i < uHeight; i++) {
        if(fread(pRGBA, uWidth*4, 1, file) != 1) {
            LOG_ERR("ReadRGBAFrame: Error reading file: %s\n", fileName);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pRGBA += (uWidthSurface*4);
    }

    NvMediaVideoSurfacePutBits(pFrame, NULL, (void **)pRGBABuff, RGBAPitch);
    NvMediaVideoSurfaceUnlock(pFrame);

done:
    if(pBuff)
        free(pBuff);

    if(file)
        fclose(file);

    return ret;
}

NvMediaStatus
CreateMemRGBASurf(
    unsigned int width,
    unsigned int height,
    NvMediaBool initflag,
    unsigned int initvalue,
    MemSurf **surf_out)
{
    MemSurf *surf;
    unsigned int *p;
    unsigned int pixels;

    surf = malloc(sizeof(MemSurf));
    if(!surf) {
        LOG_ERR("CreateMemRGBASurf: Cannot allocate surface structure\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    memset(surf, 0, sizeof(MemSurf));
    surf->pSurf = malloc(width * height * 4);
    if(!surf->pSurf) {
        free(surf);
        LOG_ERR("CreateMemRGBASurf: Cannot allocate surface\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }
    surf->width = width;
    surf->height = height;
    surf->pitch = width * 4;
    surf->bpp = 4;

    pixels = width * height;

    if(initflag) {
        p = (unsigned int *)(void *)surf->pSurf;
        while(pixels--) {
            *p++ = initvalue;
        }
    }
    *surf_out = surf;
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
DestroyMemSurf(
    MemSurf *surf)
{
    if(surf) {
        if(surf->pSurf)
            free(surf->pSurf);
        free(surf);
    }
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
DrawRGBARect(
    MemSurf *surf,
    NvMediaRect *rect,
    NvU8 R,
    NvU8 G,
    NvU8 B,
    NvU8 A)
{
    NvU32 color;
    NvU32 lines;
    NvU32 pixels;
    NvU8 *pPixelBase;

    if(!surf || !rect)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    color = PACK_RGBA(R, G, B, A);
    lines = rect->y1 - rect->y0;
    pixels = rect->x1 - rect->x0;
    pPixelBase = surf->pSurf + rect->y0 * surf->pitch + rect->x0 * 4;

    while(lines--) {
        NvU32 i;
        NvU32 *pPixel = (NvU32 *)(void *)pPixelBase;
        for(i = 0; i < pixels; i++) {
            *pPixel++ = color;
        }

        pPixelBase += surf->pitch;
    }
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
PreMultiplyRGBASurf(
    MemSurf *surf)
{
    NvU32 pixels;
    NvU8 *p;
    NvU8 alpha;

    if(!surf)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    pixels = surf->width * surf->height;
    p = (NvU8 *)(void *)surf->pSurf;

    while(pixels--) {
        alpha = *(p + 3);
        *p = ((NvU16)*p * (NvU16)alpha + 128) >> 8;
        p++;
        *p = ((NvU16)*p * (NvU16)alpha + 128) >> 8;
        p++;
        *p = ((NvU16)*p * (NvU16)alpha + 128) >> 8;
        p++;
        p++;
    }
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CreateMemI8Surf(
    NvU32 width,
    NvU32 height,
    NvU8 init,
    MemSurf **surf_out)
{
    MemSurf *surf;

    surf = malloc(sizeof(MemSurf));
    if(!surf) {
        LOG_ERR("CreateMemI8Surf: Cannot allocate surface structure\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    memset(surf, 0, sizeof(MemSurf));
    surf->pSurf = malloc(width * height);
    if(!surf->pSurf) {
        free(surf);
        LOG_ERR("CreateMemI8Surf: Cannot allocate surface\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }
    surf->width = width;
    surf->height = height;
    surf->pitch = width;
    surf->bpp = 1;

    memset(surf->pSurf, init, width * height);

    *surf_out = surf;
    return NVMEDIA_STATUS_OK;
}


NvMediaStatus
DrawI8Rect(
    MemSurf *surf,
    NvMediaRect *rect,
    NvU8 index)
{
    NvU32 lines, pixels;
    NvU8 *pPixelBase;

    if(!surf || !rect)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    lines = rect->y1 - rect->y0;
    pixels = rect->x1 - rect->x0;
    pPixelBase = surf->pSurf + rect->y0 * surf->pitch + rect->x0;

    while(lines--) {
        memset(pPixelBase, index, pixels);
        pPixelBase += surf->pitch;
    }
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ReadRGBA(
    char *filename,
    unsigned int width,
    unsigned int height,
    MemSurf *rgbaSurface)
{
    FILE *file;

    if(!rgbaSurface || !rgbaSurface->pSurf)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    file = fopen(filename, "rb");
    if(!file) {
        LOG_ERR("ReadRGBA: Failed opening file %s\n", filename);
        return NVMEDIA_STATUS_ERROR;
    }

    if(fread(rgbaSurface->pSurf, width * height * 4, 1, file) != 1) {
        LOG_ERR("ReadRGBA: Failed reading file %s\n", filename);
        return NVMEDIA_STATUS_ERROR;
    }

    fclose(file);

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
WriteRGBA(
    char *filename,
    NvU32 outputBpp,
    NvU8 defaultAplha,
    MemSurf *rgbaSurface)
{
    NvU32 width = rgbaSurface->width;
    NvU32 height = rgbaSurface->height;
    NvMediaStatus ret = NVMEDIA_STATUS_ERROR;
    NvU8 *lineBuff = NULL;
    NvU32 i;
    FILE *f = fopen(filename, "wb");
    if(!f) {
        LOG_ERR("WriteRGBA: Cannot create file: %s", filename);
        goto WriteRGBA_end;
    }

    lineBuff = malloc(width * outputBpp);
    if(!lineBuff) {
        LOG_ERR("WriteRGBA: Error allocating line buffer");
        goto WriteRGBA_end;
    }

    for(i = 0; i < height; i++) {
        NvU32 j;
        NvU8 *srcBuff = rgbaSurface->pSurf + i * rgbaSurface->pitch;
        NvU8 *dstBuff = lineBuff;

        for(j = 0; j < width; j++) {
            dstBuff[0] = srcBuff[0]; // R
            dstBuff[1] = srcBuff[1]; // G
            dstBuff[2] = srcBuff[2]; // B
            if(outputBpp == 4) {
                dstBuff[3] = rgbaSurface->bpp == 3 ? defaultAplha : srcBuff[3];
            }
            srcBuff += rgbaSurface->bpp;
            dstBuff += outputBpp;
        }
        if(fwrite(lineBuff, width * outputBpp, 1, f) != 1) {
            LOG_ERR("WriteRGBA: Error writing file: %s", filename);
            goto WriteRGBA_end;
        }
    }

    ret = NVMEDIA_STATUS_OK;
WriteRGBA_end:
    if(lineBuff)
        free(lineBuff);
    if(f)
        fclose(f);

    return ret;
}

NvMediaStatus
GetPPMFileDimensions(
    char *fileName,
    NvU16 *uWidth,
    NvU16 *uHeight)
{
    NvU32 uFileWidth;
    NvU32 uFileHeight;
    char buf[256], *t;
    FILE *file;

    file = fopen(fileName, "rb");
    if(!file) {
        LOG_ERR("GetPPMFileDimensions: Error opening file: %s\n", fileName);
        return NVMEDIA_STATUS_ERROR;
    }

    t = fgets(buf, 256, file);
    if(!t || strncmp(buf, "P6\n", 3)) {
        LOG_ERR("GetPPMFileDimensions: Invalid PPM header in file: %s\n", fileName);
        return NVMEDIA_STATUS_ERROR;
    }
    do {
        t = fgets(buf, 255, file);
        if(!t) {
            LOG_ERR("GetPPMFileDimensions: Invalid PPM header in file: %s\n", fileName);
            return NVMEDIA_STATUS_ERROR;
        }
    } while(!strncmp(buf, "#", 1));
    if(sscanf(buf, "%u %u", &uFileWidth, &uFileHeight) != 2) {
        LOG_ERR("GetPPMFileDimensions: Error getting PPM file resolution for the file: %s\n", fileName);
        return NVMEDIA_STATUS_ERROR;
    }
    if(uFileWidth < 16 || uFileWidth > 2048 ||
            uFileHeight < 16 || uFileHeight > 2048) {
        LOG_ERR("GetPPMFileDimensions: Invalid PPM file resolution: %ux%u in file: %s\n", uFileWidth, uFileHeight, fileName);
        return NVMEDIA_STATUS_ERROR;
    }

    fclose(file);

    *uWidth  = uFileWidth;
    *uHeight = uFileHeight;

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ReadPPMFrame(
    char *fileName,
    NvMediaVideoSurface *pFrame)
{
    NvU32 uSurfaceWidth;
    NvU32 uSurfaceHeight;
    NvU32 maxValue = 0;
    int num = 0;
    NvU32 x, y;
    char buf[256], *c;
    NvU8 *pBuff = NULL, *pRGBBuff;
    NvU32 uFrameSize;
    FILE *file;
    NvMediaVideoSurfaceMap surfaceMap;
    NvMediaStatus ret = NVMEDIA_STATUS_OK;

    if(!pFrame) {
        LOG_ERR("ReadPPMFrame: Failed allocating memory\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    file = fopen(fileName, "rb");
    if(!file) {
        LOG_ERR("ReadPPMFrame: Error opening file: %s\n", fileName);
        return NVMEDIA_STATUS_ERROR;
    }

    c = fgets(buf, 256, file);
    if(!c || strncmp(buf, "P6\n", 3)) {
        LOG_ERR("ReadPPMFrame: Invalid PPM header in file: %s\n", fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }
    do {
        c = fgets(buf, 255, file);
        if(!c) {
            LOG_ERR("ReadPPMFrame: Invalid PPM header in file: %s\n", fileName);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
    } while(!strncmp(buf, "#", 1));

    num = sscanf(buf, "%u %u %u", &uSurfaceWidth, &uSurfaceHeight, &maxValue);
    switch(num) {
    case 2:
        c = fgets(buf, 255, file);
        if(!c || strncmp(buf, "255\n", 4)) {
            LOG_ERR("ReadPPMFrame: Invalid PPM header in file: %s\n", fileName);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        } else
            break;
    case 3:
        if(maxValue != 255) {
            LOG_ERR("ReadPPMFrame: Invalid PPM header in file: %s\n", fileName);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        } else
            break;
    default:
        LOG_ERR("ReadPPMFrame: Error getting PPM file resolution in file: %s\n", fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    if(uSurfaceWidth < 16 || uSurfaceWidth > 2048 ||
            uSurfaceHeight < 16 || uSurfaceHeight > 2048) {
        LOG_ERR("ReadPPMFrame: Invalid PPM file resolution: %ux%u for file: %s\n", uSurfaceWidth, uSurfaceHeight, fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    uFrameSize = uSurfaceWidth * uSurfaceHeight * 3;
    pBuff = malloc(uFrameSize);
    if(!pBuff) {
        LOG_ERR("ReadPPMFrame: Out of memory\n");
        ret = NVMEDIA_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    pRGBBuff = pBuff;

    if(fread(pRGBBuff, uFrameSize, 1, file) != 1) {
        LOG_ERR("ReadPPMFrame: Error reading file: %s\n", fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    NvMediaVideoSurfaceLock(pFrame, &surfaceMap);

    for(y = 0; y < uSurfaceHeight; y++) {
        NvU8 *pPixel = (NvU8 *)surfaceMap.pRGBA + surfaceMap.pitchRGBA * y;
        for(x = 0; x < uSurfaceWidth; x++) {
            *pPixel++ = *pRGBBuff++; // R
            *pPixel++ = *pRGBBuff++; // G
            *pPixel++ = *pRGBBuff++; // B
            *pPixel++ = 255;         // Alpha
        }
    }

done:
    NvMediaVideoSurfaceUnlock(pFrame);
    if(pBuff) free(pBuff);
    if(file) fclose(file);

    return ret;
}

NvMediaStatus
ReadPPM(
    char *fileName,
    NvU8 defaultAplha,
    MemSurf *rgbaSurface)
{
    NvU32 surfaceWidth;
    NvU32 surfaceHeight;
    NvU32 data = 0;
    char buf[256], *t;
    NvU8 *lineBuff = NULL;
    NvU32 i;
    NvMediaStatus ret = NVMEDIA_STATUS_ERROR;

    LOG_DBG("ReadPPM: Start - File: %s", fileName);

    FILE *f = fopen(fileName, "rb");
    if(!f) {
        LOG_ERR("ReadPPM: Error opening file: %s", fileName);
        goto ReadPPM_end;
    }

    t = fgets(buf, 256, f);
    if(!t || strncmp(buf, "P6\n", 3)) {
        LOG_ERR("ReadPPM: Invalid PPM header: %s", fileName);
        goto ReadPPM_end;
    }
    do {
        t = fgets(buf, 255, f);
        if(!t) {
            LOG_ERR("ReadPPM: Invalid PPM header: %s", fileName);
            goto ReadPPM_end;
        }
    } while(!strncmp(buf, "#", 1));
    if(sscanf(buf, "%u %u %u\n", &surfaceWidth, &surfaceHeight, &data) != 3) {
        LOG_ERR("ReadPPM: Error getting PPM file resolution - file: %s string: %s", fileName, buf);
        goto ReadPPM_end;
    }
    if(data != 255) {
        LOG_ERR("ReadPPM: Invalid PPM header (data: %u) resolution: %dx%d string: %s", data, surfaceWidth, surfaceHeight, buf);
        goto ReadPPM_end;
    }

    lineBuff = malloc(surfaceWidth * 3);
    if(!lineBuff) {
        LOG_ERR("ReadPPM: Error allocating line buffer");
        goto ReadPPM_end;
    }

    for(i = 0; i < surfaceHeight; i++) {
        NvU32 j;
        NvU8 *srcBuff = lineBuff;
        NvU8 *dstBuff = rgbaSurface->pSurf + i * rgbaSurface->pitch;

        if(fread(lineBuff, surfaceWidth * 3, 1, f) != 1) {
            LOG_ERR("ReadPPM: Error reading file: %s", fileName);
            goto ReadPPM_end;
        }
        for(j = 0; j < surfaceWidth; j++) {
            dstBuff[0] = srcBuff[0]; // R
            dstBuff[1] = srcBuff[1]; // G
            dstBuff[2] = srcBuff[2]; // B
            if(rgbaSurface->bpp == 4)
                dstBuff[3] = defaultAplha;
            srcBuff += 3;
            dstBuff += rgbaSurface->bpp;
        }
    }

    ret = NVMEDIA_STATUS_OK;
ReadPPM_end:
    if(lineBuff)
        free(lineBuff);
    if(f)
        fclose(f);

    LOG_DBG("ReadPPM: End");

    return ret;
}

NvMediaStatus
WritePPM(
    char *fileName,
    MemSurf *rgbaSurface)
{
    NvU8 *lineBuff = NULL;
    NvU32 i;
    char header[256];
    NvMediaStatus ret = NVMEDIA_STATUS_ERROR;

    LOG_DBG("WritePPM: Start - File: %s", fileName);

    FILE *f = fopen(fileName, "wb");
    if(!f) {
        LOG_ERR("WritePPM: Error opening file: %s", fileName);
        goto WritePPM_end;
    }

    sprintf(header, "P6\n# NVIDIA\n%u %u %u\n", rgbaSurface->width, rgbaSurface->height, 255);

    if(fwrite(header, strlen(header), 1, f) != 1) {
        LOG_ERR("WritePPM: Error writing PPM file header: %s", fileName);
        goto WritePPM_end;
    }

    lineBuff = malloc(rgbaSurface->width * 3);
    if(!lineBuff) {
        LOG_ERR("WritePPM: Error allocating line buffer");
        goto WritePPM_end;
    }

    for(i = 0; i < rgbaSurface->height; i++) {
        NvU32 j;
        NvU8 *srcBuff = rgbaSurface->pSurf + i * rgbaSurface->pitch;
        NvU8 *dstBuff = lineBuff;

        for(j = 0; j < rgbaSurface->width; j++) {
            dstBuff[0] = srcBuff[0]; // R
            dstBuff[1] = srcBuff[1]; // G
            dstBuff[2] = srcBuff[2]; // B
            srcBuff += rgbaSurface->bpp;
            dstBuff += 3;
        }
        if(fwrite(lineBuff, rgbaSurface->width * 3, 1, f) != 1) {
            LOG_ERR("WritePPM: Error writing file: %s", fileName);
            goto WritePPM_end;
        }
    }

    ret = NVMEDIA_STATUS_OK;
WritePPM_end:
    if(lineBuff)
        free(lineBuff);
    if(f)
        fclose(f);

    LOG_DBG("WritePPM: End");

    return ret;
}

NvMediaStatus
ReadPAL(
    char *filename,
    NvU32 *palette)
{
    NvMediaStatus ret = NVMEDIA_STATUS_ERROR;

    FILE *f = fopen(filename, "rb");
    if(!f) {
        LOG_ERR("ReadPAL: File: %s does not exist", filename);
        goto ReadPAL_end;
    }

    if(fread(palette, 256 * 4, 1, f) != 1) {
        LOG_ERR("ReadPAL: Error reading file: %s", filename);
        goto ReadPAL_end;
    }

    ret = NVMEDIA_STATUS_OK;
ReadPAL_end:
    if(f)
        fclose(f);

    return ret;
}

NvMediaStatus
ReadI8(
    char *filename,
    MemSurf *dstSurface)
{
    NvMediaStatus ret = NVMEDIA_STATUS_ERROR;
    NvU32 i;
    NvU8 *dst;

    FILE *f = fopen(filename, "rb");
    if(!f) {
        LOG_ERR("ReadI8: File: %s does not exist", filename);
        goto ReadI8_end;
    }

    dst = dstSurface->pSurf;
    for(i = 0; i < dstSurface->height; i++) {
        if(fread(dst, dstSurface->width, 1, f) != 1) {
            LOG_ERR("ReadI8: Error reading file: %s", filename);
            goto ReadI8_end;
        }
        dst += dstSurface->pitch;
    }

    ret = NVMEDIA_STATUS_OK;
ReadI8_end:
    if(f)
        fclose(f);

    return ret;
}

NvMediaStatus
GetSurfaceCrc(
    NvMediaVideoSurface *surf,
    NvU32 width,
    NvU32 height,
    NvMediaBool monochromeFlag,
    NvU32 *crcOut)
{
    NvMediaVideoSurfaceMap surfMap;
    NvU32 lines, crc = 0;
    NvU8 *pBuff = NULL, *pDstBuff[3] = {NULL};
    NvU32 dstPitches[3];
    NvU8 *pY, *pV, *pU;
    NvU32 *pitchY, *pitchV, *pitchU;
    NvU32 bytePerPixel = 1;
    NvMediaStatus status;

    if(!surf || !crcOut) {
        LOG_ERR("GetSurfaceCrc: Bad parameter\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(NvMediaVideoSurfaceLock(surf, &surfMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("CheckSurfaceCrc: NvMediaVideoSurfaceLock failed\n");
        return NVMEDIA_STATUS_ERROR;
    }

    switch(surf->type) {
        case NvMediaSurfaceType_Video_420_10bit:
        case NvMediaSurfaceType_Video_420_12bit:
        case NvMediaSurfaceType_Video_422_10bit:
        case NvMediaSurfaceType_Video_422_12bit:
        case NvMediaSurfaceType_Video_444_10bit:
        case NvMediaSurfaceType_Video_444_12bit:
            bytePerPixel = 2;
            break;
        default:
            bytePerPixel = 1;
            break;
    }

    pBuff = malloc(width * height * bytePerPixel * 4);
    if(!pBuff) {
        LOG_ERR("CheckSurfaceCrc: Failed to allocate frame buffer\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    pDstBuff[0] = pBuff;
    pY = pDstBuff[0];
    pV = pDstBuff[1];
    pU = pDstBuff[2];
    pitchY = &dstPitches[0];
    pitchV = &dstPitches[1];
    pitchU = &dstPitches[2];

    switch(surf->type) {
        case NvMediaSurfaceType_YV24:
        case NvMediaSurfaceType_Video_444_10bit:
        case NvMediaSurfaceType_Video_444_12bit:
            pU = pY + width * height * bytePerPixel;
            pV = pU + width * height * bytePerPixel;
            *pitchY = *pitchV = *pitchU = width * bytePerPixel;
            break;
        case NvMediaSurfaceType_YV16:
        case NvMediaSurfaceType_YV16x2:
        case NvMediaSurfaceType_Video_422_10bit:
        case NvMediaSurfaceType_Video_422_12bit:
            pU = pY + width * height * bytePerPixel;
            pV = pU + width * height * bytePerPixel / 2;
            *pitchY = width * bytePerPixel;
            *pitchV = *pitchU = width * bytePerPixel / 2;
            break;
        case NvMediaSurfaceType_YV12:
        case NvMediaSurfaceType_Video_420_10bit:
        case NvMediaSurfaceType_Video_420_12bit:
            pU = pY + width * height * bytePerPixel;
            pV = pU + width * height * bytePerPixel / 4;
            *pitchY = width * bytePerPixel;
            *pitchV = *pitchU = width * bytePerPixel / 2;
            break;
        case NvMediaSurfaceType_R8G8B8A8:
        case NvMediaSurfaceType_R8G8B8A8_BottomOrigin:
            dstPitches[0] = width * 4;
            break;
        default:
            LOG_ERR("CheckSurfaceCrc: Invalid video surface type\n");
            free(pBuff);
            return NVMEDIA_STATUS_ERROR;
    }

    status = NvMediaVideoSurfaceGetBits(surf, NULL, (void **)pDstBuff, &dstPitches[0]);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("CheckSurfaceCrc: NvMediaVideoSurfaceGetBits failed\n");
        free(pBuff);
        return NVMEDIA_STATUS_ERROR;
    }
    NvMediaVideoSurfaceUnlock(surf);

    switch(surf->type) {
        case NvMediaSurfaceType_YV24:
        case NvMediaSurfaceType_YV16:
        case NvMediaSurfaceType_YV16x2:
        case NvMediaSurfaceType_YV12:
        case NvMediaSurfaceType_Video_420_10bit:
        case NvMediaSurfaceType_Video_420_12bit:
        case NvMediaSurfaceType_Video_422_10bit:
        case NvMediaSurfaceType_Video_422_12bit:
        case NvMediaSurfaceType_Video_444_10bit:
        case NvMediaSurfaceType_Video_444_12bit:
            lines = height;
            while(lines--) {
                crc = CalculateBufferCRC(*pitchY, crc, pY);
                pY += *pitchY;
            }

            if (monochromeFlag != NV_TRUE) {
                lines = height / 2;
                while(lines--) {
                    crc = CalculateBufferCRC(*pitchV, crc, pV);
                    pV += *pitchV;
                }
                lines = height / 2;
                while(lines--) {
                    crc = CalculateBufferCRC(*pitchU, crc, pU);
                    pU += *pitchU;
                }
            }
            break;
        case NvMediaSurfaceType_R8G8B8A8:
        case NvMediaSurfaceType_R8G8B8A8_BottomOrigin:
            lines = height;
            while(lines--) {
                crc = CalculateBufferCRC(dstPitches[0], crc, pDstBuff[0]);
                pDstBuff[0] += dstPitches[0];
            }
            break;
        default:
            LOG_ERR("CheckSurfaceCrc: Invalid video surface type\n");
            free(pBuff);
            return NVMEDIA_STATUS_ERROR;
    }

    *crcOut = crc;

    free(pBuff);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CheckSurfaceCrc(
    NvMediaVideoSurface *surf,
    NvU32 width,
    NvU32 height,
    NvMediaBool monochromeFlag,
    NvU32 ref,
    NvMediaBool *isMatching)
{
    NvU32 crc = 0;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    status = GetSurfaceCrc(surf, width, height, monochromeFlag, &crc);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("CheckSurfaceCrc: GetSurfaceCrc failed\n");
        return NVMEDIA_STATUS_ERROR;
    }

    if(crc != ref) {
        LOG_WARN("CheckSurfaceCrc: Encountered CRC mismatch.\n");
        LOG_WARN("CheckSurfaceCrc: Calculated CRC: %8x (%d). Expected CRC: %8x (%d).\n", crc, crc, ref, ref);
        *isMatching = NVMEDIA_FALSE;
    } else {
        *isMatching = NVMEDIA_TRUE;
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
GetImageCrc(
    NvMediaImage *image,
    NvU32 width,
    NvU32 height,
    NvU32 *crcOut,
    NvU32 rawBytesPerPixel)
{
    NvMediaImageSurfaceMap surfaceMap;
    NvU32 lines, crc = 0;
    NvU8 *pBuff = NULL, *pYUVBuff[3] = {NULL};
    NvU32 YUVPitch[3];
    NvU8 *pYBuff = NULL, *pUBuff = NULL, *pVBuff = NULL;
    NvU32 uHeightSurface, uWidthSurface, imageSize;
    NvU32 xScale = 1, yScale = 1;
    NvMediaStatus status;

    if(!image || !crcOut) {
        LOG_ERR("GetImageCrc: Bad parameter\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("GetImageCrc: NvMediaImageLock failed\n");
        return NVMEDIA_STATUS_ERROR;
    }

    uHeightSurface = surfaceMap.height;
    uWidthSurface  = surfaceMap.width;

    if(width > uWidthSurface || height > uHeightSurface) {
        LOG_ERR("GetImageCrc: Bad parameters\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pBuff = malloc(uHeightSurface * uWidthSurface * 4);
    if(!pBuff) {
        LOG_ERR("GetImageCrc: Failed to allocate frame buffer\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    switch(image->type) {
        case NvMediaSurfaceType_Image_YUV_444:
            YUVPitch[0] = uWidthSurface;
            imageSize = width * height * 3;
            memset(pBuff, 0x10, uWidthSurface * uHeightSurface);
            memset(pBuff + uWidthSurface * uHeightSurface, 0x80, (uHeightSurface * uWidthSurface) * 2);
            //YVU order in the buffer
            xScale = 1;
            yScale = 1;
            break;
        case NvMediaSurfaceType_Image_YUV_422:
        case NvMediaSurfaceType_Image_YUYV_422:
            YUVPitch[0] = uWidthSurface;
            imageSize = 2 * (width * height);
            memset(pBuff, 0x10, uWidthSurface * uHeightSurface);
            memset(pBuff + uWidthSurface * uHeightSurface, 0x80, (uHeightSurface * uWidthSurface));
            xScale = 2;
            yScale = 1;
            break;
        case NvMediaSurfaceType_Image_YUV_420:
            YUVPitch[0] = uWidthSurface;
            imageSize = (width * height * 3) / 2;
            memset(pBuff, 0x10, uWidthSurface * uHeightSurface);
            memset(pBuff + uWidthSurface * uHeightSurface, 0x80, (uHeightSurface * uWidthSurface) / 2);
            xScale = 2;
            yScale = 2;
            break;
        case NvMediaSurfaceType_Image_RGBA:
            imageSize = uWidthSurface * uHeightSurface * 4;
            memset(pBuff, 0x10, imageSize);
            YUVPitch[0] = width * 4;
            break;
        case NvMediaSurfaceType_Image_RAW:
            imageSize = uWidthSurface * uHeightSurface * rawBytesPerPixel;
            memset(pBuff, 0x10, imageSize);
            YUVPitch[0] = width * rawBytesPerPixel;
            break;
        default:
            LOG_ERR("GetImageCrc: Invalid image surface type, not supported\n");
            free(pBuff);
            return NVMEDIA_STATUS_ERROR;
    }

    YUVPitch[1] = uWidthSurface / xScale;
    YUVPitch[2] = uWidthSurface / xScale;

    pYBuff = pBuff;
    pUBuff = pYBuff + uWidthSurface * uHeightSurface;
    pVBuff = pUBuff + (uWidthSurface * uHeightSurface) / (xScale * yScale);

    pYUVBuff[0] = pYBuff;
    pYUVBuff[1] = pUBuff;
    pYUVBuff[2] = pVBuff;

    status = NvMediaImageGetBits(image, NULL, (void **)pYUVBuff, &YUVPitch[0]);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("GetImageCrc: NvMediaVideoSurfaceGetBits failed\n");
        free(pBuff);
        return NVMEDIA_STATUS_ERROR;
    }
    NvMediaImageUnlock(image);
    switch(image->type) {
        case NvMediaSurfaceType_Image_YUV_444:
        case NvMediaSurfaceType_Image_YUV_422:
        case NvMediaSurfaceType_Image_YUYV_422:
        case NvMediaSurfaceType_Image_YUV_420:
            lines = height;
            while(lines--) {
                crc = CalculateBufferCRC(YUVPitch[0], crc, pYBuff);
                pYBuff += YUVPitch[0];
            }

            lines = height / 2;
            while(lines--) {
                crc = CalculateBufferCRC(YUVPitch[1], crc, pVBuff);
                pVBuff += YUVPitch[1];
            }
            lines = height / 2;
            while(lines--) {
               crc = CalculateBufferCRC(YUVPitch[2], crc, pUBuff);
               pUBuff += YUVPitch[2];
            }
            break;
        case NvMediaSurfaceType_Image_RGBA:
            lines = height;
            while(lines--) {
                crc = CalculateBufferCRC(YUVPitch[0], crc, pYUVBuff[0]);
                pYUVBuff[0] += YUVPitch[0];
            }
            break;
        case NvMediaSurfaceType_Image_RAW:
            lines = height + ((image->embeddedDataTopSize + image->embeddedDataBottomSize)/(width * rawBytesPerPixel));
            while(lines--) {
                crc = CalculateBufferCRC(YUVPitch[0], crc, pYUVBuff[0]);
                pYUVBuff[0] += YUVPitch[0];
            }
            break;
        default:
            LOG_ERR("GetImageCrc: Invalid image type, not supported\n");
            free(pBuff);
            return NVMEDIA_STATUS_ERROR;
    }

    *crcOut = crc;
    free(pBuff);
    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
CheckImageCrc(
    NvMediaImage *image,
    NvU32 width,
    NvU32 height,
    NvU32 ref,
    NvMediaBool *isMatching,
    NvU32 rawBytesPerPixel)
{
    NvU32 crc = 0;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    status = GetImageCrc(image, width, height, &crc, rawBytesPerPixel);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("CheckImageCrc: GetImageCrc failed\n");
        return NVMEDIA_STATUS_ERROR;
    }

    if(crc != ref) {
        LOG_WARN("CheckImageCrc: Encountered CRC mismatch.\n");
        LOG_WARN("CheckImageCrc: Calculated CRC: %8x (%d). Expected CRC: %8x (%d).\n", crc, crc, ref, ref);
        *isMatching = NVMEDIA_FALSE;
    } else {
        *isMatching = NVMEDIA_TRUE;
    }

    return NVMEDIA_STATUS_OK;
}


NvMediaStatus
WriteRAWImageToRGBA(
    char *filename,
    NvMediaImage *image,
    NvMediaBool appendFlag,
    NvU32 bytesPerPixel)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvMediaImageSurfaceMap surfaceMap;
    NvU32 width, height;
    FILE *file = NULL;
    NvU32 *rgbaBuffer;
    NvU32 i, j;
    NvU8 *rawBuffer;
    NvU16 *evenLine;
    NvU16 *oddLine;

    if(!image || !filename) {
        LOG_ERR("WriteRAWImageToRGBA: Bad parameter\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("WriteRAWImageToRGBA: NvMediaImageLock failed\n");
        return NVMEDIA_STATUS_ERROR;
    }

    height = surfaceMap.height;
    width  = surfaceMap.width;

    if(!(file = fopen(filename, appendFlag ? "ab" : "wb"))) {
        LOG_ERR("WriteRAWImageToRGBA: file open failed: %s\n", filename);
        perror(NULL);
        return NVMEDIA_STATUS_ERROR;
    }

    if(!(rgbaBuffer = calloc(1, width * height * 4))) {
        LOG_ERR("WriteRAWImageToRGBA: Out of memory\n");
        fclose(file);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    rawBuffer = (NvU8 *)surfaceMap.surface[0].mapping;

    for(j = 0; j < height - 2; j = j + 2) {
        evenLine = (NvU16 *)rawBuffer;
        oddLine = (NvU16 *)(rawBuffer + surfaceMap.surface[0].pitch);
        for(i = 0; i < width - 2; i += 2) {
            rgbaBuffer[i + j * width] = ((NvU32)(oddLine[i + 1] >> 6)) |              // R
                                         ((NvU32)(evenLine[i + 1] >> 6) << 8) |       // G
                                         ((NvU32)(evenLine[i] >> 6) << 16) |          // B
                                         0xFF000000;
            rgbaBuffer[i + j * width + 1] = ((NvU32)(oddLine[i + 1] >> 6)) |          // R
                                             ((NvU32)(evenLine[i + 1] >> 6) << 8) |   // G
                                             ((NvU32)(evenLine[i] >> 6) << 16) |      // B
                                             0xFF000000;
            rgbaBuffer[i + (j + 1) * width] = ((NvU32)(oddLine[i + 1] >> 6)) |         // R
                                               ((NvU32)(oddLine[i] >> 6) << 8) |       // G
                                               ((NvU32)(evenLine[i] >> 6) << 16) |     // B
                                               0xFF000000;
            rgbaBuffer[i + (j + 1) * width + 1] = ((NvU32)(oddLine[i + 1] >> 6)) |      // R
                                                   ((NvU32)(oddLine[i] >> 6) << 8) |    // G
                                                   ((NvU32)(evenLine[i] >> 6) << 16) |  // B
                                                   0xFF000000;
        }
        rawBuffer += surfaceMap.surface[0].pitch * 2;
    }

    if(fwrite(rgbaBuffer, width * height * 4, 1, file) != 1) {
        LOG_ERR("WriteRAWImageToRGBA: file write failed\n");
        goto done;
    }

    status = NVMEDIA_STATUS_OK;

done:
    fclose(file);

    NvMediaImageUnlock(image);

    if(rgbaBuffer)
        free(rgbaBuffer);

    return status;
}

NvMediaStatus
WriteImage(
    char *filename,
    NvMediaImage *image,
    NvMediaBool uvOrderFlag,
    NvMediaBool appendFlag,
    NvU32 bytesPerPixel)
{
    NvMediaImageSurfaceMap surfaceMap;
    unsigned char *pBuff = NULL, *pDstBuff[3] = {NULL}, *pChroma;
    unsigned int dstPitches[3];
    unsigned int imageSize = 0, width, height;
    int indexU = 1, indexV = 2;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    FILE *file = NULL;
    unsigned int xScale = 1;
    unsigned int yScale = 1;

    if(!image || !filename) {
        LOG_ERR("WriteImage: Bad parameter\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("WriteImage: NvMediaImageLock failed\n");
        return NVMEDIA_STATUS_ERROR;
    }

    height = surfaceMap.height;
    width  = surfaceMap.width;

    if(!(file = fopen(filename, appendFlag ? "ab" : "wb"))) {
        LOG_ERR("WriteImage: file open failed: %s\n", filename);
        perror(NULL);
        return NVMEDIA_STATUS_ERROR;
    }

    switch(image->type) {
        case NvMediaSurfaceType_Image_YUV_444:
            dstPitches[0] = width;
            xScale = 1;
            yScale = 1;
            imageSize = width * height * 3;
            break;
        case NvMediaSurfaceType_Image_YUV_422:
        case NvMediaSurfaceType_Image_YUYV_422:
            dstPitches[0] = width;
            xScale = 2;
            yScale = 1;
            imageSize = width * height * 2;
            break;
        case NvMediaSurfaceType_Image_YUV_420:
            dstPitches[0] = width;
            xScale = 2;
            yScale = 2;
            imageSize = width * height * 3 / 2;
            break;
        case NvMediaSurfaceType_Image_RGBA:
            dstPitches[0] = width * 4;
            imageSize = width * height * 4;
            break;
        case NvMediaSurfaceType_Image_RAW:
            dstPitches[0] = width * bytesPerPixel;
            imageSize = width * height * bytesPerPixel;
            break;
        case NvMediaSurfaceType_Image_V16Y16U16X16:
            dstPitches[0] = width * 8;
            imageSize = width * height * 8;
            break;
        case NvMediaSurfaceType_Image_X2U10Y10V10:
            dstPitches[0] = width * 4;
            imageSize = width * height * 4;
            break;
        case NvMediaSurfaceType_Image_Y16:
        case NvMediaSurfaceType_Image_Y10:
            dstPitches[0] = width * 2;
            xScale = 1;
            yScale = 1;
            imageSize = width * height * 2;
            break;
        case NvMediaSurfaceType_Image_Monochrome:
        default:
            LOG_ERR("WriteImage: Invalid video surface type\n");
            goto done;
    }

    imageSize += image->embeddedDataTopSize;
    imageSize += image->embeddedDataBottomSize;

    if(!(pBuff = malloc(imageSize))) {
        LOG_ERR("WriteImage: Out of memory\n");
        fclose(file);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    pDstBuff[0] = pBuff;
    pDstBuff[indexV] = pDstBuff[0] + width * height;
    pDstBuff[indexU] = pDstBuff[indexV] + (width * height) / (xScale * yScale);

    dstPitches[indexU] = width / xScale;
    dstPitches[indexV] = width / xScale;

    status = NvMediaImageGetBits(image, NULL, (void **)pDstBuff, dstPitches);
    NvMediaImageUnlock(image);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("WriteImage: NvMediaVideoSurfaceGetBits() failed\n");
        goto done;
    }

    switch(image->type) {
        case NvMediaSurfaceType_Image_YUV_444:
        case NvMediaSurfaceType_Image_YUV_422:
        case NvMediaSurfaceType_Image_YUYV_422:
        case NvMediaSurfaceType_Image_YUV_420:
            if(fwrite(pDstBuff[0], width * height, 1, file) != 1) {
                LOG_ERR("WriteImage, line %d: file write failed\n", __LINE__);
                goto done;
            }
            pChroma = uvOrderFlag ? pDstBuff[indexU] : pDstBuff[indexV];
            if(fwrite(pChroma, (width * height) / (xScale * yScale), 1, file) != 1) {
                LOG_ERR("WriteImage, line %d: file write failed\n", __LINE__);
                goto done;
            }
            pChroma = uvOrderFlag ? pDstBuff[indexV] : pDstBuff[indexU];
            if(fwrite(pChroma, (width * height) / (xScale * yScale), 1, file) != 1) {
                LOG_ERR("WriteImage, line %d: file write failed\n", __LINE__);
                goto done;
            }
            break;
        case NvMediaSurfaceType_Image_V16Y16U16X16:
        case NvMediaSurfaceType_Image_X2U10Y10V10:
        case NvMediaSurfaceType_Image_Y16:
        case NvMediaSurfaceType_Image_Y10:
        case NvMediaSurfaceType_Image_RGBA:
        case NvMediaSurfaceType_Image_RAW:
            if(fwrite(pDstBuff[0], imageSize, 1, file) != 1) {
                LOG_ERR("WriteImage, line %d: file write failed\n", __LINE__);
                goto done;
            }
            break;
        default:
            LOG_ERR("WriteImage: Invalid image surface type\n");
            goto done;
    }

    status = NVMEDIA_STATUS_OK;

done:
    if(pBuff)
        free(pBuff);

    if(file)
        fclose(file);

    return status;
}

NvMediaStatus
ReadImage(
    char *fileName,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvMediaImage *image,
    NvMediaBool uvOrderFlag,
    NvU32 bytesPerPixel)
{
    NvU32 xScale = 1, yScale = 1, imageSize = 0, i;
    NvU32 uHeightSurface, uWidthSurface, YUVPitch[3];
    NvU8 *pYUVBuff[3], *pBuff = NULL, *pYBuff, *pUBuff = NULL, *pVBuff = NULL, *pChroma;
    FILE *file = NULL;
    NvMediaImageSurfaceMap surfaceMap;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    MemSurf *memSurface = NULL;

    if(NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("ReadImage: NvMediaImageLock failed\n");
        goto done;
    }

    uHeightSurface = surfaceMap.height;
    uWidthSurface  = surfaceMap.width;

    if(!image || width > uWidthSurface || height > uHeightSurface) {
        LOG_ERR("ReadImage: Bad parameters\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pBuff = malloc(uHeightSurface * uWidthSurface * 4);
    if(!pBuff) {
        LOG_ERR("ReadImage: Failed to allocate image buffer\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    switch(image->type) {
        case NvMediaSurfaceType_Image_YUV_444:
            YUVPitch[0] = uWidthSurface;
            imageSize = width * height * 3;
            memset(pBuff, 0x10, uWidthSurface * uHeightSurface);
            memset(pBuff + uWidthSurface * uHeightSurface, 0x80, (uHeightSurface * uWidthSurface) * 2);
            //YVU order in the buffer
            xScale = 1;
            yScale = 1;
            break;
        case NvMediaSurfaceType_Image_YUV_422:
        case NvMediaSurfaceType_Image_YUYV_422:
            YUVPitch[0] = uWidthSurface;
            imageSize = 2 * (width * height);
            memset(pBuff, 0x10, uWidthSurface * uHeightSurface);
            memset(pBuff + uWidthSurface * uHeightSurface, 0x80, (uHeightSurface * uWidthSurface));
            xScale = 2;
            yScale = 1;
            break;
        case NvMediaSurfaceType_Image_YUV_420:
            YUVPitch[0] = uWidthSurface;
            imageSize = (width * height * 3) / 2;
            memset(pBuff, 0x10, uWidthSurface * uHeightSurface);
            memset(pBuff + uWidthSurface * uHeightSurface, 0x80, (uHeightSurface * uWidthSurface) / 2);
            xScale = 2;
            yScale = 2;
            break;
        case NvMediaSurfaceType_Image_Y16:
        case NvMediaSurfaceType_Image_Y10:
            width *= 2;
            uWidthSurface *= 2;
            YUVPitch[0] = uWidthSurface;
            imageSize = width * height;
            memset(pBuff, 0x10, uWidthSurface * uHeightSurface);
            break;
        case NvMediaSurfaceType_Image_RGBA:
            imageSize = uWidthSurface * uHeightSurface * 4;
            memset(pBuff, 0x10, imageSize);
            YUVPitch[0] = width * 4;
            break;
        case NvMediaSurfaceType_Image_RAW:
            imageSize = uWidthSurface * uHeightSurface * bytesPerPixel;
            memset(pBuff, 0xFF, imageSize);
            YUVPitch[0] = uWidthSurface * bytesPerPixel;
            break;
        case NvMediaSurfaceType_Image_Monochrome:
        default:
            LOG_ERR("ReadImage: Invalid image surface type\n");
            goto done;
    }

    if(!(file = fopen(fileName, "rb"))) {
        LOG_ERR("ReadImage: Error opening file: %s\n", fileName);
        goto done;
    }

    if(frameNum > 0) {
        if(fseeko(file, frameNum * (off_t)imageSize, SEEK_SET)) {
            LOG_ERR("ReadImage: Error seeking file: %s\n", fileName);
            goto done;
        }
    }

    YUVPitch[1] = uWidthSurface / xScale;
    YUVPitch[2] = uWidthSurface / xScale;

    pYBuff = pBuff;
    pVBuff = pYBuff + uWidthSurface * uHeightSurface;
    pUBuff = pVBuff + (uWidthSurface * uHeightSurface) / (xScale * yScale);

    pYUVBuff[0] = pYBuff;
    pYUVBuff[1] = pUBuff;
    pYUVBuff[2] = pVBuff;

    switch(image->type) {
        case NvMediaSurfaceType_Image_YUV_444:
        case NvMediaSurfaceType_Image_YUV_422:
        case NvMediaSurfaceType_Image_YUYV_422:
        case NvMediaSurfaceType_Image_YUV_420:
            //read Y U V separately
            for(i = 0; i < height; i++) {
                if(fread(pYBuff, width, 1, file) != 1) {
                    LOG_ERR("ReadImage: Error reading file: %s\n", fileName);
                    goto done;
                }
                pYBuff += uWidthSurface;
            }

            pChroma = uvOrderFlag ? pUBuff : pVBuff;
            for(i = 0; i < height / yScale; i++) {
                if(fread(pChroma, width / xScale, 1, file) != 1) {
                    LOG_ERR("ReadImage: Error reading file: %s\n", fileName);
                    goto done;
                }
                pChroma += uWidthSurface / xScale;
            }

            pChroma = uvOrderFlag ? pVBuff : pUBuff;
            for(i = 0; i < height / yScale; i++) {
                if(fread(pChroma, width / xScale, 1, file) != 1) {
                    LOG_ERR("ReadImage: Error reading file: %s\n", fileName);
                    goto done;
                }
                pChroma += uWidthSurface / xScale;
            }
            break;
        case NvMediaSurfaceType_Image_Y16:
        case NvMediaSurfaceType_Image_Y10:
            //read Y data only
            for(i = 0; i < height; i++) {
                if(fread(pYBuff, width, 1, file) != 1) {
                    LOG_ERR("ReadImage: Error reading file: %s\n", fileName);
                    goto done;
                }
                pYBuff += uWidthSurface;
            }
            break;
        case NvMediaSurfaceType_Image_RGBA:
            if(strstr(fileName, ".png")) {
                LOG_ERR("ReadImage: Use ReadPNGImage() function from common/nvmediatest_png.c\n");
                goto done;
            } else if(fread(pBuff, imageSize, 1, file) != 1) {
                LOG_ERR("ReadImage: Error reading file: %s\n", fileName);
                goto done;
            }
            break;
        case NvMediaSurfaceType_Image_RAW:
            if(fread(pBuff, imageSize, 1, file) != 1) {
                LOG_ERR("ReadImage: Error reading file: %s\n", fileName);
                goto done;
            }
            break;
        case NvMediaSurfaceType_Image_Monochrome:
        default:
            LOG_ERR("ReadImage: Invalid image surface type\n");
            goto done;
    }

    if(IsSucceed(NvMediaImagePutBits(image, NULL, (void **)pYUVBuff, YUVPitch))) {
        status = NVMEDIA_STATUS_OK;
    } else {
        LOG_ERR("%s: Failed to put bits\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
    }


done:
    if(pBuff)
        free(pBuff);

    if(file)
        fclose(file);

    if(memSurface) {
        DestroyMemSurf(memSurface);
        memSurface = NULL;
    }

    NvMediaImageUnlock(image);

    return status;
}

NvMediaStatus
InitImage(
    NvMediaImage *image,
    NvU32 bytesPerPixel)
{
    NvU32 xScale = 1, yScale = 1;
    NvU32 uHeightSurface, uWidthSurface, YUVPitch[3];
    NvU8 *pYUVBuff[3], *pBuff = NULL, *pYBuff, *pUBuff = NULL, *pVBuff = NULL;
    NvMediaImageSurfaceMap surfaceMap;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;

    if(NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("InitImage: NvMediaImageLock failed\n");
        goto done;
    }

    uHeightSurface = surfaceMap.height;
    uWidthSurface  = surfaceMap.width;

    pBuff = calloc(1, uHeightSurface * uWidthSurface * 4);
    if(!pBuff) {
        LOG_ERR("InitImage: Failed to allocate image buffer\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    switch(image->type) {
        case NvMediaSurfaceType_Image_YUV_444:
            YUVPitch[0] = uWidthSurface;
            //YVU order in the buffer
            xScale = 1;
            yScale = 1;
            break;
        case NvMediaSurfaceType_Image_YUV_422:
        case NvMediaSurfaceType_Image_YUYV_422:
            YUVPitch[0] = uWidthSurface;
            xScale = 2;
            yScale = 1;
            break;
        case NvMediaSurfaceType_Image_YUV_420:
            YUVPitch[0] = uWidthSurface;
            xScale = 2;
            yScale = 2;
            break;
        case NvMediaSurfaceType_Image_RGBA:
            YUVPitch[0] = uWidthSurface * 4;
            break;
        case NvMediaSurfaceType_Image_RAW:
            YUVPitch[0] = uWidthSurface * bytesPerPixel;
            break;
        case NvMediaSurfaceType_Image_Monochrome:
        case NvMediaSurfaceType_Image_V16Y16U16X16:
        case NvMediaSurfaceType_Image_X2U10Y10V10:
        default:
            goto done;
    }

    YUVPitch[1] = uWidthSurface / xScale;
    YUVPitch[2] = uWidthSurface / xScale;

    pYBuff = pBuff;
    pVBuff = pYBuff + uWidthSurface * uHeightSurface;
    pUBuff = pVBuff + (uWidthSurface * uHeightSurface) / (xScale * yScale);

    pYUVBuff[0] = pYBuff;
    pYUVBuff[1] = pUBuff;
    pYUVBuff[2] = pVBuff;

    if(IsSucceed(NvMediaImagePutBits(image, NULL, (void **)pYUVBuff, YUVPitch))) {
        status = NVMEDIA_STATUS_OK;
    } else {
        LOG_ERR("%s: Failed to put bits\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
    }

done:
    if(pBuff)
        free(pBuff);

    NvMediaImageUnlock(image);

    return status;
}

NvMediaStatus
ReadPPMImage(
    char *fileName,
    NvMediaImage *image)
{
    NvU32 uSurfaceWidth, uSurfaceHeight;
    NvU32 x, y;
    char buf[256], *c;
    NvU8 *pBuff = NULL, *pRGBBuff;
    NvU32 uFrameSize;
    FILE *file = NULL;
    NvMediaImageSurfaceMap surfaceMap;
    NvMediaStatus ret = NVMEDIA_STATUS_OK;

    if(NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: NvMediaImageLock failed\n", __func__);
        goto done;
    }

    if(!image) {
        LOG_ERR("%s: Failed allocating memory\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    file = fopen(fileName, "rb");
    if(!file) {
        LOG_ERR("%s: Error opening file: %s\n", __func__, fileName);
        return NVMEDIA_STATUS_ERROR;
    }

    c = fgets(buf, 256, file);
    if(!c || strncmp(buf, "P6\n", 3)) {
        LOG_ERR("%s: Invalid PPM header in file: %s\n", __func__, fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }
    do {
        c = fgets(buf, 255, file);
        if(!c) {
            LOG_ERR("%s: Invalid PPM header in file: %s\n", __func__, fileName);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
    } while(!strncmp(buf, "#", 1));
    if(sscanf(buf, "%u %u", &uSurfaceWidth, &uSurfaceHeight) != 2) {
        LOG_ERR("%s: Error getting PPM file resolution in file: %s\n", __func__, fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }
    if(uSurfaceWidth < 16 || uSurfaceWidth > 2048 ||
            uSurfaceHeight < 16 || uSurfaceHeight > 2048) {
        LOG_ERR("%s: Invalid PPM file resolution: %ux%u for file: %s\n", __func__, uSurfaceWidth, uSurfaceHeight, fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    uFrameSize = uSurfaceWidth * uSurfaceHeight * 3;
    pBuff = malloc(uFrameSize);
    if(!pBuff) {
        LOG_ERR("%s: Out of memory\n", __func__);
        ret = NVMEDIA_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    pRGBBuff = pBuff;

    if(fread(pRGBBuff, uFrameSize, 1, file) != 1) {
        LOG_ERR("%s: Error reading file: %s\n", __func__, fileName);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    for(y = 0; y < uSurfaceHeight; y++) {
        NvU8 *pPixel = (NvU8 *)surfaceMap.surface[0].mapping + surfaceMap.surface[0].pitch * y;
        for(x = 0; x < uSurfaceWidth; x++) {
            *pPixel++ = *pRGBBuff++; // R
            *pPixel++ = *pRGBBuff++; // G
            *pPixel++ = *pRGBBuff++; // B
            *pPixel++ = 255;         // Alpha
        }
    }

done:
    NvMediaImageUnlock(image);
    if(pBuff) free(pBuff);
    if(file) fclose(file);

    return ret;
}
NvMediaStatus
ReadYUVBuffer(
    FILE *file,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvU8 *pBuff,
    NvMediaBool bOrderUV)
{
    NvU8 *pYBuff, *pUBuff, *pVBuff, *pChroma;
    NvU32 frameSize = (width * height *3)/2;
    NvMediaStatus ret = NVMEDIA_STATUS_OK;
    unsigned int i;

    if(!pBuff || !file)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    pYBuff = pBuff;

    //YVU order in the buffer
    pVBuff = pYBuff + width * height;
    pUBuff = pVBuff + width * height / 4;

    if(fseek(file, frameNum * frameSize, SEEK_SET)) {
        LOG_ERR("ReadYUVFrame: Error seeking file: %p\n", file);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }
    //read Y U V separately
    for(i = 0; i < height; i++) {
        if(fread(pYBuff, width, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %p\n", file);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pYBuff += width;
    }

    pChroma = bOrderUV ? pUBuff : pVBuff;
    for(i = 0; i < height / 2; i++) {
        if(fread(pChroma, width / 2, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %p\n", file);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pChroma += width / 2;
    }

    pChroma = bOrderUV ? pVBuff : pUBuff;
    for(i = 0; i < height / 2; i++) {
        if(fread(pChroma, width / 2, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %p\n", file);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pChroma += width / 2;
    }

done:
    return ret;
}

NvMediaStatus
ReadRGBABuffer(
    FILE *file,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvU8 *pBuff)
{
    NvU32 frameSize = width * height * 4;
    NvMediaStatus ret = NVMEDIA_STATUS_OK;

    if(!pBuff || !file)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(fseek(file, frameNum * frameSize, SEEK_SET)) {
        LOG_ERR("ReadYUVFrame: Error seeking file: %p\n", file);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    //read rgba data
    if(fread(pBuff, frameSize, 1, file) != 1) {
        if (feof(file))
            LOG_DBG("ReadRGBAFrame: file read to the end\n");
        else
            LOG_ERR("ReadRGBAFrame: Error reading file: %p\n", file);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

done:
    return ret;
}


NvMediaStatus
GetFrameCrc(
    NvU8   *pBuff,
    NvU32  width,
    NvU32  height,
    NvU32  pitch,
    NvMediaSurfaceType imageType,
    NvU32  *crcOut,
    NvU32  rawBytesPerPixel)
{
    NvU32 lines, crc = 0;
    NvU8 *pDstBuff[3] = {NULL};
    NvU32 dstPitches[3];
    NvU32 dstWidths[3];
    NvU8 *pY, *pV, *pU;
    NvU32 *pitchY, *pitchV, *pitchU;
    NvU32 *widthY, *widthV, *widthU;
    NvU32 bytePerPixel;

    if(!pBuff || !crcOut) {
        LOG_ERR("GetFrameCrc: Bad parameter\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pDstBuff[0] = pBuff;
    pDstBuff[1] = NULL;
    pDstBuff[2] = NULL;
    dstPitches[0] = 0;
    dstPitches[1] = 0;
    dstPitches[2] = 0;
    dstWidths[0] = 0;
    dstWidths[1] = 0;
    dstWidths[2] = 0;

    pY = pDstBuff[0];
    pV = pDstBuff[0];
    pU = pDstBuff[0];
    pitchY = &dstPitches[0];
    pitchV = &dstPitches[1];
    pitchU = &dstPitches[2];
    widthY = &dstWidths[0];
    widthV = &dstWidths[1];
    widthU = &dstWidths[2];
    switch(imageType){
        case NvMediaSurfaceType_Image_YUV_420:
            bytePerPixel = 1;
            if(!pitch)
                pitch  = width;
            pV = pY + pitch * height * bytePerPixel;
            pU = pV + pitch * height * bytePerPixel / 4;
            *pitchY = pitch * bytePerPixel;
            *widthY = width * bytePerPixel;
            *pitchV = *pitchU = pitch * bytePerPixel / 2;
            *widthV = *widthU = width * bytePerPixel / 2;
            lines = height;
            while(lines--) {
                crc = CalculateBufferCRC(*widthY, crc, pY);
                pY += *pitchY;
            }
            lines = height / 2;
            while(lines--) {
                crc = CalculateBufferCRC(*widthV, crc, pV);
                pV += *pitchV;
            }
            lines = height / 2;
            while(lines--) {
                crc = CalculateBufferCRC(*widthU, crc, pU);
                pU += *pitchU;
            }
            break;
        case NvMediaSurfaceType_Image_RGBA:
            bytePerPixel = 4;
            if(!pitch)
                pitch  = width * bytePerPixel;
            dstPitches[0] = pitch;
            dstWidths[0] = width * bytePerPixel;
            lines = height;
            while(lines--) {
                crc = CalculateBufferCRC(dstWidths[0], crc, pDstBuff[0]);
                pDstBuff[0] += dstPitches[0];
            }
            break;
        case NvMediaSurfaceType_Image_RAW:
             bytePerPixel = rawBytesPerPixel;
             if(!pitch)
                 pitch  = width * bytePerPixel;
             dstPitches[0] = pitch;
             dstWidths[0] = width * bytePerPixel;
             lines = height;
             while(lines--) {
                crc = CalculateBufferCRC(dstWidths[0], crc, pDstBuff[0]);
                pDstBuff[0] += dstPitches[0];
             }
             break;
        default:
             LOG_ERR("GetFrameCrc: Invalid image surface type, not supported\n");
             return NVMEDIA_STATUS_ERROR;

    }

    *crcOut = crc;
    return NVMEDIA_STATUS_OK;
}
