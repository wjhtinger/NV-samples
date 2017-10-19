/*
 * Copyright (c) 2013-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cmdline.h"
#include "log_utils.h"
#include "misc_utils.h"

void PrintUsage()
{
    NvMediaVideoOutputDeviceParams videoOutputs[MAX_OUTPUT_DEVICES];
    NvMediaStatus rt;
    int outputDevicesNum, i;

    LOG_MSG("Usage: nvmvid_play [options]\n");
    LOG_MSG("Options:\n");
    LOG_MSG("-h                Prints usage\n");
    LOG_MSG("-c  [codec type]  Numeric codec type  = 1(MPEG), 2(MPEG4), 3(VC1), 4(H264) 5(VP8) 6(H265) 7(VP9)\n");
    LOG_MSG("-c  [codec type]  Text codec type  = mpeg, mpeg4, vc1, h264, vp8, h265, vp9\n");
    LOG_MSG("-f  [input file]  Input file name\n");
    LOG_MSG("-t                Show decode timing info\n");
    LOG_MSG("-r  [value]       Frame rate\n");
    LOG_MSG("-n  [frames]      Number of frames to decode\n");
    LOG_MSG("-l  [loops]       Number of loops of playback\n");
    LOG_MSG("                  -1 for infinite loops of playback (default: 1)\n");
    LOG_MSG("-s  [output file] Output YUV File name to save\n");
    LOG_MSG("-i  [deinterlace] Deinterlacing mode\n");
    LOG_MSG("                  0(Off), 1(BOB), 2(Advanced - Frame Rate), 3(Advanced - Field Rate)\n");
    LOG_MSG("-ia [deinterlacing algorithm] Used only for advanced deinterlacing modes\n");
    LOG_MSG("                  1(Advanced1), 2(Advanced2)\n");
    LOG_MSG("-it               Use inverse telecine\n");
    LOG_MSG("-fq               Set filter quality 1(Low), 2(Medium), 3(High)\n");
    LOG_MSG("-a  [value]       Aspect ratio\n");
    LOG_MSG("-w  [id]          Window ID. Default: 1\n");
    LOG_MSG("-p  [position]    Window position. Default: full screen size\n");
    LOG_MSG("-z  [depth]       Window depth. Default: 1\n");
    LOG_MSG("-d  [id]          Display ID\n");

    rt = GetAvailableDisplayDevices(&outputDevicesNum, &videoOutputs[0]);
    if(rt != NVMEDIA_STATUS_OK) {
        LOG_ERR("PrintUsage: Failed retrieving available video output devices\n");
        return;
    }

    LOG_MSG("                  Available display devices: %d\n", outputDevicesNum);
    for(i = 0; i < outputDevicesNum; i++) {
        LOG_MSG("                        Display ID: %d\n", videoOutputs[i].displayId);
    }

#ifdef NVMEDIA_QNX
    LOG_MSG("-screen           Use screen to render\n");
    LOG_MSG("-y2r              Uses screen format RGB to render\n");
#endif
    LOG_MSG("-v  [level]       Logging Level = 0(Errors), 1(Warnings), 2(Info), 3(Debug)\n");
}

int ParseArgs(int argc, char **argv, TestArgs *args)
{
    NvBool bLastArg = NV_FALSE;
    NvBool bDataAvailable = NV_FALSE;
    NvBool bHasCodecType = NV_FALSE;
    NvBool bHasFileName = NV_FALSE;
    int i, x = 0, y = 0, w = 0, h = 0;
    FILE *file;
    NvMediaStatus rt;

    /* app defaults */
    args->loop = 1;
    args->aspectRatio = 0.0;
    args->depth = 1;
    args->windowId = 1;
#ifdef NVMEDIA_QNX
    args->bScreen = NV_FALSE;
    args->bYuv2Rgb= NV_FALSE;
