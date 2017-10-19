/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _CMDLINE_H_
#define _CMDLINE_H_

#include <nvcommon.h>
#include <nvmedia.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FILE_PATH_LENGTH_MAX    256

typedef enum
{
    EGLSTREAM_NVMEDIA_VIDEO = 0,
    EGLSTREAM_NVMEDIA_IMAGE = 1,
    EGLSTREAM_GL            = 2,
    EGLSTREAM_CUDA          = 3,

} ProdConsType;

typedef enum
{
    IMAGE_TYPE_YUV420 = 0,
    IMAGE_TYPE_RGBA = 1,
    IMAGE_TYPE_RAW = 2,
}ImageType;

typedef struct _TestModeArgs{
    NvBool  isTestMode;
    NvBool  isGenCrc;
    NvBool  isChkCrc;
    char    *refCrcFileName;
    FILE    *chkCrcFile;
    FILE    *refCrcFile;
    NvBool  isCrcMatched;
} TestModeArgs;

typedef struct _TestArgs {

    NvU32          width;
    NvU32          height;
    NvU32          frameCount;
    ImageType      imagetype;
    ProdConsType   producer;
    ProdConsType   consumer;
    char           *inpFileName;
    char           *outFileName;
    int            logLevel;
    NvBool         fifoMode;

    /*Cross proc*/
    NvBool         isCrossProc;
    NvBool         isProdCrossProc;
    NvBool         isConsCrossProc;

    /*Test mode params*/
    TestModeArgs   testModeParams;
    char           crcPathName[FILE_PATH_LENGTH_MAX];
    char           inputPathName[FILE_PATH_LENGTH_MAX];
    NvBool         pitchLinearOutput;
    NvBool         isConsumerondGPU;
    NvBool         useblitpath;
} TestArgs;

void PrintUsage(void);
int MainParseArgs(int argc, char **argv, TestArgs *args);

#endif
