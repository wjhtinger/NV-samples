/*
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _NVMEDIA_JPEG_TEST_CMD_LINE_H_
#define _NVMEDIA_JPEG_TEST_CMD_LINE_H_

#include "nvmedia_image.h"

#define FILE_NAME_SIZE                  256

typedef struct {
    char        *crcFilename;
    NvMediaBool crcGenMode;
    NvMediaBool crcCheckMode;
} CRCOptions;

typedef struct _TestArgs {
    char                        *infile;
    char                        *outfile;
    unsigned int                inputWidth;
    unsigned int                inputHeight;
    NvMediaSurfaceType          inputSurfType;
    unsigned int                inputSurfAttributes;
    NvMediaImageAdvancedConfig  inputSurfAdvConfig;

    unsigned int                maxOutputBuffering;
    unsigned char               quality;

    CRCOptions                  crcoption;
    int                         logLevel;
} TestArgs;

void PrintUsage(void);
int  ParseArgs(int argc, char **argv, TestArgs *args);

#endif /* _NVMEDIA_JPEG_TEST_CMD_LINE_H_ */
