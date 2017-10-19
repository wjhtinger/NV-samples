/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "nvmedia.h"
#include "nvcommon.h"
#include "misc_utils.h"
#include "cmdline.h"
#include "ipp.h"
#include "main.h"
#include "interop.h"

static NvMediaBool *quitFlag;

static void
SigHandler(int signum)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGKILL, SIG_IGN);
    signal(SIGSTOP, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    *quitFlag = NVMEDIA_TRUE;

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGKILL, SIG_DFL);
    signal(SIGSTOP, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
}

static void
SigSetup(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SigHandler;

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGKILL, &action, NULL);
    sigaction(SIGSTOP, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
}

static void
ParseRuntimeCommand(IPPCtx *ctx)
{
    char input[256] = {0};

    if (!fgets(input, 256, stdin)) {
        if(*quitFlag != NVMEDIA_TRUE) {
            LOG_ERR("%s: Failed to read command\n", __func__);
        }
    }
    else if (strlen(input) > 1) {
        if (input[strlen(input) - 1] == '\n') {
            input[strlen(input) - 1] = '\0';
        }

        if (!strcasecmp(input, "q") || !strcasecmp(input, "quit")) {
            *quitFlag = NVMEDIA_TRUE;
        }
        else if (!strcasecmp(input, "h") || !strcasecmp(input, "help")) {
            PrintRuntimeUsage();
        }
        else if (!strcasecmp(input, "c") || !strcasecmp(input, "cycle")) {
            if (ctx->imagesNum > 1) {
                ctx->displayCameraId = (ctx->displayCameraId + 1) % ctx->imagesNum;
            }
            else {
                printf("Only one camera available\n");
            }
        }
        else {
            LOG_ERR("Invalid command, type \"h\" for usage\n");
        }
    }

    return;
}

