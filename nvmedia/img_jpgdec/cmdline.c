/*
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmdline.h"
#include "config_parser.h"
#include "log_utils.h"

void PrintUsage()
{
    LOG_MSG("Usage: nvmedia_dec [options]\n");
    LOG_MSG("Options:\n");
    LOG_MSG("-h                         Prints usage\n");
    LOG_MSG("-f [file]                  Input jpeg file. \n");
    LOG_MSG("-of [file]                 Output image file. \n");
    LOG_MSG("-fr [WxH]                  Output file resolution\n");
    LOG_MSG("-fn [value]                Decoder frame number\n");
    LOG_MSG("-format                    Output format: 0(yuv420), 1(rgba), 2(yuv422), 3(yuv444)\n");
    LOG_MSG("-ds [value]                Decoder output downscale factor (in log2 manner) (0..3)\n");
    LOG_MSG("-crcgen [crcs.txt]         Generate CRC values\n");
    LOG_MSG("-crcchk [crcs.txt]         Check CRC values\n");
    LOG_MSG("-v  [level]                Logging Level = 0(Errors), 1(Warnings), 2(Info), 3(Debug)\n");
}

int ParseArgs(int argc, char **argv, TestArgs *args)
{
    int i;
    int bLastArg = 0;
    int bDataAvailable = 0;

    // Defaults
    args->frameNum = 1;
    args->maxBitstreamBytes = MAX_JPEG_BITSTREAM_BYTES;
    args->maxWidth = MAX_JPEG_DECODE_WIDTH;
    args->maxHeight = MAX_JPEG_DECODE_HEIGHT;
    args->outputSurfType = NvMediaSurfaceType_Image_YUV_420;
    //init crcoption
    args->crcoption.crcGenMode = NVMEDIA_FALSE;
    args->crcoption.crcCheckMode = NVMEDIA_FALSE;

    if((argc == 2 && (strcasecmp(argv[1], "-h") == 0)) || argc < 2) {
        PrintUsage();
        exit(0);
    }

    for(i = 0; i < argc; i++) {
        bLastArg = ((argc - i) == 1);

        // check if there is data available to be parsed following the option
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if(argv[i][0] == '-') {
            if(strcmp(&argv[i][1], "h") == 0) {
                PrintUsage();
                exit(0);
            } else if(strcmp(&argv[i][1], "v") == 0) {
                args->logLevel = LEVEL_DBG;
                if(bDataAvailable) {
                    args->logLevel = atoi(argv[++i]);
                    if(args->logLevel < LEVEL_ERR || args->logLevel > LEVEL_DBG) {
                        LOG_INFO("MainParseArgs: Invalid logging level chosen (%d). ", args->logLevel);
                        LOG_INFO("           Setting logging level to LEVEL_ERR (0)\n");
                        args->logLevel = LEVEL_ERR;
                    }
                }
                SetLogLevel(args->logLevel);
            } else if(strcmp(&argv[i][1], "f") == 0) {
                // Input file name
                if(bDataAvailable) {
                    args->infile = argv[++i];
                } else {
                    LOG_ERR("ParseArgs: -f must be followed by input file name\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "of") == 0) {
                // Output file name
                if(bDataAvailable) {
                    args->outfile = argv[++i];
                } else {
                    LOG_ERR("ParseArgs: -of must be followed by output file name\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "fr") == 0) {
                if(bDataAvailable) {
                    if((sscanf(argv[++i], "%ux%u", &args->outputWidth, &args->outputHeight) != 2)) {
                        LOG_ERR("ParseArgs: Bad output resolution: %s\n", argv[i]);
                        return 0;
                    }
                } else {
                    LOG_ERR("ParseArgs: -fr must be followed by output resolution\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "fn") == 0) {
                if(bDataAvailable) {
                    args->frameNum = atoi(argv[++i]);
                    if(args->frameNum <= 0) {
                        LOG_ERR("MainParseArgs: Invalid frame number (%d). ", args->frameNum);
                        LOG_ERR("               Frame number should be positive number\n");
                        return 0;
                    }
                } else {
                    LOG_ERR("ParseArgs: -fn must be followed by frame number\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "ds") == 0) {
                if(bDataAvailable) {
                    args->downscaleLog2 = atoi(argv[++i]);
                    if(args->downscaleLog2 < 0 || args->downscaleLog2 > 3) {
                        LOG_ERR("MainParseArgs: Invalid downscale factor (%d). ", args->downscaleLog2);
                        LOG_ERR("               downscale level should be in [0..3] inclusive\n");
                        return 0;
                    }
                } else {
                    LOG_ERR("ParseArgs: -ds must be followed by downscale factor [0..3] inclusive\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "format") == 0) {
                if(bDataAvailable) {
                    int format = atoi(argv[++i]);
                    switch(format) {
                    case 0:
                        args->outputSurfType = NvMediaSurfaceType_Image_YUV_420;
                        break;
                    case 1:
                        args->outputSurfType = NvMediaSurfaceType_Image_RGBA;
                        break;
                    case 2:
                        args->outputSurfType = NvMediaSurfaceType_Image_YUV_422;
                        break;
                    case 3:
                        args->outputSurfType = NvMediaSurfaceType_Image_YUV_444;
                        break;
                    default:
                        LOG_ERR("MainParseArgs: Invalid format index (%d). ", format);
                        LOG_ERR("               Format index should be 0(yuv420), 1(rgba), 2(yuv422), 3(yuv444)\n");
                        return 0;
                    }
                } else {
                    LOG_ERR("ParseArgs: -format must be followed by index 0(yuv420), 1(rgba), 2(yuv422), 3(yuv444)\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "crcchk") == 0) {
                // crc check
                if(bDataAvailable) {
                    args->crcoption.crcCheckMode = NVMEDIA_TRUE;
                    args->crcoption.crcFilename = argv[++i];
                } else {
                    LOG_ERR("ParseArgs: -crcchk must be followed by crc file name\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "crcgen") == 0) {
                // crc generate
                if(bDataAvailable) {
                    args->crcoption.crcGenMode = NVMEDIA_TRUE;
                    args->crcoption.crcFilename = argv[++i];
                } else {
                    LOG_ERR("ParseArgs: -crcgen must be followed by crc file name\n");
                    return 0;
                }
            }
        }

    }

    if (!args->infile || !args->outfile) {
        LOG_ERR("ParseArgs: command line not complete\n");
        PrintUsage();
        return 0;
    }

    return 1;
}
