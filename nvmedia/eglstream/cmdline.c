/*
 * cmdline.c
 *
 * Copyright (c) 2014-2017, NVIDIA CORPORATION. All rights reserved.
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

#include <stdlib.h>
#include <string.h>
#include <cmdline.h>
#include <misc_utils.h>
#include <log_utils.h>

// Command line producer/consumer IDs.  Not all producer and consumers are
// available on all platform builds.  Unsupported consumers are skipped to
// keep valid enums packed.
enum {
    VIDEO_PRODUCER=0,
    IMAGE_PRODUCER,
    GL_PRODUCER,
#ifdef CUDA_SUPPORT
    CUDA_PRODUCER,
#endif
    PRODUCER_COUNT,
    DEFAULT_PRODUCER=VIDEO_PRODUCER,
};

enum {
    VIDEO_CONSUMER=0,
    IMAGE_CONSUMER,
    GL_CONSUMER,
#ifdef CUDA_SUPPORT
    CUDA_CONSUMER,
#endif
#ifdef NVMEDIA_QNX
    SCREEN_WINDOW_CONSUMER,
#endif
#ifdef EGLOUTPUT_SUPPORT
    EGLOUTPUT_CONSUMER,
#endif
    CONSUMER_COUNT,
    DEFAULT_CONSUMER=VIDEO_CONSUMER,
};

void PrintUsage(void)
{
    NvMediaVideoOutputDeviceParams videoOutputs[MAX_OUTPUT_DEVICES];
    NvMediaStatus rt;
    int outputDevicesNum, i;

    LOG_MSG("Usage:\n");
    LOG_MSG("-h                         Print this usage\n");

    LOG_MSG("\nEGL-VideoProducer Params:\n");
    LOG_MSG("-f [file name]             Input File Name\n");
    LOG_MSG("-l [loops]                 Number of loops of playback\n");
    LOG_MSG("                           -1 for infinite loops of playback (default: 1)\n");

    LOG_MSG("\nEGL-ImageProducer Params:\n");
    LOG_MSG("-f [file]                  Input image file. \n");
    LOG_MSG("                           Input file should in YUV 420 format, UV order\n");
    LOG_MSG("-fr [WxH]                  Input file resolution\n");
    LOG_MSG("-pl                        Producer uses pitchlinear surface.\n");
    LOG_MSG("                           Default uses blocklinear\n");

    LOG_MSG("\n Common-Params:\n");

    LOG_MSG("-v [level]                 Verbose, diagnostics prints\n");
    LOG_MSG("-fifo                      Set FIFO mode for EGL stream\n");
    LOG_MSG("-producer [n]              Set %d(video producer),\n", VIDEO_PRODUCER);
    LOG_MSG("                               %d(image producer),\n", IMAGE_PRODUCER);
    LOG_MSG("                               %d(gl    producer),\n", GL_PRODUCER);
#ifdef CUDA_SUPPORT
    LOG_MSG("                               %d(cuda  producer),\n", CUDA_PRODUCER);
#endif
    LOG_MSG("                           Default: %d\n", DEFAULT_PRODUCER);
    LOG_MSG("-consumer [n]              Set %d(video consumer),\n", VIDEO_CONSUMER);
    LOG_MSG("                               %d(image consumer),\n", IMAGE_CONSUMER);
    LOG_MSG("                               %d(gl    consumer),\n", GL_CONSUMER);
#ifdef CUDA_SUPPORT
    LOG_MSG("                               %d(cuda  consumer),\n", CUDA_CONSUMER);
#endif
#ifdef NVMEDIA_QNX
    LOG_MSG("                               %d(screen window consumer),\n", SCREEN_WINDOW_CONSUMER);
#endif
#ifdef EGLOUTPUT_SUPPORT
    LOG_MSG("                               %d(egloutput consumer),\n", EGLOUTPUT_CONSUMER);
#endif
    LOG_MSG("                           Default: %d\n", DEFAULT_CONSUMER);
    LOG_MSG("-metadata                  Enable metadata for EGL stream\n");
    LOG_MSG("-enc [file]                Output 264 bitstream, may be set when consumer=%d\n", VIDEO_CONSUMER);
    LOG_MSG("-standalone [n]            Set 0(not standalone, producer/consumer),\n");
    LOG_MSG("                               1(producer), 2(consumer), default=0\n");

    LOG_MSG("-w [window]                Display hardware window Id [0-2] (default=1)\n");
    LOG_MSG("-d [id]                    Display ID\n");
    LOG_MSG("-ot [type]                 Surface type: yuv420/rgba/y10(y10 only works with image producer and gl/cuda consumer)\n");
    LOG_MSG("                           Default: rgba\n");
    LOG_MSG("-shader [type]             shader type: yuv/rgb(default)\n");
#ifdef NVMEDIA_GHSI
    LOG_MSG("-socketport                set port for cross process eglstream communication[1024-49151]. Default: 8888\n");
#endif
    LOG_MSG("                           shader type can only be used when gl consumer enabled\n");

    rt = GetAvailableDisplayDevices(&outputDevicesNum, &videoOutputs[0]);
    if(rt != NVMEDIA_STATUS_OK) {
        LOG_ERR("PrintUsage: Failed retrieving available video output devices\n");
        return;
    }

    LOG_MSG("\nAvailable display devices (%d):\n", outputDevicesNum);
    for(i = 0; i < outputDevicesNum; i++) {
        LOG_MSG("Display ID: %d \n", videoOutputs[i].displayId);
    }
}

int MainParseArgs(int argc, char **argv, TestArgs *args)
{
    int bLastArg = 0;
    int bDataAvailable = 0;
    int i;
    NvMediaBool displayDeviceEnabled;
    NvMediaStatus rt;
    NvMediaBool useShader = NV_FALSE;

#ifdef NVMEDIA_GHSI
    args->socketport = 8888;
    strncpy(args->ip, "127.0.0.1", 16);
#endif

    for (i = 1; i < argc; i++) {
        // check if this is the last argument
        bLastArg = ((argc - i) == 1);

        // check if there is data available to be parsed following the option
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if (argv[i][0] == '-') {
            if (strcmp(&argv[i][1], "h") == 0) {
                PrintUsage();
                return 0;
            }
            else if (strcmp(&argv[i][1], "fifo") == 0) {
                args->fifoMode = NV_TRUE;
            }
            else if (strcmp(&argv[i][1], "metadata") == 0) {
                args->metadata = NV_TRUE;
            }
            else if (strcmp(&argv[i][1], "v") == 0) {
                int logLevel = LEVEL_DBG;
                if(bDataAvailable) {
                    logLevel = atoi(argv[++i]);
                    if(logLevel < LEVEL_ERR || logLevel > LEVEL_DBG) {
                        LOG_INFO("MainParseArgs: Invalid logging level chosen (%d). ", logLevel);
                        LOG_INFO("           Setting logging level to LEVEL_ERR (0)\n");
                        logLevel = LEVEL_ERR;
                    }
                }
                SetLogLevel(logLevel);
            }
            else if (strcmp(&argv[i][1], "d") == 0) {
                if (bDataAvailable) {
                    if((sscanf(argv[++i], "%u", &args->displayId) != 1)) {
                        LOG_ERR("Err: Bad display id: %s\n", argv[i]);
                        return 0;
                    }
                    rt = CheckDisplayDeviceID(args->displayId, &displayDeviceEnabled);
                    if (rt != NVMEDIA_STATUS_OK) {
                        LOG_ERR("Err: Chosen display (%d) not available\n", args->displayId);
                        return 0;
                    }
                    LOG_DBG("ParseArgs: Chosen display: (%d) device enabled? %d \n",
                            args->displayId, displayDeviceEnabled);
                } else {
                    LOG_ERR("Err: -d must be followed by display id\n");
                    return 0;
                }
            }
            else if (strcmp(&argv[i][1], "consumer") == 0) {
                if (bDataAvailable) {
                    unsigned int type;
                    if (sscanf(argv[++i], "%d", &type)) {
                        if (type >= CONSUMER_COUNT){
                            LOG_ERR("ERR: -consumer must be set to 0-%d\n", CONSUMER_COUNT-1);
                            return 0;
                        }
                        args->nvmediaConsumer =      (type==VIDEO_CONSUMER ||
                                                      type==IMAGE_CONSUMER);
                        args->nvmediaVideoConsumer = (type==VIDEO_CONSUMER);
                        args->nvmediaImageConsumer = (type==IMAGE_CONSUMER);
                        args->glConsumer =           (type==GL_CONSUMER);
#ifdef CUDA_SUPPORT
                        args->cudaConsumer =         (type==CUDA_CONSUMER);
#endif
#ifdef NVMEDIA_QNX
                        args->screenConsumer =       (type==SCREEN_WINDOW_CONSUMER);
#endif
#ifdef EGLOUTPUT_SUPPORT
                        args->egloutputConsumer =    (type==EGLOUTPUT_CONSUMER);
#endif
                    } else {
                        LOG_ERR("ERR: -consumer must be set to 0-%d\n", CONSUMER_COUNT-1);
                    }
                } else {
                    LOG_ERR("ERR: -consumer must be set to 0-%d\n", CONSUMER_COUNT-1);
                    return 0;
                }
            }
            else if (strcmp(&argv[i][1], "producer") == 0) {
                if (bDataAvailable) {
                    unsigned int type;
                    if (sscanf(argv[++i], "%d", &type)) {
                        if (type >= PRODUCER_COUNT) {
                            LOG_ERR("ERR: -producer must be set to 0-%d\n", PRODUCER_COUNT-1);
                            return 0;
                        }
                        args->nvmediaVideoProducer = (type==VIDEO_PRODUCER);
                        args->nvmediaImageProducer = (type==IMAGE_PRODUCER);
                        args->nvmediaProducer =      (type==VIDEO_PRODUCER ||
                                                      type==IMAGE_PRODUCER);
#ifdef CUDA_SUPPORT
                        args->cudaProducer =         (type==CUDA_PRODUCER);
#endif
                        args->glProducer =           (type==GL_PRODUCER);
                    } else {
                        LOG_ERR("ERR: -producer must be set to 0-%d\n", PRODUCER_COUNT-1);
                    }
                } else {
                    LOG_ERR("ERR: -producer must be set to 0-%d\n", PRODUCER_COUNT-1);
                    return 0;
                }
            }
            else if (strcmp(&argv[i][1], "enc") == 0 ) {
                args->nvmediaEncoder = NV_TRUE;
                args->outfile = argv[++i];
            }
            else if (strcmp(&argv[i][1], "standalone") == 0 ) {
                if(!bDataAvailable || !sscanf(argv[++i], "%u", &args->standalone) || (args->standalone > 2)) {
                    LOG_ERR("ERR: -standalone must be followed by mode [0-2].\n");
                    return 0;
                }
            }
            else if (strcmp(&argv[i][1], "w") == 0) {
                if (!bDataAvailable || !sscanf(argv[++i], "%u", &args->windowId) || (args->windowId > 2)) {
                    LOG_ERR("ERR: -w must be followed by window id [0-2].\n");
                    return 0;
                }
            }
#ifdef NVMEDIA_GHSI
            else if (strcmp(&argv[i][1], "socketport") == 0) {
                if (!bDataAvailable || !sscanf(argv[++i], "%u", &args->socketport)) {
                    LOG_ERR("ERR: -socketport must be followed by port number\n");
                    return 0;
                }
                if ((args->socketport < 1024) || (args->socketport > 49151)) {
                    LOG_ERR("ERR: Invalid socket port\n");
                    return 0;
                }
            }
#endif
        }
    }

    if (args->nvmediaVideoProducer)
        args->prodSurfaceType = NvMediaSurfaceType_R8G8B8A8_BottomOrigin;
    else if (args->nvmediaImageProducer)
        args->prodSurfaceType = NvMediaSurfaceType_Image_RGBA;
    if (args->nvmediaVideoConsumer)
        args->consSurfaceType = NvMediaSurfaceType_R8G8B8A8_BottomOrigin;
    else if (args->nvmediaImageConsumer)
        args->consSurfaceType = NvMediaSurfaceType_Image_RGBA;
    else
        args->consSurfaceType = NvMediaSurfaceType_R8G8B8A8_BottomOrigin; //by default

    args->shaderType = 0; // default, using RGB shader

    // The rest
    for(i = 0; i < argc; i++) {
        bLastArg = ((argc - i) == 1);

        // check if there is data available to be parsed following the option
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if (argv[i][0] == '-') {
            if (strcmp(&argv[i][1], "ot") == 0 ) {
                if(bDataAvailable) {
                    ++i;
                    if(!strcasecmp(argv[i], "yuv420")) {
                        if (args->nvmediaVideoProducer)
                            args->prodSurfaceType = NvMediaSurfaceType_Video_420;
                        else if (args->nvmediaImageProducer)
                            args->prodSurfaceType = NvMediaSurfaceType_Image_YUV_420;
                        else if (args->cudaProducer)
                            args->prodIsRGBA = NV_FALSE;

                        if (args->nvmediaVideoConsumer)
                            args->consSurfaceType = NvMediaSurfaceType_Video_420;
                        else if (args->nvmediaImageConsumer)
                            args->consSurfaceType = NvMediaSurfaceType_Image_YUV_420;
                        else if (args->cudaConsumer)
                            args->consIsRGBA = NV_FALSE;
                        else if (args->glConsumer)
                            args->consSurfaceType = NvMediaSurfaceType_Video_420;
                        else if (args->egloutputConsumer)
                            args->consSurfaceType = NvMediaSurfaceType_Video_420;
                    } else if(!strcasecmp(argv[i], "rgba")) {
                        if (args->nvmediaVideoProducer)
                            args->prodSurfaceType = NvMediaSurfaceType_R8G8B8A8_BottomOrigin;
                        else if (args->nvmediaImageProducer)
                            args->prodSurfaceType = NvMediaSurfaceType_Image_RGBA;
                        else if (args->cudaProducer)
                            args->prodIsRGBA = NV_TRUE;

                        if (args->nvmediaVideoConsumer)
                            args->consSurfaceType = NvMediaSurfaceType_R8G8B8A8_BottomOrigin;
                        else if (args->nvmediaImageConsumer)
                            args->consSurfaceType = NvMediaSurfaceType_Image_RGBA;
                        else if (args->cudaConsumer)
                            args->consIsRGBA = NV_TRUE;
                        else if (args->glConsumer)
                            args->consSurfaceType = NvMediaSurfaceType_R8G8B8A8_BottomOrigin;
                    } else if(!strcasecmp(argv[i], "y10")) {
                        args->prodSurfaceType = NvMediaSurfaceType_Image_Y10;
                        args->consSurfaceType = NvMediaSurfaceType_Image_Y10;
                    } else {
                        printf("ERR: ParseArgs: Unknown output surface type encountered: %s\n", argv[i]);
                        return 0;
                    }
                } else {
                    printf("ERR: ParseArgs: -ot must be followed by surface type\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "f") == 0) {
                // Input file name
                if(bDataAvailable) {
                    args->infile = argv[++i];
                } else {
                    LOG_ERR("ParseArgs: -f must be followed by input file name\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "fr") == 0) {
                if(bDataAvailable) {
                    if((sscanf(argv[++i], "%ux%u", &args->inputWidth, &args->inputHeight) != 2)) {
                        LOG_ERR("ParseArgs: Bad output resolution: %s\n", argv[i]);
                        return 0;
                    }
                } else {
                    LOG_ERR("ParseArgs: -fr must be followed by resolution\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "pl") == 0) {
                args->pitchLinearOutput = NV_TRUE;
            } else if (strcmp(&argv[i][1], "l") == 0) {
                if (argv[i+1]) {
                    int loop;
                    if (sscanf(argv[++i], "%d", &loop) && loop >= -1 && loop != 0) {
                        args->prodLoop = loop;
                    } else {
                        printf("ERR: Invalid loop count encountered (%s)\n", argv[i]);
                    }
                } else {
                    printf("ERR: -l must be followed by loop count.\n");
                    return 0;
                }
            } else if (strcmp(&argv[i][1], "n") == 0) {
                if (bDataAvailable) {
                    int frameCount;
                    if (sscanf(argv[++i], "%d", &frameCount)) {
                        args->prodFrameCount = frameCount;
                    } else {
                        LOG_DBG("ERR: -n must be followed by decode frame count.\n");
                    }
                } else {
                    LOG_DBG("ERR: -n must be followed by frame count.\n");
                    return 0;
                }
            } else if(strcmp(&argv[i][1], "shader") == 0) {
                if(bDataAvailable) {
                    i++;
                    if(!strcasecmp(argv[i], "yuv")) {
                        args->shaderType = NV_TRUE;
                        LOG_DBG("using yuv shader.\n");
                    } else if(!strcasecmp(argv[i], "rgb")) {
                        args->shaderType = NV_FALSE;
                        LOG_DBG("using rgb shader.\n");
                    } else {
                        LOG_ERR("ParseArgs: -shader unknown shader type)\n");
                        return 0;
                    }
                    useShader = NV_TRUE;
                } else {
                    LOG_ERR("ParseArgs: -shader rgb | yuv\n");
                    return 0;
                }
            }
        }
    }

    if ((args->nvmediaImageProducer || args->cudaProducer) &&
       !(args->inputWidth && args->inputHeight)) {
        LOG_ERR("Input File Resolution must be specified for image/CUDA producer (use -fr option)\n");
        return 0;
    }
    //check vadility, nvmediaEncoder can be used only when nvmedia video consumer is enable
    if ( args->nvmediaEncoder && (!args->nvmediaVideoConsumer)){
        LOG_ERR("Please use nvmedia video consumer when do the encoding\n");
        return 0;
    }

    if (args->cudaConsumer && (args->standalone!=2) &&
       !(args->nvmediaVideoProducer || args->nvmediaImageProducer || args->cudaProducer)) {
        LOG_ERR("Invalid EGL pipeline, please use video/image or CUDA producer with CUDA consumer\n");
        return 0;
    }

    if (args->cudaProducer && (args->standalone!=1) && (!args->cudaConsumer)) {
        LOG_ERR("Invalid EGL pipeline, please use CUDA consumer with CUDA producer\n");
        return 0;
    }

    if (args->metadata) {
        if (!args->nvmediaImageProducer && (args->standalone!=2)) {
            LOG_ERR("Please enable metadata only with image producer and image consumer\n");
            return 0;
        }
        if (!args->nvmediaImageConsumer && (args->standalone!=1)) {
            LOG_ERR("Please enable metadata only with image producer and image consumer\n");
            return 0;
        }
    }

    if(!args->glConsumer && useShader) {
        LOG_INFO("-shader command only use with gl consumer enabled, ignore shader command here\n");
    }

    if(args->glConsumer && args->shaderType &&
            (args->consSurfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin || args->consSurfaceType == NvMediaSurfaceType_Image_RGBA)) {
        LOG_INFO("rgba surface type can only use rgb shader, set shader to rgb\n");
        args->shaderType = NV_FALSE;
    }
    return 1;
}
