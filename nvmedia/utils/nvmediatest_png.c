/*
 * Copyright (c) 2016, NVIDIA Corporation. All rights reserved.
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
#include "png.h"
#include "nvmediatest_png.h"
#include "log_utils.h"

#ifdef NVMEDIA_ANDROID
#include <pngstruct.h>
#include <pnginfo.h>
#endif

NvMediaStatus
GetPNGDimensions(
    char *filename,
    unsigned int *width,
    unsigned int *height)
{
    int pngDepth, pngColorType;
    unsigned char signature[8];
    png_structp pngStruct;
    png_infop pngInfo;
    FILE *file;

    if(!filename && !width && !height) {
        LOG_ERR("GetPNGDimensions: Invalid parameters");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    file = fopen(filename, "rb");
    if(!file) {
        LOG_ERR("GetPNGDimensions: %s does not exist\n", filename);
        return NVMEDIA_STATUS_ERROR;
    }

    fread(signature, 1, 8, file);

    // Check PNG Singnature
    if(!png_check_sig(signature, 8)) {
        LOG_ERR("GetPNGDimensions: Invalid PNG signature\n");
        return NVMEDIA_STATUS_ERROR;
    }

    pngStruct = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!pngStruct) {
        LOG_ERR("GetPNGDimensions: Cannot creat PNG structure\n");
        return NVMEDIA_STATUS_ERROR;
    }

    pngInfo = png_create_info_struct(pngStruct);
    if(!pngInfo) {
#ifdef NVMEDIA_ANDROID
        png_destroy_read_struct(&pngStruct, pngInfo ? &pngInfo : NULL, (png_infopp) NULL);
#else
        png_destroy_read_struct(&pngStruct, pngInfo ? &pngInfo : NULL, png_infopp_NULL);
#endif
        LOG_ERR("GetPNGDimensions: Cannot creat PNG info structure\n");
        return NVMEDIA_STATUS_ERROR;
    }

    png_init_io(pngStruct, file);

    // Set that we have already read 8 bytes for the signature
    png_set_sig_bytes(pngStruct, 8);

    // Setup Error handling function
    if(setjmp(png_jmpbuf(pngStruct))) {
        // If there is an error clean-up to do
#ifdef NVMEDIA_ANDROID
        png_destroy_read_struct(&pngStruct, pngInfo ? &pngInfo : NULL, (png_infopp) NULL);
#else
        png_destroy_read_struct(&pngStruct, pngInfo ? &pngInfo : NULL, png_infopp_NULL);
#endif
        LOG_ERR("GetPNGDimensions: Error during processing\n");
        return NVMEDIA_STATUS_ERROR;
    }

    // Read all PNG info up to the image data
    png_read_info(pngStruct, pngInfo);

    // Get Header Info
    *width = 0;
    *height = 0;
    if(!png_get_IHDR(pngStruct, pngInfo, (png_uint_32 *)width, (png_uint_32 *)height,
                      &pngDepth, &pngColorType, NULL, NULL, NULL)) {
        LOG_ERR("GetPNGDimensions: Invalid PNG header info\n");
        return NVMEDIA_STATUS_ERROR;
    }

    LOG_DBG("GetPNGDimensions: width: %u height:%u\n", *width, *height);
    fclose(file);

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ReadPNG(
    char *filename,
    NvU8 defaultAplha,
    MemSurf *surface,
    NvU32 *palette)
{
    png_uint_32 pngWidth, pngHeight;
    int pngDepth, pngColorType;
    double pngDisplayExponent = 2.2;
    double pngGamma;
    unsigned char signature[8];
    png_structp pngStruct = NULL;
    png_infop pngInfo = NULL;
    int i;
    png_uint_32 pngStride, pngChannels;
    png_byte *pngImageData = NULL;
    png_byte **ppStridePointers = NULL;
    int copyWidth, copyHeight;
    NvMediaStatus res = NVMEDIA_STATUS_ERROR;
    unsigned int width = surface ? surface->width : 0;
    unsigned int height = surface ? surface->height : 0;

    if(!surface && !palette && !filename) {
        LOG_ERR("ReadPNG: Invalid parameters");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    LOG_DBG("ReadPNG: Start - File: %s Size: %lux%lu", filename, width, height);

    FILE *f = fopen(filename, "rb");
    if(!f) {
        LOG_ERR("ReadPNG: File: %s does not exist", filename);
        goto ReadPNG_end;
    }

    if(!fread(signature, 1, 8, f)) {
        LOG_ERR("ReadPNG: Error reading file: %s", filename);
        goto ReadPNG_end;
    }

    // Check PNG Singnature
    if(!png_check_sig(signature, 8)) {
        LOG_ERR("ReadPNG: Invalid PNG signature");
        goto ReadPNG_end;
    }

    pngStruct = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!pngStruct) {
        LOG_ERR("ReadPNG: Cannot creat PNG structure");
        goto ReadPNG_end;
    }

    pngInfo = png_create_info_struct(pngStruct);
    if(!pngInfo) {
        LOG_ERR("ReadPNG: Cannot creat PNG info structure");
        goto ReadPNG_end;
    }

    png_init_io(pngStruct, f);
    // Set that we have already read 8 bytes for the signature
    png_set_sig_bytes(pngStruct, 8);

    // Setup Error handling function
    if(setjmp(png_jmpbuf(pngStruct))) {
        // If there is an errror do clean-up
        LOG_ERR("ReadPNG: Error during processing");
        goto ReadPNG_end;
    }

    // Read all PNG info up to the image data
    png_read_info(pngStruct, pngInfo);

    if(palette) {
        if(pngInfo->num_palette) {
            NvU8 *p = (NvU8 *)palette;
            // Copy RGB palette values
            for(i = 0; i < pngInfo->num_palette; i++) {
                p[0] = pngInfo->palette[i].red;
                p[1] = pngInfo->palette[i].green;
                p[2] = pngInfo->palette[i].blue;
                p[3] = defaultAplha;
                p += 4;
            }
            p = (NvU8 *)palette;
            // Copy alpha palette values
            for(i = 0; i < pngInfo->num_trans; i++) {
#ifdef NVMEDIA_ANDROID
                p[3] = *(pngInfo->trans_alpha);
#else
                p[3] = pngInfo->trans[i];
#endif
                p += 4;
            }
            res = NVMEDIA_STATUS_OK;
        } else {
            LOG_ERR("ReadPNG: No palette in file: %s", filename);
        }
        goto ReadPNG_end;
    }

    if(!surface) {
        LOG_ERR("ReadPNG: Invalid target surface");
        goto ReadPNG_end;
    }

    // Get Header Info
    if(!png_get_IHDR(pngStruct, pngInfo, &pngWidth, &pngHeight,
        &pngDepth, &pngColorType, NULL, NULL, NULL)) {
        LOG_ERR("ReadPNG: Invalid PNG header info");
        goto ReadPNG_end;
    }

    // Expand Paletted image to RGB
    if(pngColorType == PNG_COLOR_TYPE_PALETTE) {
        if(surface->bpp != 1)
            png_set_expand(pngStruct);
    } else {
        if(surface->bpp == 1) {
            LOG_ERR("ReadPNG: Trying to load non-paletted PNG to an I8 surface: %s", filename);
            goto ReadPNG_end;
        }
    }
    // Expand Gray to 8 bit/pixel if it is less
    if(pngColorType == PNG_COLOR_TYPE_GRAY && pngDepth < 8) {
        png_set_expand(pngStruct);
    }
    // Expand Gray to RGB
    if(pngColorType == PNG_COLOR_TYPE_GRAY ||
       pngColorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
       png_set_gray_to_rgb(pngStruct);
    }
    // Expand Global Transparency to Alpha
    if(png_get_valid(pngStruct, pngInfo, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(pngStruct);
    }
    // Get and Set Gamma
    if(png_get_gAMA(pngStruct, pngInfo, &pngGamma)) {
        png_set_gamma(pngStruct, pngDisplayExponent, pngGamma);
    }
    // Reduce 16-bit color depht to 8-bit
    if(pngDepth == 16) {
        png_set_strip_16(pngStruct);
    }
    if(surface->bpp != 1) {
        if(pngColorType & PNG_COLOR_MASK_ALPHA && surface->bpp == 3) {
            // Remove alpha for RGB surfaces
            png_set_strip_alpha(pngStruct);
        }
        if(!(pngColorType & PNG_COLOR_MASK_ALPHA) && surface->bpp == 4) {
            // Add 0x80 alpha for RGBA surfaces
            png_set_add_alpha(pngStruct, defaultAplha, PNG_FILLER_AFTER);
        }
    }

    // Activate transformation settings
    png_read_update_info(pngStruct, pngInfo);

    pngStride = png_get_rowbytes(pngStruct, pngInfo);
    pngChannels = png_get_channels(pngStruct, pngInfo);

    LOG_DBG("PNG Size: %dx%d pngChannels: %ld pngDepth: %d pngColorType: %d pngStride: %ld",
        pngWidth, pngHeight, pngChannels, pngDepth, pngColorType, pngStride);

    // Decode frame

    // Allocate Buffer for Image Data
    pngImageData = malloc(pngStride * pngHeight);
    if(!pngImageData) {
        LOG_ERR("ReadPNG: Cannot allocate image data");
        goto ReadPNG_end;
    }

    // Allocate Buffer for Stride Pointers
    ppStridePointers = malloc(pngHeight * sizeof(png_bytep));
    if(!ppStridePointers) {
        LOG_ERR("ReadPNG: Cannot allocate image data");
        goto ReadPNG_end;
    }
    // Initialize Stride Pointers
    for(i = 0;  i < (int)pngHeight; i++)
        ppStridePointers[i] = pngImageData + i * pngStride;

    // Decode Picture
    png_read_image(pngStruct, ppStridePointers);

    // Read until End of Data
    png_read_end(pngStruct, NULL);

    // Copy picture
    copyWidth = MIN((int)width, (int)pngWidth);
    copyHeight = MIN((int)height, (int)pngHeight);

    for(i = 0; i < copyHeight; i++) {
        memcpy(surface->pSurf + i * surface->pitch, ppStridePointers[i], copyWidth * surface->bpp);
    }

    res = NVMEDIA_STATUS_OK;

ReadPNG_end:
    // Release resources
    if(pngStruct)
#ifdef NVMEDIA_ANDROID
        png_destroy_read_struct(&pngStruct, pngInfo ? &pngInfo : NULL, (png_infopp) NULL);
#else
        png_destroy_read_struct(&pngStruct, pngInfo ? &pngInfo : NULL, png_infopp_NULL);
#endif

    if(ppStridePointers)
        free(ppStridePointers);

    if(pngImageData)
        free(pngImageData);

    if(f)
        fclose(f);

    LOG_DBG("ReadPNG: End: Result: %d", res);

    return res;
}

NvMediaStatus
ReadPNGFrame(
    char *fileName,
    NvU32 width,
    NvU32 height,
    NvMediaVideoSurface *pFrame)
{
    NvU32 uHeightSurface, uWidthSurface, YUVPitch[3];
    NvU8 *pYUVBuff[3], *pBuff = NULL, *pYBuff;
    FILE *file = NULL;
    NvMediaVideoSurfaceMap surfaceMap;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    MemSurf *memSurface = NULL;

    if(!pFrame && !fileName) {
        LOG_ERR("%s: Bad parameters\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(NvMediaVideoSurfaceLock(pFrame, &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: NvMediaVideoSurfaceLock failed\n", __func__);
        goto done;
    }

    uHeightSurface = (height + 15) & (~15);
    uWidthSurface  = (width + 15) & (~15);

    pBuff = malloc(uHeightSurface * uWidthSurface * 4);
    if(!pBuff) {
        LOG_ERR("%s: Failed to allocate image buffer\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    memset(pBuff, 0x10, uHeightSurface * uWidthSurface * 4);
    YUVPitch[0] = width * 4;

    if(!(file = fopen(fileName, "rb"))) {
        LOG_ERR("%s: Error opening file: %s\n", __func__, fileName);
        goto done;
    }

    if(IsFailed(CreateMemRGBASurf(width,
                                  height,
                                  NVMEDIA_TRUE,
                                  0x40808080,
                                  &memSurface))) {
        LOG_ERR("%s: CreateMemRGBASurf failed\n", __func__);
        goto done;
    }
    if(IsFailed(ReadPNG(fileName,
                        255, //DEFAULT_ALPHA,
                        memSurface,
                        NULL))) {
        LOG_ERR("%s: Failed reading PNG\n", __func__);
        goto done;
    }
    pYBuff = memSurface->pSurf;
    pYUVBuff[0] = pYBuff;

    if(IsSucceed(NvMediaVideoSurfacePutBits(pFrame, NULL, (void **)pYUVBuff, YUVPitch)))
        status = NVMEDIA_STATUS_OK;

done:
    if(pBuff)
        free(pBuff);

    if(file)
        fclose(file);

    if(memSurface) {
        DestroyMemSurf(memSurface);
        memSurface = NULL;
    }

    NvMediaVideoSurfaceUnlock(pFrame);

    return status;
}

NvMediaStatus
WritePNG(
    char *filename,
    NvU32 outputBpp,
    NvU8 defaultAplha,
    MemSurf *surface)
{
    png_structp pngStruct = NULL;
    png_infop pngInfo = NULL;
    png_uint_32 pngStride, pngChannels;
    png_byte *pngImageData = NULL;
    png_byte **ppStridePointers = NULL;
    NvMediaStatus res = NVMEDIA_STATUS_ERROR;
    png_uint_32 i;
    png_uint_32 width = surface ? surface->width : 0;
    png_uint_32 height = surface ? surface->height : 0;

    if(!filename && !surface) {
        LOG_ERR("WritePNG: Invalid parameters");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    LOG_DBG("WritePNG: Start - File: %s Size: %lux%lu", filename, width, height);

    FILE *f = fopen(filename, "wb");
    if(!f) {
        LOG_ERR("WritePNG: Unable to create file: %s", filename);
        goto WritePNG_end;
    }

    pngStruct = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if(!pngStruct) {
        LOG_ERR("WritePNG: Cannot creat PNG structure");
        goto WritePNG_end;
    }

    pngInfo = png_create_info_struct(pngStruct);
    if(!pngInfo) {
        LOG_ERR("WritePNG: Cannot creat PNG info structure");
        goto WritePNG_end;
    }

    // Setup Error handling function
    if(setjmp(png_jmpbuf(pngStruct))) {
        LOG_ERR("WritePNG: Error encoding image");
        goto WritePNG_end;
    }

    pngChannels = outputBpp;

    LOG_DBG("WritePNG: Set attributes");

    // Set image attributes
    png_set_IHDR(pngStruct,
                 pngInfo,
                 width,
                 height,
                 8,
                 pngChannels == 4 ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    pngStride = width * pngChannels;

    // Allocate Buffer for Image Data
    pngImageData = malloc(pngStride * height);
    if(!pngImageData) {
        LOG_ERR("WritePNG: Error allocating image data");
        goto WritePNG_end;
    }
    memset(pngImageData, defaultAplha, pngStride * height);

    // Allocate Buffer for Stride Pointers
    ppStridePointers = malloc(height * sizeof(png_bytep));
    if(!ppStridePointers) {
        LOG_ERR("WritePNG: Error allocating stride data");
        goto WritePNG_end;
    }
    // Initialize Stride Pointers
    for(i = 0;  i < height; i++)
        ppStridePointers[i] = pngImageData + i * pngStride;

    LOG_DBG("WritePNG: Copy picture pngStride: %lu pngChannels: %lu", pngStride, pngChannels);

    // Copy picture
    for(i = 0; i < height; i++) {
        png_byte *src = surface->pSurf + i * surface->pitch;
        png_byte *dst = ppStridePointers[i];
        png_uint_32 j, k;

        for(j = 0;  j < width; j++) {
            for(k = 0; k < pngChannels; k++) {
                dst[k] = src[k];
            }
            src += surface->bpp;
            dst += pngChannels;
        }
    }

    LOG_DBG("WritePNG: Write PNG file");

    // Write PNG file
    png_init_io(pngStruct, f);
    png_set_rows(pngStruct, pngInfo, ppStridePointers);
    png_write_png(pngStruct, pngInfo, PNG_TRANSFORM_IDENTITY, NULL);

    // Set OK status
    res = NVMEDIA_STATUS_OK;

WritePNG_end:
    if(pngStruct)
        png_destroy_write_struct(&pngStruct, pngInfo ? &pngInfo : NULL);

    if(f)
        fclose(f);

    // Cleanup memory
    if(pngImageData)
        free(pngImageData);

    if(ppStridePointers) {
        free(ppStridePointers);
    }

    LOG_DBG("WritePNG: End: Result: %d", res);

    return res;
}

NvMediaStatus
ReadPNGImage(
    char *fileName,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvMediaImage *image)
{
    NvU32 xScale = 1, yScale = 1, imageSize = 0;
    NvU32 uHeightSurface, uWidthSurface, YUVPitch[3];
    NvU8 *pYUVBuff[3], *pBuff = NULL, *pYBuff, *pUBuff = NULL, *pVBuff = NULL;
    FILE *file = NULL;
    NvMediaImageSurfaceMap surfaceMap;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    MemSurf *memSurface = NULL;

    if(!image && !fileName) {
        LOG_ERR("ReadPNGImage: Invalid parameters\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK) {
        LOG_ERR("ReadPNGImage: NvMediaImageLock failed\n");
        goto done;
    }

    uHeightSurface = surfaceMap.height;
    uWidthSurface  = surfaceMap.width;
    if(width > uWidthSurface || height > uHeightSurface) {
        LOG_ERR("ReadPNGImage: Bad parameters - width/height\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    pBuff = malloc(uHeightSurface * uWidthSurface * 4);
    if(!pBuff) {
        LOG_ERR("ReadPNGImage: Failed to allocate image buffer\n");
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    imageSize = uWidthSurface * uHeightSurface * 4;
    memset(pBuff, 0x10, imageSize);
    YUVPitch[0] = width * 4;

    if(!(file = fopen(fileName, "rb"))) {
        LOG_ERR("ReadPNGImage: Error opening file: %s\n", fileName);
        goto done;
    }

    if(frameNum > 0) {
        if(fseeko(file, frameNum * (off_t)imageSize, SEEK_SET)) {
            LOG_ERR("ReadPNGImage: Error seeking file: %s\n", fileName);
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

    if(IsFailed(CreateMemRGBASurf(width,
                                  height,
                                  NVMEDIA_TRUE,
                                  0x40808080,
                                  &memSurface))) {
        LOG_ERR("ReadPNGImage: CreateMemRGBASurf failed\n");
        goto done;
    }

    if(IsFailed(ReadPNG(fileName,
                        DEFAULT_ALPHA,
                        memSurface,
                        NULL))) {
        LOG_ERR("ReadPNGImage: Failed reading PNG\n");
        goto done;
    }
    pYBuff = memSurface->pSurf;
    pYUVBuff[0] = pYBuff;

    if(IsSucceed(NvMediaImagePutBits(image, NULL, (void **)pYUVBuff, YUVPitch)))
        status = NVMEDIA_STATUS_OK;

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
