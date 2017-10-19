/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _NVMEDIA_JPEG_TEST_CMD_LINE_H_
#define _NVMEDIA_JPEG_TEST_CMD_LINE_H_

#include "nvmedia_image.h"

#define FILE_NAME_SIZE            256
#define MAX_JPEG_BITSTREAM_BYTES  4096*4096
#define MAX_JPEG_DECODE_WIDTH     4096
#define MAX_JPEG_DECODE_HEIGHT    4096

typedef struct {
    char        *crcFilename;
    NvMediaBool crcGenMode;
    NvMediaBool crcCheckMode;
} CRCOptions;

typedef struct _TestArgs {
    char                        *infile;
    char                        *outfile;
    unsigned int                frameNum;
    unsigned int                outputWidth;
    unsigned int                outputHeight;
    NvMediaSurfaceType          outputSurfType;
    unsigned int                outputSurfAttributes;
    NvMediaImageAdvancedConfig  outputSurfAdvConfig;

    unsigned int                maxBitstreamBytes;
    unsigned int                maxWidth;
    unsigned int                maxHeight;
    unsigned char               downscaleLog2;
    NvMediaBool                 supportPartialAccel;

    CRCOptions                  crcoption;
    int                         logLevel;
} TestArgs;

void PrintUsage(void);
int  ParseArgs(int argc, char **argv, TestArgs *args);

#endif /* _NVMEDIA_JPEG_TEST_CMD_LINE_H_ */
