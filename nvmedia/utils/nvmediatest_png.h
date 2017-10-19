/*
 * Copyright (c) 2016, NVIDIA Corporation. All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef _NVMEDIA_TEST_NVMEDIATEST_PNG_H_
#define _NVMEDIA_TEST_NVMEDIATEST_PNG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "misc_utils.h"
#include "nvcommon.h"
#include "nvmedia.h"
#include "surf_utils.h"

//
//    GetPNGDimensions()  Get PNG file dimensions
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   width
//      (out) Surface width
//
//   height
//      (out) Surface height

NvMediaStatus
GetPNGDimensions(
    char *filename,
    unsigned int *width,
    unsigned int *height);

//  ReadPNGFrame
//
//    ReadPNGFrame()  read frame from PNG file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   width
//      (out) Surface width
//
//   height
//      (out) Surface height
//   pFrame
//      (out) Pointer to pre-allocated output surface

NvMediaStatus
ReadPNGFrame(
    char *fileName,
    NvU32 width,
    NvU32 height,
    NvMediaVideoSurface *pFrame);

//  ReadPNG
//
//    ReadPNG()  Read surface from PNG file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   defaultAplha
//      (in) Default alpha in case of an RGB file
//
//   width
//      (in) Surface width
//
//   height
//      (in) Surface height
//
//   surface
//      (out) Pointer to pre-allocated MemSurf structure
//
//   palette
//      (out) If not NULL palette info is copied to this buffer

NvMediaStatus
ReadPNG(
    char *filename,
    NvU8 defaultAplha,
    MemSurf *surface,
    NvU32 *palette);

//  WritePNG
//
//    WritePNG()  Write surface to a PNG file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   outputBpp
//      (in) Output bytes per pixel
//
//   defaultAplha
//      (in) Default alpha in case of an RGB file
//
//   surface
//      (in) Pointer to pre-allocated MemSurf structure

NvMediaStatus
WritePNG(
    char *filename,
    NvU32 outputBpp,
    NvU8 defaultAplha,
    MemSurf *surface);

//  ReadPNGImage
//
//    ReadPNGImage()  Read image from YUV file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   frameNum
//      (in) Frame number to read. Use for stream input files.
//
//   width
//      (in) Surface width
//
//   height
//      (in) Surface height
//
//   image
//      (out) Pointer to pre-allocated output surface

NvMediaStatus
ReadPNGImage(
    char *fileName,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvMediaImage *image);

#endif /*_NVMEDIA_TEST_NVMEDIATEST_PNG_H_*/
