/*
 * Copyright (c) 2013-2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef _NVMEDIA_TEST_SURF_UTILS_H_
#define _NVMEDIA_TEST_SURF_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "misc_utils.h"
#include "nvcommon.h"
#include "nvmedia.h"

#define PACK_RGBA(R, G, B, A)  (((NvU32)(A) << 24) | ((NvU32)(B) << 16) | \
                                ((NvU32)(G) << 8) | (NvU32)(R))
#define DEFAULT_ALPHA   0x80

typedef struct {
    unsigned char  *pSurf;
    unsigned int    width;
    unsigned int    height;
    unsigned int    pitch;
    unsigned int    bpp;
} MemSurf;

typedef struct {
    int                     refCount;
    int                     width;
    int                     height;
    int                     frameNum;
    int                     index;
    NvMediaVideoSurface    *videoSurface;
    NvBool                  progressiveFrameFlag;
    NvBool                  topFieldFirstFlag;
} FrameBuffer;

//  WriteFrame
//
//    WriteFrame()  Save RGB or YUV video surface to a file
//
//  Arguments:
//
//   filename
//      (in) Output file name
//
//   videoSurface
//      (out) Pointer to a surface
//
//   bOrderUV
//      (in) Flag for UV order. If true - UV; If false - VU;
//           Used only in YUV type surface case
//
//   bAppend
//      (in) Apped to exisitng file if true otherwise create new file

NvMediaStatus
WriteFrame(
    char *filename,
    NvMediaVideoSurface *videoSurface,
    NvMediaBool bOrderUV,
    NvMediaBool bAppend);


//  ReadFrame
//
//    ReadFrame()  Read specific frame from YUV or RGBA file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   uFrameNum
//      (in) Frame number to read
//
//   width
//      (in) Surface width
//
//   height
//      (in) Surface height
//
//   pFrame
//      (out) Pointer to pre-allocated output surface
//
//   bOrderUV
//      (in) Flag for UV order. If true - UV; If false - VU;

NvMediaStatus
ReadFrame(
    char *fileName,
    NvU32 uFrameNum,
    NvU32 uWidth,
    NvU32 uHeight,
    NvMediaVideoSurface *pFrame,
    NvMediaBool bOrderUV);

//  ReadRGBAFrame
//
//    ReadRGBAFrame()  Read specific frame from RGBA file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   uFrameNum
//      (in) Frame number to read
//
//   width
//      (in) Surface width
//
//   height
//      (in) Surface height
//
//   pFrame
//      (out) Pointer to pre-allocated output surface
//

NvMediaStatus
ReadRGBAFrame(
    char *fileName,
    NvU32 uFrameNum,
    NvU32 uWidth,
    NvU32 uHeight,
    NvMediaVideoSurface *pFrame);

//  ReadYUVFrame
//
//    ReadYUVFrame()  Read specific frame from YUV file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   uFrameNum
//      (in) Frame number to read
//
//   width
//      (in) Surface width
//
//   height
//      (in) Surface height
//
//   pFrame
//      (out) Pointer to pre-allocated output surface
//
//   bOrderUV
//      (in) Flag for UV order. If true - UV; If false - VU;
NvMediaStatus
ReadYUVFrame(
    char *fileName,
    NvU32 uFrameNum,
    NvU32 uWidth,
    NvU32 uHeight,
    NvMediaVideoSurface *pFrame,
    NvMediaBool bOrderUV);

//  ReadRGBAFile
//
//    ReadRGBAFile()  Read surface from RGBA file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   width
//      (in) Surface width
//
//   height
//      (in) Surface height
//
//   rgbaSurface
//      (out) Pointer to pre-allocated MemSurf structure

NvMediaStatus
ReadRGBA(
    char *filename,
    unsigned int width,
    unsigned int height,
    MemSurf *rgbaSurface);

//  WriteRGBA
//
//    WriteRGBA()  Write surface to an RGBA (binary) file
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
//   rgbaSurface
//      (in) Pointer to pre-allocated MemSurf structure

NvMediaStatus
WriteRGBA(
    char *filename,
    NvU32 outputBpp,
    NvU8 defaultAplha,
    MemSurf *rgbaSurface);

//  GetPPMFileDimensions
//
//    GetPPMFileDimensions() Gets surface dimenssions from a PPM file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   uWidth
//      (out) Pointer to surface width
//
//   uHeight
//      (out) Pointer to surface height

NvMediaStatus
GetPPMFileDimensions(
    char *fileName,
    NvU16 *uWidth,
    NvU16 *uHeight);

//  ReadPPMFrame
//
//    ReadPPMFrame()  Read surface from PPM file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   pFrame
//      (out) Pointer to pre-allocated surface

NvMediaStatus
ReadPPMFrame(
    char *fileName,
    NvMediaVideoSurface *pFrame);

//  ReadPPM
//
//    ReadPPM()  Read PPM file to a surface
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   rgbaSurface
//      (out) Pointer to pre-allocated surface

NvMediaStatus
ReadPPM(
    char *fileName,
    NvU8 defaultAplha,
    MemSurf *rgbaSurface);

//  WritePPM
//
//    WritePPM()  Write a surface to PPM file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   rgbaSurface
//      (in) Pointer to pre-allocated surface

NvMediaStatus
WritePPM(
    char *fileName,
    MemSurf *rgbaSurface);

//  ReadPAL
//
//    ReadPAL()  Read binary palette from file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   palette
//      (out) Pointer to pre-allocated palette

NvMediaStatus
ReadPAL(
    char *filename,
    NvU32 *palette);

//  ReadI8
//
//    ReadI8()  Read I8 (indexed) file to a surface
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   surface
//      (out) Pointer to pre-allocated surface

NvMediaStatus
ReadI8(
    char *filename,
    MemSurf *surface);

//  CreateMemRGBASurf
//
//    CreateMemRGBASurf()  Creates RGBA surface and initializes the values (optional)
//
//  Arguments:
//
//   width
//      (in) Surface width
//
//   height
//      (in) Surface height
//
//   initflag
//      (in) Flag for surface initialization
//
//   initvalue
//      (in) Init value. Surface will be initialized with this value if initflag is true
//
//   surf_out
//      (out) Pointer to pointer to created surface

NvMediaStatus
CreateMemRGBASurf(
    unsigned int width,
    unsigned int height,
    NvMediaBool initflag,
    unsigned int initvalue,
    MemSurf **surf_out);

//  DestroyMemSurf
//
//    DestroyMemSurf()  Releasing surface memory
//
//  Arguments:
//
//   surf
//      (in) Pointer to released surface

NvMediaStatus
DestroyMemSurf(
    MemSurf *surf);

//  DrawRGBARect
//
//    DrawRGBARect()  Creates RGBA rectangle with chosen color
//
//  Arguments:
//
//   surf
//      (out) Pointer to pre-allocated output surface
//
//   rect
//      (in) Pointer to requested rectangle structure
//
//   R
//      (in) R value
//
//   G
//      (in) G value
//
//   B
//      (out) B value
//
//   A
//      (out) A value

NvMediaStatus
DrawRGBARect(
    MemSurf *surf,
    NvMediaRect *rect,
    NvU8 R,
    NvU8 G,
    NvU8 B,
    NvU8 A);

//  PreMultiplyRGBASurf
//
//    PreMultiplyRGBASurf()  Multiplies RGBA surface
//
//  Arguments:
//
//   surf
//      (in/out) Pointer to pre-allocated surface

NvMediaStatus
PreMultiplyRGBASurf(
    MemSurf *surf);

//  CreateMemI8Surf
//
//    CreateMemI8Surf()  Creates and initializes I8 surface
//
//  Arguments:
//
//   width
//      (in) Surface width
//
//   height
//      (in) Surface height
//
//   init
//      (in) Init value for surface initialization
//
//   surf_out
//      (out) Pointer to output surface

NvMediaStatus
CreateMemI8Surf(
    NvU32 width,
    NvU32 height,
    NvU8 init,
    MemSurf **surf_out);

//  DrawI8Rect
//
//    DrawI8Rect()  Creates and initializing I8 rectangle
//
//  Arguments:
//
//   surf
//      (out) Pointer to pre-allocated output surface
//
//   rect
//      (in) Pointer to requested rectangle structure
//
//   index
//      (in) Initialization  value

NvMediaStatus
DrawI8Rect(
    MemSurf *surf,
    NvMediaRect *rect,
    NvU8 index);

NvMediaStatus
GetSurfaceCrc(
    NvMediaVideoSurface *surf,
    NvU32 width,
    NvU32 height,
    NvMediaBool monochromeFlag,
    NvU32 *crcOut);

NvMediaStatus
CheckSurfaceCrc(
    NvMediaVideoSurface *surf,
    NvU32 width,
    NvU32 height,
    NvMediaBool monochromeFlag,
    NvU32 ref,
    NvMediaBool *isMatching);

NvMediaStatus
GetImageCrc(
    NvMediaImage *image,
    NvU32 width,
    NvU32 height,
    NvU32 *crcOut,
    NvU32 rawBytesPerPixel);

NvMediaStatus
CheckImageCrc(
    NvMediaImage *image,
    NvU32 width,
    NvU32 height,
    NvU32 ref,
    NvMediaBool *isMatching,
    NvU32 rawBytesPerPixel);

//  WriteImage
//
//    WriteImage()  Save RGB or YUV image to a file
//
//  Arguments:
//
//   filename
//      (in) Output file name
//
//   image
//      (out) Pointer to the image
//
//   uvOrderFlag
//      (in) Flag for UV order. If true - UV; If false - VU;
//           Used only in YUV type surface case
//
//   appendFlag
//      (in) Apped to exisitng file if true otherwise create new file
//
//   bytesPerPixel
//      (in) Bytes per pixel. Nedded for RAW image types handling.
//         RAW8 - 1 byte per pixel
//         RAW10, RAW12, RAW14 - 2 bytes per pixel

NvMediaStatus
WriteImage(
    char *filename,
    NvMediaImage *image,
    NvMediaBool uvOrderFlag,
    NvMediaBool appendFlag,
    NvU32 bytesPerPixel);

//  ReadImage
//
//    ReadImage()  Read image from file
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
//
//   uvOrderFlag
//      (in) Flag for UV order. If true - UV; If false - VU;
//
//   bytesPerPixel
//      (in) Bytes per pixel. Nedded for RAW image types handling.
//         RAW8 - 1 byte per pixel
//         RAW10, RAW12, RAW14 - 2 bytes per pixel

NvMediaStatus
ReadImage(
    char *fileName,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvMediaImage *image,
    NvMediaBool uvOrderFlag,
    NvU32 bytesPerPixel);

//  WriteRAWImageToRGBA
//
//    WriteRAWImageToRGBA()  Converts RAW image to RGBA and saves to file
//
//  Arguments:
//
//   filename
//      (in) Output file name
//
//   image
//      (in) Pointer to RAW image
//
//   appendFlag
//      (in) Apped to exisitng file if true otherwise create new file
//
//   bytesPerPixel
//      (in) Bytes per pixel.
//         RAW8 - 1 byte per pixel
//         RAW10, RAW12, RAW14 - 2 bytes per pixel

NvMediaStatus
WriteRAWImageToRGBA(
    char *filename,
    NvMediaImage *image,
    NvMediaBool appendFlag,
    NvU32 bytesPerPixel);

//  ReadPPMImage
//
//    ReadPPMImage()  Read PPM file to a image surface
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   image
//      (out) Pointer to pre-allocated image surface

NvMediaStatus
ReadPPMImage(
    char *fileName,
    NvMediaImage *image);

//  InitImage
//
//    InitImage()  Init image data to zeros
//
//  Arguments:
//
//   image
//      (in) image to initialize
//
//   bytesPerPixel
//      (in) For raw image, specify the number bytes for pixel.
//          For non-raw images, this parameter is ignored.

NvMediaStatus
InitImage(
    NvMediaImage *image,
    NvU32 bytesPerPixel);

//  ReadYUVBuffer
//
//    ReadYUVBuffer()  Read specific frame from YUV file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   uFrameNum
//      (in) Frame number to read
//
//   width
//      (in) buffer width
//
//   height
//      (in) buffer height
//
//   pFrame
//      (out) Pointer to pre-allocated output buffer
//
//   bOrderUV
//      (in) Flag for UV order. If true - UV; If false - VU;
NvMediaStatus
ReadYUVBuffer(
    FILE *file,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvU8 *pBuff,
    NvMediaBool bOrderUV);

//  ReadRGBABuffer
//
//    ReadRGBABuffer()  Read buffer from RGBA file
//
//  Arguments:
//
//   filename
//      (in) Input file name
//
//   width
//      (in) buffer width
//
//   height
//      (in) buffer height
//
//   rgbaSurface
//      (out) Pointer to pre-allocated output buffer

NvMediaStatus
ReadRGBABuffer(
    FILE *file,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvU8 *pBuff);

NvMediaStatus
GetFrameCrc(
    NvU8   *pBuff,
    NvU32  width,
    NvU32  height,
    NvU32  pitch,
    NvMediaSurfaceType imageType,
    NvU32  *crcOut,
    NvU32  rawBytesPerPixel);

#ifdef __cplusplus
}
#endif

#endif /* _NVMEDIA_TEST_SURF_UTILS_H_ */