static NvMediaStatus
setExtImgDevParameters(IPPCtx *ctx,
                       CaptureConfigParams *captureParams,
                       ExtImgDevParam *configParam)
{
    NvU32 i;

    configParam->desAddr = captureParams->desAddr;
    configParam->brdcstSerAddr = captureParams->brdcstSerAddr;
    configParam->brdcstSensorAddr = captureParams->brdcstSensorAddr;
    for (i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
        configParam->sensorAddr[i] = captureParams->brdcstSensorAddr + i + 1;
    }
    configParam->i2cDevice = captureParams->i2cDevice;
    configParam->moduleName = captureParams->inputDevice;
    configParam->board = captureParams->board;
    configParam->resolution = captureParams->resolution;
    configParam->sensorsNum = ctx->imagesNum;
    configParam->inputFormat = captureParams->inputFormat;
    configParam->interface = captureParams->interface;
    configParam->enableEmbLines =
        (captureParams->embeddedDataLinesTop ||
         captureParams->embeddedDataLinesBottom) ?
         NVMEDIA_TRUE : NVMEDIA_FALSE;
    configParam->initialized = NVMEDIA_FALSE;
    configParam->enableSimulator = NVMEDIA_FALSE;
    configParam->enableVirtualChannels = ctx->useVirtualChannels;
    configParam->enableExtSync = captureParams->enableExtSync;
    configParam->dutyRatio =
        ((captureParams->dutyRatio <= 0.0) || (captureParams->dutyRatio >= 1.0)) ?
            0.25 : captureParams->dutyRatio;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
setInputSurfSettings(IPPCtx *ctx,
                     NvMediaICPSettings *icpSettings,
                     ExtImgDevProperty *property,
                     CaptureConfigParams *captureParams)
{
    char *inputFormat = captureParams->inputFormat;

    if (!strcasecmp(inputFormat, "raw12")) {
        ctx->inputSurfFormat.surfaceFormatType = NVMEDIA_IMAGE_CAPTURE_SURFACE_FORMAT_TYPE_RAW;
        ctx->inputSurfFormat.bitsPerPixel = property->bitsPerPixel;
        ctx->inputSurfFormat.pixelOrder = property->pixelOrder;
        ctx->inputSurfType = NvMediaSurfaceType_Image_RAW;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_BITS_PER_PIXEL;
        ctx->inputSurfAttributes |= NVMEDIA_IMAGE_ATTRIBUTE_RAW_PIXEL_ORDER;
        ctx->inputSurfAdvConfig.bitsPerPixel = property->bitsPerPixel;
        ctx->inputSurfAdvConfig.pixelOrder = property->pixelOrder;
        ctx->rawBytesPerPixel = 2;
        ctx->rawCompressionFormat = RAW1x12;
    }
    else {
        LOG_ERR("%s: Bad input format specified: %s \n",
                __func__, inputFormat);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    ctx->inputSurfAdvConfig.embeddedDataLinesTop = ctx->embeddedLinesTop = property->embLinesTop;
    ctx->inputSurfAdvConfig.embeddedDataLinesBottom = ctx->embeddedLinesBottom = property->embLinesBottom;
    ctx->inputSurfAttributes |= (NVMEDIA_IMAGE_ATTRIBUTE_EXTRA_LINES |
                             ctx->inputSurfAttributes);

    return NVMEDIA_STATUS_OK;
}

int
main(int argc, char **argv)
{
    IPPCtx *ctx;
    TestArgs args;
    ExtImgDevParam extImgDevParam;
    CaptureConfigParams *captureParams;
    NvU32 setId;
    InteropContext *interopCtx = NULL;

    ctx = calloc(1, sizeof(IPPCtx));
    if (!ctx) {
        LOG_ERR("%s: Failed to allocate memory\n", __func__);
        goto failed;
    }

    interopCtx = calloc(1, sizeof(InteropContext));
    if (!interopCtx) {
        LOG_ERR("%s: Failed to allocate memory for interopCtx", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    quitFlag = &ctx->quit;
    SigSetup();

    memset(&args, 0, sizeof(TestArgs));
    memset(&extImgDevParam, 0, sizeof(extImgDevParam));

    if (IsFailed(ParseArgs(argc, argv, &args))) {
        return NVMEDIA_STATUS_ERROR;
    }

    // set configuration from command-line arguments and config file
    ctx->displayId = args.displayId;
    ctx->windowId = args.windowId;
    ctx->imagesNum = args.imagesNum;
    ctx->showTimeStamp = args.showTimeStamp;
    ctx->saveEnabled = args.saveEnabled;
    ctx->displayEnabled = args.displayEnabled;
    ctx->useVirtualChannels = args.usevc;
    strncpy(ctx->filename, args.filename, MAX_STRING_SIZE - 1);
    ctx->filename[MAX_STRING_SIZE - 1] = '\0';

    setId = args.configId;
    captureParams = &args.captureConfigs[setId];
    captureParams->enableExtSync = args.enableExtSync;
    captureParams->dutyRatio = args.dutyRatio;
    LOG_DBG("%s: setId=%d, input resolution %s\n",
            __func__,
            setId,
            captureParams->resolution);
    if (sscanf(captureParams->resolution,
               "%ux%u",
               &ctx->inputWidth,
               &ctx->inputHeight) != 2) {
        LOG_ERR("%s: Invalid input resolution %s\n",
                __func__,
                captureParams->resolution);
        goto failed;
    }
    LOG_DBG("%s: inputWidth =%d, ctx->inputHeight =%d\n",
            __func__,
            ctx->inputWidth,
            ctx->inputHeight);

    // create device
    ctx->device = NvMediaDeviceCreate();
    if (!ctx->device) {
        LOG_ERR("%s: Failed to create NvMedia device\n", __func__);
        goto failed;
    }

    // configure ExtImgDevice settings
    if (IsFailed(setExtImgDevParameters(ctx, captureParams, &extImgDevParam))) {
        LOG_ERR("%s: Failed to set ExtImgDevice settings\n", __func__);
        goto failed;
    }

    // create ExtImgDevice
    ctx->extImgDevice = ExtImgDevInit(&extImgDevParam);
    if (!ctx->extImgDevice) {
        LOG_ERR("%s: Failed to initialize ISC device\n", __func__);
        goto failed;
    }

    // configure input surface settings
    if (IsFailed(setInputSurfSettings(ctx,
                                      &ctx->icpSettings,
                                      &ctx->extImgDevice->property,
                                      captureParams))) {
        LOG_ERR("%s: Failed to set ICP settings\n", __func__);
        goto failed;
    }


    // create IPP
    if (IsFailed(IPPInit(ctx, &args))) {
        LOG_ERR("%s: Error in IPP initialization\n", __func__);
        goto failed;
    }

    if (IsFailed(InteropInit(interopCtx, ctx, &args))){
        LOG_ERR("%s: Error in InteropInit", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    if(IsFailed(InteropProc(interopCtx, &args))){
       goto failed;
    }

    // start pipeline
    if (IsFailed(IPPStart(ctx))) {
        LOG_ERR("%s: Failed to start IPP\n", __func__);
        goto failed;
    }

    // start ExtImgDevice
    ExtImgDevStart(ctx->extImgDevice);

    while (!ctx->quit) {
        LOG_MSG("-");
        ParseRuntimeCommand(ctx);
    }


failed:
    if (ctx) {
        ctx->quit = NVMEDIA_TRUE;

        IPPStop(ctx);

        InteropFini(interopCtx);

        IPPFini(ctx);

        if (ctx->device) {
            NvMediaDeviceDestroy(ctx->device);
        }

        if (ctx->extImgDevice) {
            ExtImgDevDeinit(ctx->extImgDevice);
        }

        free(ctx);
    }
    return 0;
}

