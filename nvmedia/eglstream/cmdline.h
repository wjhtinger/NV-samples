/*
 * cmdline.h
 *
 * Copyright (c) 2014-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Command line parsing for the test application
//

#ifndef _NVMEDIA_TEST_CMD_LINE_H_
#define _NVMEDIA_TEST_CMD_LINE_H_

#include <nvcommon.h>
#include <nvmedia.h>

#define MAX_IP_LEN       16

typedef struct _TestArgs {
    char   *infile;
    char   *outfile;
    int    logLevel;

    NvBool fifoMode;
    NvBool metadata;
    NvBool nvmediaConsumer;
    NvBool nvmediaProducer;
    NvBool nvmediaVideoProducer;
    NvBool nvmediaVideoConsumer;
    NvBool nvmediaImageProducer;
    NvBool nvmediaImageConsumer;
    NvBool nvmediaEncoder;
    NvBool cudaConsumer;
    NvBool cudaProducer;
    NvBool glConsumer;
    NvBool glProducer;
    NvBool screenConsumer;
    NvBool egloutputConsumer;

    NvU32 standalone;
    NvMediaSurfaceType prodSurfaceType;
    NvMediaSurfaceType consSurfaceType;
    NvBool prodIsRGBA;
    NvBool consIsRGBA;
    NvU32  windowId;
    NvU32  displayId;
    NvU32  inputWidth;
    NvU32  inputHeight;
    NvBool pitchLinearOutput;
    NvU32  prodLoop;
    NvU32  prodFrameCount;
    NvBool shaderType;
#ifdef NVMEDIA_GHSI
    int socketport;
    char ip[MAX_IP_LEN];
#endif
} TestArgs;

void PrintUsage(void);
int MainParseArgs(int argc, char **argv, TestArgs *args);

#endif
