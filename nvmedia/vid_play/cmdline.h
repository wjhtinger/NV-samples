/*
 * Copyright (c) 2013-2016 NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _NVMEDIA_TEST_CMD_LINE_H_
#define _NVMEDIA_TEST_CMD_LINE_H_

/*
 * Macro Variable to enable QNX secific code
 */
#ifdef __QNXNTO__
#ifndef NVMEDIA_QNX
#define NVMEDIA_QNX
#endif
#endif

#include "nvmedia.h"
#include "video_parser.h"

typedef struct _TestArgs {
    int                      logLevel;
    NvVideoCompressionStd    eCodec;
    char                    *filename;
    NvS64                    fileSize;
    char                    *OutputYUVFilename;
    int                      loop;
    float                    aspectRatio;
    double                   frameTimeUSec;
    int                      numFramesToDecode;
    int                      deinterlace;
    int                      deinterlaceAlgo;
    NvBool                   inverceTelecine;
    NvBool                   showDecodeTimimg;
    int                      displayId;
    NvBool                   displayEnabled;
    NvMediaBool              displayDeviceEnabled;
    NvBool                   positionSpecifiedFlag;
    NvMediaRect              position;
    unsigned int             windowId;
    unsigned int             depth;
    unsigned int             filterQuality;
    NvBool                   bScreen;
    NvBool                   bYuv2Rgb;
} TestArgs;

//  PrintUsage
//
//    PrintUsage()  Prints video demo application usage options

void PrintUsage(void);

//  ParseArgs
//
//    ParseArgs()  Parsing command line arguments
//
//  Arguments:
//
//   argc
//      (in) Number of tokens in the command line
//
//   argv
//      (in) Command line tokens
//
//   args
//      (out) Pointer to test arguments structure

int  ParseArgs(int argc, char **argv, TestArgs *args);

#endif /* _NVMEDIA_TEST_CMD_LINE_H_ */
