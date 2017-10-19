/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _NVMEDIA_TEST_CMD_LINE_H_
#define _NVMEDIA_TEST_CMD_LINE_H_

#include "misc_utils.h"
#include "nvcommon.h"
#include "nvmedia.h"
#include "testutil_board.h"
#include "testutil_capture_input.h"

#define NVMEDIA_DEINTERLACE_TYPE_WEAVE   3
#define MAX_DISPLAYS                     2

typedef enum _CaptureType {
    CAPTURE_CSI  = 0,
    CAPTURE_VIP  = 1,
    CAPTURE_NONE = 2
} CaptureType;

typedef struct _DisplayParams {
    int                                  displayId;
    NvMediaBool                          isEnabled;
    NvMediaBool                          isPositionSpecified;
    NvMediaRect                          position;
    unsigned int                         windowId;
    unsigned int                         depth;
} VideoDisplayParams;

typedef struct _TestArgs {
    I2cId                                i2cDevice;
    NvMediaBool                          isLiveMode;
    NvMediaBool                          checkCRC;
    unsigned int                         crcChecksum;
    NvMediaBool                          displayEnabled;
    NvMediaVideoOutputType               outputType;
    VideoDisplayParams                   displaysList;
    int                                  logLevel;
    unsigned int                         timeout;
    char                                 configFileName[MAX_STRING_SIZE];
    char                                *paramSetName;
    unsigned int                         captureTime;
    int                                  captureCount;
    CaptureInputDeviceId                 captureDeviceInUse;
    NvMediaVideoCaptureInterfaceFormat   inputVideoStd;
    NvMediaBool                          fileDumpEnabled;
    char                                *outputFileName;
    float                                aspectRatio;
    unsigned int                         mixerWidth;
    unsigned int                         mixerHeight;
    unsigned int                         inputWidth;
    unsigned int                         inputHeight;
    CaptureType                          captureType;
    BoardType                            boardType;

    /* csi specific params */
    NvMediaVideoCaptureInterfaceType     csiPortInUse;
    unsigned int                         csiInterfaceLaneCount;
    NvMediaVideoCaptureInputFormatType   csiInputFormat;
    NvMediaBool                          csiCaptureInterlaced;
    NvMediaBool                          csiDeinterlaceEnabled;
    int                                  csiDeinterlaceType;
    int                                  csiDeinterlaceAlgo;
    NvBool                               csiInverceTelecine;
    unsigned int                         csiExtraLines;
    NvMediaVideoCaptureSurfaceFormatType csiSurfaceFormat;
    NvMediaBool                          externalBuffer;
    /* Video Surface */
    NvMediaSurfaceType                   outputSurfaceType;
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

//  PrintOptions
//
//    PrintOptions()  Prints capture test arguments

void PrintOptions(TestArgs *args);

#endif /* _NVMEDIA_TEST_CMD_LINE_H_ */