#endif

    SetLogLevel(LEVEL_ERR); // Default logging level

    for (i = 1; i < argc; i++) {
        // check if this is the last argument
        bLastArg = ((argc - i) == 1);

        // check if there is data available to be parsed following the option
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if (argv[i][0] == '-') {
            if (strcmp(&argv[i][1], "h") == 0) {
                PrintUsage();
                return 1;
            }
            else if (strcmp(&argv[i][1], "c") == 0) {
                if (bDataAvailable) {
                    struct {
                        char *name;
                        NvVideoCompressionStd codec;
                    } codecs[] = {
                        { "mpeg", NVCS_MPEG2 },
                        { "mpeg4", NVCS_MPEG4 },
                        { "vc1", NVCS_VC1 },
                        { "h264", NVCS_H264 },
                        { "vp8", NVCS_VP8 },
                        { "h265", NVCS_H265 },
                        { "vp9", NVCS_VP9 },
                        { NULL, NVCS_Unknown }
                    };
                    char *arg = argv[++i];
                    if (*arg >= '1' && *arg <= '7') {
                        args->eCodec = codecs[atoi(arg) - 1].codec;
                        bHasCodecType = NV_TRUE;
                    } else {
                        int j;
                        for(j = 0; codecs[j].name; j++) {
                            if (!strcasecmp(arg, codecs[j].name)) {
                                args->eCodec = codecs[j].codec;
                                bHasCodecType = NV_TRUE;
                                break;
                            }
                        }
                        if (!bHasCodecType) {
                            LOG_ERR("ParseArgs: -c must be followed by codec type\n");
                            return -1;
                        }
                    }
                } else {
                    LOG_ERR("ParseArgs: -c must be followed by codec type\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "f") == 0) {
                if (bDataAvailable) {
                    args->filename = argv[++i];
                    bHasFileName = NV_TRUE;
                    struct stat st;
                    file = fopen(args->filename, "rb");
                    if (!file) {
                        LOG_ERR("ParseArgs: failed to open stream %s\n", args->filename);
                        return -1;
                    }

                    if (stat(args->filename, &st) == -1) {
                        fclose(file);
                        LOG_ERR("ParseArgs: failed to stat stream %s\n", args->filename);
                        return -1;
                    }
                    args->fileSize = st.st_size;
                    fclose(file);
                } else {
                    LOG_ERR("ParseArgs: -f must be followed by file name\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "d") == 0) {
                if (bDataAvailable) {
                    args->displayEnabled = NV_TRUE;
                    if((sscanf(argv[++i], "%u", &args->displayId) != 1)) {
                        LOG_ERR("ParseArgs: Bad display id: %s\n", argv[i]);
                        return -1;
                    }
                    rt = CheckDisplayDeviceID(args->displayId, &args->displayDeviceEnabled);
                    if (rt != NVMEDIA_STATUS_OK) {
                        LOG_ERR("ParseArgs: Chosen display (%d) not available\n", args->displayId);
                        return -1;
                    }
                    LOG_DBG("ParseArgs: Chosen display: (%d) device enabled? %d nvmedia display device: %d\n",
                            args->displayId, args->displayDeviceEnabled);
                } else {
                    LOG_ERR("ParseArgs: -d must be followed by display id\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "w") == 0) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    args->windowId = atoi(arg);
                } else {
                    LOG_ERR("ParseArgs: -w must be followed by window id\n");
                    return -1;
                }
                if (args->windowId > 2) {
                    LOG_ERR("ParseArgs: Bad window ID: %d. Valid values are [0-2]. ", args->windowId);
                    LOG_ERR("           Using default window ID 0\n");
                    args->windowId = 0;
                }
            }
            else if (strcmp(&argv[i][1], "p") == 0) {
                if (bDataAvailable) {
                    if((sscanf(argv[++i], "%u:%u:%u:%u", &x, &y, &w, &h) != 4)) {
                        LOG_ERR("ParseArgs: Bad resolution: %s\n", argv[i]);
                        return -1;
                    }
                    args->position.x0 = x;
                    args->position.y0 = y;
                    args->position.x1 = x + w;
                    args->position.y1 = y + h;
                    args->positionSpecifiedFlag = NV_TRUE;
                } else {
                    LOG_ERR("ParseArgs: -p must be followed by window position x0:x1:width:height\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "z") == 0) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    args->depth = atoi(arg);
                } else {
                    LOG_ERR("ParseArgs: -z must be followed by depth value\n");
                    return -1;
                }
                if (args->depth > 255) {
                    LOG_ERR("ParseArgs: Bad depth value: %d. Valid values are [0-255]. ", args->depth);
                    LOG_ERR("           Using default depth value: 1\n");
                    args->depth = 1;
                }
            }
            else if (strcmp(&argv[i][1], "t") == 0) {
                args->showDecodeTimimg = NV_TRUE;
            }
            else if (strcmp(&argv[i][1], "v") == 0) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    args->logLevel = atoi(arg);
                    if (args->logLevel < LEVEL_ERR || args->logLevel > LEVEL_DBG) {
                        LOG_ERR("ParseArgs: Invalid logging level chosen (%d). ", args->logLevel);
                        LOG_ERR("           Setting logging level to LEVEL_ERR (0)\n");
                    }
                } else {
                    args->logLevel = LEVEL_DBG; // Max logging level
                }
                SetLogLevel((enum LogLevel)args->logLevel);
            }
            else if (strcmp(&argv[i][1], "r") == 0) {
                if (bDataAvailable) {
                    float framerate;
                    if (sscanf(argv[++i], "%f", &framerate)) {
                        args->frameTimeUSec = 1000000.0 / framerate;
                    } else {
                        LOG_ERR("ParseArgs: Invalid frame rate encountered (%s)\n", argv[i]);
                    }
                } else {
                    LOG_ERR("ParseArgs: -r must be followed by frame rate\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "n") == 0) {
                if (bDataAvailable) {
                    int decodeCount;
                    if (sscanf(argv[++i], "%d", &decodeCount)) {
                        args->numFramesToDecode = decodeCount;
                    } else {
                        LOG_ERR("ParseArgs: -n must be followed by decode frame count\n");
                    }
                } else {
                    LOG_ERR("ParseArgs: -n must be followed by frame count\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "s") == 0) {
                if (bDataAvailable) {
                    args->OutputYUVFilename = argv[++i];
                } else {
                    LOG_ERR("ParseArgs: -s must be followed by file name\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "l") == 0) {
                if (argv[i+1]) {
                    int loop;
                    if (sscanf(argv[++i], "%d", &loop) && loop >= -1 && loop != 0) {
                        args->loop = loop;
                    } else {
                        LOG_ERR("ParseArgs: Invalid loop count encountered (%s)\n", argv[i]);
                    }
                } else {
                    LOG_ERR("ParseArgs: -l must be followed by loop count\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "i") == 0) {
                if (bDataAvailable) {
                    int deinterlace;
                    if (sscanf(argv[++i], "%d", &deinterlace) && deinterlace >= 0 && deinterlace < 4) {
                        args->deinterlace = deinterlace;
                    } else {
                        LOG_ERR("ParseArgs: Invalid deinterlace mode encountered (%s)\n", argv[i]);
                    }
                } else {
                    LOG_ERR("ParseArgs: -i must be followed by deinterlacing mode\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "ia") == 0) {
                if (bDataAvailable) {
                    int deinterlaceAlgo;
                    if (sscanf(argv[++i], "%d", &deinterlaceAlgo) && deinterlaceAlgo >= 0 && deinterlaceAlgo < 3) {
                        args->deinterlaceAlgo = deinterlaceAlgo;
                    } else {
                        LOG_ERR("ParseArgs: Invalid deinterlace algorithm encountered (%s)\n", argv[i]);
                    }
                } else {
                    LOG_ERR("ParseArgs: -ia must be followed by deinterlacing algorithm\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "it") == 0) {
                args->inverceTelecine = NV_TRUE;
            }
            else if (strcmp(&argv[i][1], "a") == 0) {
                if (bDataAvailable) {
                    float aspectRatio;
                    if (sscanf(argv[++i], "%f", &aspectRatio) && aspectRatio > 0.0) {
                        args->aspectRatio = aspectRatio;
                    } else {
                        LOG_ERR("ParseArgs: Invalid aspect ratio encountered (%s)\n", argv[i]);
                    }
                } else {
                    LOG_ERR("ParseArgs: -a must be followed by aspect ratio\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "fq") == 0) {
                if (bDataAvailable) {
                    int filterQuality;
                    if (sscanf(argv[++i], "%d", &filterQuality) && filterQuality > 0 && filterQuality < 4) {
                        args->filterQuality = filterQuality;
                    } else {
                        LOG_ERR("ParseArgs: Invalid filter quality encountered (%s)\n", argv[i]);
                    }
                } else {
                    LOG_ERR("ParseArgs: -ft must be followed by filter quality\n");
                    return -1;
                }
            }
#ifdef NVMEDIA_QNX
            else if (strcmp(&argv[i][1], "screen") == 0) {
                args->bScreen = NV_TRUE;
            }
            else if (strcmp(&argv[i][1], "y2r") == 0) {
                args->bYuv2Rgb = NV_TRUE;
                args->bScreen = NV_TRUE; //YUV to RGB conversion mode handled
                                         //when screen API is used
            }
#endif
            else {
                LOG_ERR("ParseArgs: option %c is not supported anymore\n", argv[i][1]);
            }
        }
    }

    if (!bHasCodecType || !bHasFileName) {
        return -1;
    }

    return 0;
}
