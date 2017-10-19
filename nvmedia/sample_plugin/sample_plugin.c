/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include <string.h>

#include "sample_plugin.h"
#include "log_utils.h"

#define MIN(a, b) ((a < b) ? a : b)
#define CLIP(x, a, b)  (x > b?b:(x < a?a:x))
#define PRINT_ISPSTATS_FORDEBUG

static void
initializeLACSettings0(
    PluginContext *ctx,
    NvMediaIPPStreamType type,
    int width,
    int height)
{
    int numWindowsX, numWindowsY;
    int numPixels, numRemainder;
    float maxLac0Range;
    NvMediaIPPPluginOutputStreamSettings *streamSettings =
        &ctx->runningPluginOutput.streamSettings[type];

    maxLac0Range = (2 ^ 12) / (2 ^ 14);

    streamSettings->lacSettingsValid[0] = NVMEDIA_TRUE;
    if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_4) {
        int i, j, index = 0;
        int ROIOffset_x = 0, ROIOffset_y = 0;

        streamSettings->lacSettings[0].v4.enable = 1;
        streamSettings->lacSettings[0].v4.pixelFormat = NVMEDIA_ISP_PIXELFORMAT_QUAD;
        streamSettings->lacSettings[0].v4.rgbToYGain[0] = 0.29296875;
        streamSettings->lacSettings[0].v4.rgbToYGain[1] = 0.57421875;
        streamSettings->lacSettings[0].v4.rgbToYGain[2] = 0.132812545;
        streamSettings->lacSettings[0].v4.rgbToYGain[3] = 0;
        streamSettings->lacSettings[0].v4.range[0].low = 0.0;
        streamSettings->lacSettings[0].v4.range[0].high = maxLac0Range;
        streamSettings->lacSettings[0].v4.range[1].low = 0.0;
        streamSettings->lacSettings[0].v4.range[1].high = maxLac0Range;
        streamSettings->lacSettings[0].v4.range[2].low = 0.0;
        streamSettings->lacSettings[0].v4.range[2].high = maxLac0Range;
        streamSettings->lacSettings[0].v4.range[3].low = 0.0;
        streamSettings->lacSettings[0].v4.range[3].high = maxLac0Range;
        //Set up 2x2 ROI grid
        for (i = 0; i < 2; i++, ROIOffset_y += height / 2) {
            ROIOffset_x = 0;
            for (j = 0; j < 2; j++, ROIOffset_x += width / 2, index++) {
                streamSettings->lacSettings[0].v4.ROIEnable[index] = NVMEDIA_TRUE;
                numWindowsX = 32;
                numPixels = width / (numWindowsX * 2);
                numRemainder = (width / 2)- (numPixels * numWindowsX);
                streamSettings->lacSettings[0].v4.windows[index].horizontalInterval = numPixels;
                streamSettings->lacSettings[0].v4.windows[index].size.width =
                            numPixels >= 32 ? 32 :
                            numPixels >= 16 ? 16 :
                            numPixels >= 8  ? 8  :
                            numPixels >= 4  ? 4  :
                            numPixels >= 2  ? 2  : 0;
                streamSettings->lacSettings[0].v4.windows[index].startOffset.x =
                    (numRemainder + (numPixels - streamSettings->lacSettings[0].v4.windows[index].size.width)) / 2 + ROIOffset_x;
                numWindowsY = 32;
                numPixels = height / (numWindowsY * 2);
                numRemainder = (height / 2) - (numPixels * numWindowsY);
                streamSettings->lacSettings[0].v4.windows[index].verticalInterval = numPixels;
                streamSettings->lacSettings[0].v4.windows[index].size.height =
                            numPixels >= 32 ? 32 :
                            numPixels >= 16 ? 16 :
                            numPixels >= 8  ? 8  :
                            numPixels >= 4  ? 4  :
                            numPixels >= 2  ? 2  : 0;
                streamSettings->lacSettings[0].v4.windows[index].startOffset.y =
                    (numRemainder + (numPixels - streamSettings->lacSettings[0].v4.windows[index].size.height)) / 2 + ROIOffset_y;
                streamSettings->lacSettings[0].v4.windows[index].horizontalNum = numWindowsX;
                streamSettings->lacSettings[0].v4.windows[index].verticalNum = numWindowsY;
            }
        }
    } else if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_3) {
        streamSettings->lacSettings[0].v3.enable = 1;
        streamSettings->lacSettings[0].v3.pixelFormat = NVMEDIA_ISP_PIXELFORMAT_QUAD;
        streamSettings->lacSettings[0].v3.rgbToYGain[0] = 0.29296875;
        streamSettings->lacSettings[0].v3.rgbToYGain[1] = 0.57421875;
        streamSettings->lacSettings[0].v3.rgbToYGain[2] = 0.132812545;
        streamSettings->lacSettings[0].v3.rgbToYGain[3] = 0;
        streamSettings->lacSettings[0].v3.range[0].low = 0.0;
        streamSettings->lacSettings[0].v3.range[0].high = maxLac0Range;
        streamSettings->lacSettings[0].v3.range[1].low = 0.0;
        streamSettings->lacSettings[0].v3.range[1].high = maxLac0Range;
        streamSettings->lacSettings[0].v3.range[2].low = 0.0;
        streamSettings->lacSettings[0].v3.range[2].high = maxLac0Range;
        streamSettings->lacSettings[0].v3.range[3].low = 0.0;
        streamSettings->lacSettings[0].v3.range[3].high = maxLac0Range;

        numWindowsX = 64;
        numPixels = width / numWindowsX;
        numRemainder = width - (numPixels * numWindowsX);
        streamSettings->lacSettings[0].v3.windows.horizontalInterval = numPixels;
        streamSettings->lacSettings[0].v3.windows.size.width =
                    numPixels >= 32 ? 32 :
                    numPixels >= 16 ? 16 :
                    numPixels >= 8  ? 8  :
                    numPixels >= 4  ? 4  :
                    numPixels >= 2  ? 2  : 0;
        streamSettings->lacSettings[0].v3.windows.startOffset.x =
            (numRemainder + (numPixels - streamSettings->lacSettings[0].v3.windows.size.width)) / 2;

        numWindowsY = 64;
        numPixels = height / numWindowsY;
        numRemainder = height - (numPixels * numWindowsY);
        streamSettings->lacSettings[0].v3.windows.verticalInterval = numPixels;
        streamSettings->lacSettings[0].v3.windows.size.height =
                    numPixels >= 32 ? 32 :
                    numPixels >= 16 ? 16 :
                    numPixels >= 8  ? 8  :
                    numPixels >= 4  ? 4  :
                    numPixels >= 2  ? 2  : 0;
        streamSettings->lacSettings[0].v3.windows.startOffset.y =
            (numRemainder + (numPixels - streamSettings->lacSettings[0].v3.windows.size.height)) / 2;
        streamSettings->lacSettings[0].v3.windows.horizontalNum = numWindowsX;
        streamSettings->lacSettings[0].v3.windows.verticalNum = numWindowsY;
    } else {
        LOG_ERR("%s: isp version not supported\n", __func__, ctx->ispVersion);
    }
}

static void
initializeLACSettings1(
    PluginContext *ctx,
    NvMediaIPPStreamType type,
    int width,
    int height)
{
    int numWindowsX, numWindowsY;
    int numPixels;
    NvMediaIPPPluginOutputStreamSettings *streamSettings =
        &ctx->runningPluginOutput.streamSettings[type];

    streamSettings->lacSettingsValid[1] = NVMEDIA_TRUE;
    numPixels = 32;

    if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_4) {
        int i, j, index = 0;
        int ROIOffset_x = 0, ROIOffset_y = 0;

        streamSettings->lacSettings[1].v4.enable = 1;
        streamSettings->lacSettings[1].v4.pixelFormat = NVMEDIA_ISP_PIXELFORMAT_QUAD;
        streamSettings->lacSettings[1].v4.rgbToYGain[0] = 0.29296875;
        streamSettings->lacSettings[1].v4.rgbToYGain[1] = 0.57421875;
        streamSettings->lacSettings[1].v4.rgbToYGain[2] = 0.132812545;
        streamSettings->lacSettings[1].v4.rgbToYGain[3] = 0;
        streamSettings->lacSettings[1].v4.range[0].low = 0.0;
        streamSettings->lacSettings[1].v4.range[0].high = 1.0;
        streamSettings->lacSettings[1].v4.range[1].low = 0.0;
        streamSettings->lacSettings[1].v4.range[1].high = 1.0;
        streamSettings->lacSettings[1].v4.range[2].low = 0.0;
        streamSettings->lacSettings[1].v4.range[2].high = 1.0;
        streamSettings->lacSettings[1].v4.range[3].low = 0.0;
        streamSettings->lacSettings[1].v4.range[3].high = 1.0;
        //Set up 2x2 ROI grid
        for (i = 0; i  <2; i++, ROIOffset_y += height / 2) {
            ROIOffset_x = 0;
            for (j = 0; j < 2; j++, ROIOffset_x += width / 2, index++) {
                streamSettings->lacSettings[1].v4.ROIEnable[index] = NVMEDIA_TRUE;

                numWindowsX = width / (numPixels * 2);
                numWindowsX = numWindowsX > 32 ? 32 : numWindowsX;

                streamSettings->lacSettings[1].v4.windows[index].horizontalInterval = width / (numWindowsX * 2);
                streamSettings->lacSettings[1].v4.windows[index].size.width = numPixels;
                streamSettings->lacSettings[1].v4.windows[index].startOffset.x =
                    (((width / 2) - streamSettings->lacSettings[1].v4.windows[index].horizontalInterval * numWindowsX) +
                    (streamSettings->lacSettings[1].v4.windows[index].horizontalInterval - numPixels)) / 2 + ROIOffset_x;

                numWindowsY = height / (numPixels * 2);
                numWindowsY = numWindowsY > 32 ? 32 : numWindowsY;

                streamSettings->lacSettings[1].v4.windows[index].verticalInterval = height / (numWindowsY * 2);
                streamSettings->lacSettings[1].v4.windows[index].size.height = numPixels;

                streamSettings->lacSettings[1].v4.windows[index].startOffset.y =
                    (((height / 2) - streamSettings->lacSettings[1].v4.windows[index].verticalInterval * numWindowsY) +
                    (streamSettings->lacSettings[1].v4.windows[index].verticalInterval - numPixels)) / 2 + ROIOffset_y;
                streamSettings->lacSettings[1].v4.windows[index].horizontalNum = numWindowsX;
                streamSettings->lacSettings[1].v4.windows[index].verticalNum = numWindowsY;
            }
        }
    } else if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_3) {
        streamSettings->lacSettings[1].v3.enable = 1;
        streamSettings->lacSettings[1].v3.pixelFormat = NVMEDIA_ISP_PIXELFORMAT_QUAD;
        streamSettings->lacSettings[1].v3.rgbToYGain[0] = 0.29296875;
        streamSettings->lacSettings[1].v3.rgbToYGain[1] = 0.57421875;
        streamSettings->lacSettings[1].v3.rgbToYGain[2] = 0.132812545;
        streamSettings->lacSettings[1].v3.rgbToYGain[3] = 0;
        streamSettings->lacSettings[1].v3.range[0].low = 0.0;
        streamSettings->lacSettings[1].v3.range[0].high = 1.0;
        streamSettings->lacSettings[1].v3.range[1].low = 0.0;
        streamSettings->lacSettings[1].v3.range[1].high = 1.0;
        streamSettings->lacSettings[1].v3.range[2].low = 0.0;
        streamSettings->lacSettings[1].v3.range[2].high = 1.0;
        streamSettings->lacSettings[1].v3.range[3].low = 0.0;
        streamSettings->lacSettings[1].v3.range[3].high = 1.0;

        numWindowsX = width / numPixels;
        numWindowsX = numWindowsX > 64 ? 64 : numWindowsX;
        streamSettings->lacSettings[1].v3.windows.horizontalInterval = width / numWindowsX;
        streamSettings->lacSettings[1].v3.windows.size.width = numPixels;
        streamSettings->lacSettings[1].v3.windows.startOffset.x =
            ((width - streamSettings->lacSettings[1].v3.windows.horizontalInterval * numWindowsX) +
            (streamSettings->lacSettings[1].v3.windows.horizontalInterval - numPixels)) / 2;

        numWindowsY = height / numPixels;
        numWindowsY = numWindowsY > 64 ? 64 : numWindowsY;
        streamSettings->lacSettings[1].v3.windows.verticalInterval = height / numWindowsY;
        streamSettings->lacSettings[1].v3.windows.size.height = numPixels;
        streamSettings->lacSettings[1].v3.windows.startOffset.y =
            ((height - streamSettings->lacSettings[1].v3.windows.verticalInterval * numWindowsY) +
            (streamSettings->lacSettings[1].v3.windows.verticalInterval - numPixels)) / 2;

        streamSettings->lacSettings[1].v3.windows.horizontalNum = numWindowsX;
        streamSettings->lacSettings[1].v3.windows.verticalNum = numWindowsY;
    } else {
        LOG_ERR("%s: isp version not supported\n", __func__, ctx->ispVersion);
    }
}

static void initializeFlickerbandSettings(
    PluginContext *ctx,
    NvMediaIPPStreamType type,
    int width,
    int height)
{
    int windowHeight = 64;
    int numBands = height / windowHeight;
    NvMediaIPPPluginOutputStreamSettings *streamSettings =
        &ctx->runningPluginOutput.streamSettings[type];

    // For low resolution shrink the band heights to have higher precision.
    while (numBands < 64 && windowHeight > 1) {
        windowHeight /= 2;
        numBands = height / windowHeight;
    }

    // Max 256 bands
    if (numBands > 256) numBands = 256;

    streamSettings->flickerBandSettingsValid = NVMEDIA_TRUE;

    if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_4) {
        streamSettings->flickerBandSettings.v4.enable = NVMEDIA_TRUE;
        streamSettings->flickerBandSettings.v4.windows.size.width = width;
        streamSettings->flickerBandSettings.v4.windows.size.height = windowHeight;
        streamSettings->flickerBandSettings.v4.windows.horizontalNum = 1;
        streamSettings->flickerBandSettings.v4.windows.verticalNum = numBands;
        streamSettings->flickerBandSettings.v4.windows.horizontalInterval = 0;
        streamSettings->flickerBandSettings.v4.windows.verticalInterval = height / numBands;
        streamSettings->flickerBandSettings.v4.windows.startOffset.x = 0;
        streamSettings->flickerBandSettings.v4.windows.startOffset.y =
            (height - streamSettings->flickerBandSettings.v4.windows.verticalInterval * numBands) / 2;
        streamSettings->flickerBandSettingsValid = NVMEDIA_FALSE;
        streamSettings->flickerBandSettings.v4.windows.verticalInterval = windowHeight;
        streamSettings->flickerBandSettings.v4.colorChannel = NVMEDIA_ISP_COLORCHANNEL_TL_R_V;//LUMINANCE;
        streamSettings->flickerBandSettings.v4.hdrMode = NVMEDIA_ISP_HDR_MODE_NORMAL; //Not HDR mode
    } else if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_3) {
        streamSettings->flickerBandSettings.v3.enable = NVMEDIA_TRUE;
        streamSettings->flickerBandSettings.v3.windows.size.width = width;
        streamSettings->flickerBandSettings.v3.windows.size.height = windowHeight;
        streamSettings->flickerBandSettings.v3.windows.horizontalNum = 1;
        streamSettings->flickerBandSettings.v3.windows.verticalNum = numBands;
        streamSettings->flickerBandSettings.v3.windows.horizontalInterval = 0;
        streamSettings->flickerBandSettings.v3.windows.verticalInterval = height / numBands;
        streamSettings->flickerBandSettings.v3.windows.startOffset.x = 0;
        streamSettings->flickerBandSettings.v3.windows.startOffset.y =
            (height - streamSettings->flickerBandSettings.v3.windows.verticalInterval * numBands) / 2;
    } else {
        LOG_ERR("%s: isp version not supported\n", __func__, ctx->ispVersion);
    }
}

static void
initializeHistogramsettings0(
    PluginContext *ctx,
    NvMediaIPPStreamType type,
    int width,
    int height)
{
    NvMediaIPPPluginOutputStreamSettings *streamSettings =
        &ctx->runningPluginOutput.streamSettings[type];

    streamSettings->histogramSettingsValid[0] = NVMEDIA_TRUE;

    if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_4) {
        int i;
        streamSettings->histogramSettings[0].v4.enable = NVMEDIA_TRUE;
        streamSettings->histogramSettings[0].v4.pixelFormat = NVMEDIA_ISP_PIXELFORMAT_QUAD;
        streamSettings->histogramSettings[0].v4.window.x0 = 0;
        streamSettings->histogramSettings[0].v4.window.y0 = 0;
        streamSettings->histogramSettings[0].v4.window.x1 = width;
        streamSettings->histogramSettings[0].v4.window.y1 = height;

        streamSettings->histogramSettings[0].v4.hdrMode = NVMEDIA_ISP_HDR_MODE_NORMAL; //Not HDR mode
        for (i = 0; i < NVMEDIA_ISP_HIST_RANGE_CFG_NUM; i++) {
            streamSettings->histogramSettings[0].v4.range[i] = (i < 2) ? 6 : 7 + (i-2);
            streamSettings->histogramSettings[0].v4.knee[i]  = (i + 1) * 32 - 1;
        }
    } else if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_3) {
        streamSettings->histogramSettings[0].v3.enable = NVMEDIA_TRUE;
        streamSettings->histogramSettings[0].v3.pixelFormat = NVMEDIA_ISP_PIXELFORMAT_QUAD;
        streamSettings->histogramSettings[0].v3.window.x0 = 0;
        streamSettings->histogramSettings[0].v3.window.y0 = 0;
        streamSettings->histogramSettings[0].v3.window.x1 = width;
        streamSettings->histogramSettings[0].v3.window.y1 = height;

        streamSettings->histogramSettings[0].v3.range.low = 0;
        streamSettings->histogramSettings[0].v3.range.high = 8192;
    } else {
        LOG_ERR("%s: isp version not supported\n", __func__, ctx->ispVersion);
    }
}

static void
initializeHistogramsettings1(
    PluginContext *ctx,
    NvMediaIPPStreamType type,
    int width,
    int height)
{
    NvMediaIPPPluginOutputStreamSettings *streamSettings =
        &ctx->runningPluginOutput.streamSettings[type];

    streamSettings->histogramSettingsValid[1] = NVMEDIA_TRUE;

    if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_4) {
        int i;
        streamSettings->histogramSettings[1].v4.enable = NVMEDIA_TRUE;
        streamSettings->histogramSettings[1].v4.pixelFormat = NVMEDIA_ISP_PIXELFORMAT_RGB;
        streamSettings->histogramSettings[1].v4.window.x0 = 0;
        streamSettings->histogramSettings[1].v4.window.y0 = 0;
        streamSettings->histogramSettings[1].v4.window.x1 = width;
        streamSettings->histogramSettings[1].v4.window.y1 = height;

        streamSettings->histogramSettings[1].v4.hdrMode = NVMEDIA_ISP_HDR_MODE_NORMAL; //Not HDR mode
        for (i = 0; i < NVMEDIA_ISP_HIST_RANGE_CFG_NUM; i++) {
            streamSettings->histogramSettings[1].v4.range[i] = (i < 2) ? 6 : 7 + (i-2);
            streamSettings->histogramSettings[1].v4.knee[i]  = (i + 1) * 32 - 1;
        }
    } else if (ctx->ispVersion == NVMEDIA_IPP_ISP_VERSION_3) {
        streamSettings->histogramSettings[1].v3.enable = NVMEDIA_TRUE;
        streamSettings->histogramSettings[1].v3.pixelFormat = NVMEDIA_ISP_PIXELFORMAT_RGB;
        streamSettings->histogramSettings[1].v3.window.x0 = 0;
        streamSettings->histogramSettings[1].v3.window.y0 = 0;
        streamSettings->histogramSettings[1].v3.window.x1 = width;
        streamSettings->histogramSettings[1].v3.window.y1 = height;

        streamSettings->histogramSettings[1].v3.range.low = 0;
        streamSettings->histogramSettings[1].v3.range.high = 8192;
    } else {
        LOG_ERR("%s: isp version not supported\n", __func__, ctx->ispVersion);
    }
}

NvMediaStatus
NvSampleACPCreate(
    NvMediaIPPComponent *controlAlgorithmHandle,
    NvMediaIPPPluginSupportFuncs *supportFunctions,
    NvMediaIPPPropertyStatic *staticProperties,
    void *clientContext,
    NvMediaIPPPlugin **pluginHandle,
    NvMediaIPPISPVersion ispVersion)
{
    PluginContext *ctx;
    NvMediaIPPExposureControl *aeControl;
    NvMediaIPPWBGainControl *awbGainControl;
    NvMediaIPPPluginOutput *runPluginOutput;
    float maxET[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    float minGain[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    unsigned int width, height;
    unsigned int type;
    unsigned int i, j;
    NvMediaStatus status;

    if (!staticProperties || !pluginHandle) {
        LOG_ERR("%s: Invalid arguemnt", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    ctx = calloc(1, sizeof(PluginContext));
    if (!ctx) {
        LOG_ERR("%s: Out of memory!\n", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    ctx->controlAlgorithmHandle = controlAlgorithmHandle;
    ctx->ispVersion = ispVersion;

    if (supportFunctions) {
        memcpy(&ctx->supportFunctions,
               supportFunctions,
               sizeof(NvMediaIPPPluginSupportFuncs));
    }

    width  = staticProperties->sensorMode.activeArraySize.width;
    height = staticProperties->sensorMode.activeArraySize.height;

    for (type = 0; type < NVMEDIA_IPP_STREAM_MAX_TYPES; type++) {
        /*
         * There is a HW limitation that all the stats calcultion
         * can't include bottom 2 lines of the image, it will cause
         * stats syncpt lost sometime. So when setup LAC/Hist/FB or
         * any stats windows, need to avoid the bottom 2 lines.
         */
        initializeLACSettings0(ctx, type, width, height - 2);
        initializeLACSettings1(ctx, type, width, height - 2);
        initializeFlickerbandSettings(ctx, type, width, height - 2);
        initializeHistogramsettings0(ctx, type, width, height - 2);
        initializeHistogramsettings1(ctx, type, width, height - 2);
    }

    runPluginOutput = &ctx->runningPluginOutput;

    runPluginOutput->aeState = NVMEDIA_IPP_AE_STATE_INACTIVE;
    runPluginOutput->aeLock = NVMEDIA_FALSE;

    runPluginOutput->colorCorrectionMatrixValid = NVMEDIA_TRUE;
    runPluginOutput->colorCorrectionMatrix.array[0][0] = 1.72331000;
    runPluginOutput->colorCorrectionMatrix.array[0][1] = -0.15490000;
    runPluginOutput->colorCorrectionMatrix.array[0][2] = 0.04468000;
    runPluginOutput->colorCorrectionMatrix.array[0][3] = 0.0;

    runPluginOutput->colorCorrectionMatrix.array[1][0] = -0.64099000;
    runPluginOutput->colorCorrectionMatrix.array[1][1] = 1.46603000;
    runPluginOutput->colorCorrectionMatrix.array[1][2] = -0.78100000;
    runPluginOutput->colorCorrectionMatrix.array[1][3] = 0.0;

    runPluginOutput->colorCorrectionMatrix.array[2][0] = -0.08232000;
    runPluginOutput->colorCorrectionMatrix.array[2][1] = -0.31113000;
    runPluginOutput->colorCorrectionMatrix.array[2][2] = 1.73632000;
    runPluginOutput->colorCorrectionMatrix.array[2][3] = 0.0;

    runPluginOutput->colorCorrectionMatrix.array[3][0] = 0;
    runPluginOutput->colorCorrectionMatrix.array[3][1] = 0;
    runPluginOutput->colorCorrectionMatrix.array[3][2] = 0;
    runPluginOutput->colorCorrectionMatrix.array[3][3] = 1.0;

    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        maxET[i] = staticProperties->exposureTimeRange[i].high / 1e9; // convert to seconds
        minGain[i] = staticProperties->sensorAnalogGainRange[i].low;
    }

    /* Override the above maxET & minGain if the get sensor attribute
     * function pointer is given and the attributes are supported */
    if (ctx->supportFunctions.getSensorAttribute) {
        status = ctx->supportFunctions.getSensorAttribute(
                        ctx->controlAlgorithmHandle,
                        NVMEDIA_ISC_SENSOR_ATTR_ET_MAX,
                        sizeof(maxET), maxET);
        if (status != NVMEDIA_STATUS_OK) {
            if (status != NVMEDIA_STATUS_NOT_SUPPORTED) {
                LOG_ERR("%s: Failed to get max exp time sensor attribute\n", __func__);
            }
        }

        status = ctx->supportFunctions.getSensorAttribute(
                        ctx->controlAlgorithmHandle,
                        NVMEDIA_ISC_SENSOR_ATTR_GAIN_MIN,
                        sizeof(minGain), minGain);
        if (status != NVMEDIA_STATUS_OK) {
            if (status != NVMEDIA_STATUS_NOT_SUPPORTED) {
                LOG_ERR("%s: Failed to get min gain sensor attribute\n", __func__);
            }
        }
    }

    aeControl = &runPluginOutput->exposureControl;
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        aeControl->sensorGain[i].valid = NVMEDIA_TRUE;
        aeControl->sensorGain[i].value = minGain[i];

        aeControl->exposureTime[i].valid = NVMEDIA_TRUE;
        aeControl->exposureTime[i].value = maxET[i];
    }

    awbGainControl = &runPluginOutput->whiteBalanceGainControl;
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        awbGainControl->wbGain[i].valid = NVMEDIA_TRUE;
        for (j = 0; j < 4; j++) {
            awbGainControl->wbGain[i].value[j] = 1.0f;
         }
    }

    *pluginHandle = ctx;
    return NVMEDIA_STATUS_OK;
}

void
NvSampleACPDestroy(
    NvMediaIPPPlugin *pluginHandle)
{
    if (!pluginHandle) return;

    free(pluginHandle);
}

static void
IPPPluginSimpleAWB(NvMediaIPPPlugin *pluginHandle,
    NvMediaIPPPluginInput *pluginInput)
{
    PluginContext *ctx = (PluginContext *) pluginHandle;
    NvMediaIPPWBGainControl *awbGainControl =
        &ctx->runningPluginOutput.whiteBalanceGainControl;

    NvMediaIPPPropertyStatic* staticProperties = pluginInput->staticProperties;

    unsigned int i, j, cnt, numpixels = 0;
    float Gavg, Ravg, Bavg;
    float longfraction;
    float normalization, invgains[4];
    NvMediaIPPPluginInputStreamData *streamData;

    streamData = &pluginInput->streamData[0];

    /* ISP Lac measurement is programmed 0 to longfraction for AWB,
     * and lac avergaes are 14-bit integers thus the lac averages
     * are normalized to be in range 0 to 1.0 */
    longfraction = (2 ^ 12) / (2 ^ 14);
    normalization = 1.0f / (longfraction * (1 << 14));

    // Compensate for the AWB gains if the wb gains are applied in sensor
    if (!staticProperties->wbGainsAppliedInISP &&
        pluginInput->whiteBalanceGainControl.wbGain[0].valid) {
        invgains[0] = normalization / pluginInput->whiteBalanceGainControl.wbGain[0].value[0];
        invgains[1] = normalization / pluginInput->whiteBalanceGainControl.wbGain[0].value[1];
        invgains[2] = normalization / pluginInput->whiteBalanceGainControl.wbGain[0].value[2];
        invgains[3] = normalization / pluginInput->whiteBalanceGainControl.wbGain[0].value[3];
    } else {
        invgains[0] = normalization;
        invgains[1] = normalization;
        invgains[2] = normalization;
        invgains[3] = normalization;
    }

    Gavg = 0;
    Ravg = 0;
    Bavg = 0;
    cnt  = 0;

    switch (ctx->ispVersion) {
        case NVMEDIA_IPP_ISP_VERSION_4:
            {
                NvMediaISPStatsLacMeasurementV4 *pIspLacStats;
                float *RAvgStats, *GAvgStats1, *GAvgStats2, *BAvgStats;
                int i;

                numpixels = 0;
                pIspLacStats = streamData->lacStats[0].v4;
                if (!pIspLacStats)
                    return;

                for (i = 0; i < 4; i++) {
                    RAvgStats = pIspLacStats->average[i][0];
                    GAvgStats1 = pIspLacStats->average[i][1];
                    GAvgStats2 = pIspLacStats->average[i][2];
                    BAvgStats = pIspLacStats->average[i][3];
                    numpixels += pIspLacStats->numWindows[i];
                    for (j = 0; j < pIspLacStats->numWindows[i]; j++) {
                        if (RAvgStats[j] > 0.0 &&
                           GAvgStats1[j] > 0.0 &&
                           GAvgStats2[j] > 0.0 &&
                           BAvgStats[j] > 0.0) {
                            Ravg += RAvgStats[j] * invgains[0];
                            Gavg += (GAvgStats1[j] * invgains[1] + GAvgStats2[j] * invgains[2]);
                            Bavg += BAvgStats[j] * invgains[3];
                            cnt++;
                        }
                    }
                }
            }
            break;
        case NVMEDIA_IPP_ISP_VERSION_3:
        default:
            {
                NvMediaISPStatsLacMeasurement *pIspLacStats;
                int *RAvgStats, *GAvgStats1, *GAvgStats2, *BAvgStats;

                pIspLacStats = streamData->lacStats[0].v3;
                if (!pIspLacStats)
                    return;

                RAvgStats = pIspLacStats->average[0];
                GAvgStats1 = pIspLacStats->average[1];
                GAvgStats2 = pIspLacStats->average[2];
                BAvgStats = pIspLacStats->average[3];
                numpixels = pIspLacStats->numWindowsH * pIspLacStats->numWindowsV ;

                for (j = 0; j < numpixels; j++) {
                    if (RAvgStats[j] > 0.0 &&
                       GAvgStats1[j] > 0.0 &&
                       GAvgStats2[j] > 0.0 &&
                       BAvgStats[j] > 0.0) {
                        Ravg += RAvgStats[j] * invgains[0];
                        Gavg += (GAvgStats1[j] * invgains[1] + GAvgStats2[j] * invgains[2]);
                        Bavg += BAvgStats[j] * invgains[3];
                        cnt++;
                    }
                }
            }
            break;
    }

    if ((cnt != 0) && (Gavg != 0) && (Bavg != 0) && (Ravg != 0)) {
        float bgain, rgain, ggain, min;
        float prevBgain, prevRgain, prevGgain;

        Gavg = Gavg / (2 * cnt);
        Ravg = Ravg / cnt;
        Bavg = Bavg / cnt;

        ggain = 1.0f;
        bgain = ((float) Gavg) / Bavg;
        rgain = ((float) Gavg) / Ravg;

        /* Make sure gains are not less than 1.0 */
        min = MIN(MIN(rgain, ggain), bgain);

        rgain = rgain / min;
        ggain = ggain / min;
        bgain = bgain / min;

        if (bgain > 8.0) bgain = 8.0f;
        if (rgain > 8.0) rgain = 8.0f;
        if (ggain > 8.0) ggain = 8.0f;

        prevRgain = awbGainControl->wbGain[0].value[0];
        prevGgain = (awbGainControl->wbGain[0].value[1] + awbGainControl->wbGain[0].value[2]) / 2;
        prevBgain = awbGainControl->wbGain[0].value[3];

        if ((bgain / prevBgain < 1.02 && bgain / prevBgain > 0.98) &&
            (ggain / prevGgain < 1.02 && ggain / prevGgain > 0.98) &&
            (rgain / prevRgain < 1.02 && rgain / prevRgain > 0.98)) {
            ctx->runningPluginOutput.awbState = NVMEDIA_IPP_AWB_STATE_CONVERGED;
            LOG_DBG("AWB CONVERGED: AWB Plugin achieved to convergence state\n");
            return;
        } else {
            ctx->runningPluginOutput.awbState = NVMEDIA_IPP_AWB_STATE_SEARCHING;
        }

        for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            awbGainControl->wbGain[i].valid = NVMEDIA_TRUE;
            awbGainControl->wbGain[i].value[0] = rgain;
            awbGainControl->wbGain[i].value[1] = ggain;
            awbGainControl->wbGain[i].value[2] = ggain;
            awbGainControl->wbGain[i].value[3] = bgain;
        }

        LOG_DBG("Computed AWB Gains: [R G B] = [%.3f, %.3f, %.3f] Used Pixels :%.2f\n",
             Ravg, Gavg, Bavg, (cnt * 100.0f) / numpixels);
    }
}

static void
IPPPluginSimpleAutoExposure(
    NvMediaIPPPlugin *pluginHandle,
    NvMediaIPPPluginInput *pluginInput)
{
    PluginContext *ctx = (PluginContext *) pluginHandle;
    NvMediaIPPPropertyStatic* staticProperties = pluginInput->staticProperties;
    NvMediaIPPExposureControl *aeControl = &ctx->runningPluginOutput.exposureControl;
    unsigned int i, j;
    float CurrentLuma = 0.0f;
    float maxPixVal = 16383.0f; //14bit ISP output
    //TODO: should be parsed from control properties or set based on precision
    float targetLuma = 128.0f / 1024;
    float inputExptime, inputExpgain, prevExptime, prevExpgain, prevExpVal, inputExpVal;
    float curExptime[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    float curExpgain[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    float targetExpVal[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    float dampingFactor, factAdjust;
    float channelGainRatio;
    NvMediaIPPPluginInputStreamData *streamData;
    NvMediaStatus status;

    streamData = &pluginInput->streamData[0];

    aeControl->digitalGain = 1.0f;
    aeControl->hdrRatio = 64;

    {
        float minET[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
        float maxET[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
        float minGain[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
        float maxGain[NVMEDIA_ISC_EXPOSURE_MODE_MAX];

        for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            minET[i] = staticProperties->exposureTimeRange[i].low / 1e9;  // convert to seconds
            maxET[i] = staticProperties->exposureTimeRange[i].high / 1e9; // convert to seconds
            minGain[i] = staticProperties->sensorAnalogGainRange[i].low;
            maxGain[i] = staticProperties->sensorAnalogGainRange[i].high;
        }

        channelGainRatio = staticProperties->channelGainRatio;

        /* Override the above values if the get sensor attribute function
         * pointer is given and the attributes are supported */
        if (ctx->supportFunctions.getSensorAttribute) {
            status = ctx->supportFunctions.getSensorAttribute(
                            ctx->controlAlgorithmHandle,
                            NVMEDIA_ISC_SENSOR_ATTR_ET_MIN,
                            sizeof(minET), minET);
            if (status != NVMEDIA_STATUS_OK) {
                if (status != NVMEDIA_STATUS_NOT_SUPPORTED) {
                    LOG_ERR("%s: Failed to get min exp time sensor attribute\n", __func__);
                }
            }

            status = ctx->supportFunctions.getSensorAttribute(
                            ctx->controlAlgorithmHandle,
                            NVMEDIA_ISC_SENSOR_ATTR_ET_MAX,
                            sizeof(maxET), maxET);
            if (status != NVMEDIA_STATUS_OK) {
                if (status != NVMEDIA_STATUS_NOT_SUPPORTED) {
                    LOG_ERR("%s: Failed to get max exp time sensor attribute\n", __func__);
                }
            }

            status = ctx->supportFunctions.getSensorAttribute(
                            ctx->controlAlgorithmHandle,
                            NVMEDIA_ISC_SENSOR_ATTR_GAIN_MIN,
                            sizeof(minGain), minGain);
            if (status != NVMEDIA_STATUS_OK) {
                if (status != NVMEDIA_STATUS_NOT_SUPPORTED) {
                    LOG_ERR("%s: Failed to get min gain sensor attribute\n", __func__);
                }
            }

            status = ctx->supportFunctions.getSensorAttribute(
                            ctx->controlAlgorithmHandle,
                            NVMEDIA_ISC_SENSOR_ATTR_GAIN_MAX,
                            sizeof(maxGain), maxGain);
            if (status != NVMEDIA_STATUS_OK) {
                if (status != NVMEDIA_STATUS_NOT_SUPPORTED) {
                    LOG_ERR("%s: Failed to get max gain sensor attribute\n", __func__);
                }
            }

            status = ctx->supportFunctions.getSensorAttribute(
                            ctx->controlAlgorithmHandle,
                            NVMEDIA_ISC_SENSOR_ATTR_GAIN_FACTOR,
                            sizeof(float), &channelGainRatio);
            if (status != NVMEDIA_STATUS_OK) {
                if (status != NVMEDIA_STATUS_NOT_SUPPORTED) {
                    LOG_ERR("%s: Failed to get min exp time sensor attribute\n", __func__);
                }
            }
        }

        float Gavg = 0;
        float Ravg = 0;
        float Bavg = 0;
        unsigned int numpixels = 0;
        float longfraction = (2 ^ 12) / (2 ^ 14);

        // Estimate Luminence
        switch (ctx->ispVersion) {
            case NVMEDIA_IPP_ISP_VERSION_4:
                {
                    NvMediaISPStatsLacMeasurementV4 *pIspLacStats;
                    float *RAvgStats, *GAvgStats1, *GAvgStats2, *BAvgStats;
                    int i;

                    pIspLacStats = streamData->lacStats[1].v4;
                    if (!pIspLacStats)
                       return;

                    numpixels = 0;
                    for (i = 0; i < 4; i++) {
                        RAvgStats = pIspLacStats->average[i][0];
                        GAvgStats1 = pIspLacStats->average[i][1];
                        GAvgStats2 = pIspLacStats->average[i][2];
                        BAvgStats = pIspLacStats->average[i][3];
                        numpixels += pIspLacStats->numWindows[i];
                        for (j = 0; j < pIspLacStats->numWindows[i]; j++) {
                            Gavg += CLIP(GAvgStats1[j] / (maxPixVal * longfraction), 0, 1);
                            Gavg += CLIP(GAvgStats2[j] / (maxPixVal * longfraction), 0, 1);
                            Ravg += CLIP(RAvgStats[j]  / (maxPixVal * longfraction), 0, 1);
                            Bavg += CLIP(BAvgStats[j]  / (maxPixVal * longfraction), 0, 1);
                        }
                    }
                }
                break;
            case NVMEDIA_IPP_ISP_VERSION_3:
            default:
                {
                    NvMediaISPStatsLacMeasurement *pIspLacStats;
                    int  *RAvgStats, *GAvgStats1, *GAvgStats2, *BAvgStats;

                    pIspLacStats = streamData->lacStats[1].v3;
                    if (!pIspLacStats)
                       return;

                    RAvgStats = pIspLacStats->average[0];
                    GAvgStats1 = pIspLacStats->average[1];
                    GAvgStats2 = pIspLacStats->average[2];
                    BAvgStats = pIspLacStats->average[3];
                    numpixels = pIspLacStats->numWindowsH * pIspLacStats->numWindowsV;
                    for (j = 0; j < numpixels; j++) {
                        Gavg += CLIP(GAvgStats1[j] / (maxPixVal * longfraction), 0, 1);
                        Gavg += CLIP(GAvgStats2[j] / (maxPixVal * longfraction), 0, 1);
                        Ravg += CLIP(RAvgStats[j]  / (maxPixVal * longfraction), 0, 1);
                        Bavg += CLIP(BAvgStats[j]  / (maxPixVal * longfraction), 0, 1);
                    }
                }
                break;
        }

        Gavg = Gavg / (2 * numpixels);
        Ravg = Ravg / numpixels;
        Bavg = Bavg / numpixels;

        CurrentLuma = (Gavg + Ravg + Bavg) / 3.0f;

        if ((CurrentLuma - targetLuma) < 0.001f && (CurrentLuma - targetLuma) > -0.001f) {
            ctx->runningPluginOutput.aeState = NVMEDIA_IPP_AE_STATE_CONVERGED;
            LOG_DBG("Current Luma and Target Luma %.6f, %.6f, %.6f\n", CurrentLuma, targetLuma, CurrentLuma - targetLuma);
            LOG_DBG("AE CONVERGED: AE Plugin achieved to convergence State\n");
            return;
        } else {
            ctx->runningPluginOutput.aeState = NVMEDIA_IPP_AE_STATE_SEARCHING;
        }

        inputExptime = pluginInput->exposureControl.exposureTime[0].value;
        inputExpgain = pluginInput->exposureControl.sensorGain[0].value;

        prevExptime = aeControl->exposureTime[0].value;
        prevExpgain = aeControl->sensorGain[0].value;

        prevExpVal = prevExptime * prevExpgain;
        inputExpVal = inputExptime * inputExpgain;

        factAdjust = targetLuma / CurrentLuma;

        // Damping
        dampingFactor = 0.60;
        targetExpVal[0] = dampingFactor * factAdjust * inputExpVal + (1 - dampingFactor) * prevExpVal ;
        targetExpVal[1] = targetExpVal[0] / 8.0; // Using hardcoded hdrRatio of 8
        targetExpVal[2] = targetExpVal[1] / 8.0; // Using hardcoded hdrRatio of 8.

        if (channelGainRatio == 0) {
            for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                curExptime[i] = maxET[i];
                curExpgain[i] = targetExpVal[i] / curExptime[i];
                if (curExpgain[i] < minGain[i]) {
                     curExpgain[i] = minGain[i];
                     curExptime[i] = targetExpVal[i] / curExpgain[i];
                     if (curExptime[i] < minET[i])
                         curExptime[i] = minET[i];
                } else if (curExpgain[i] > maxGain[i]) {
                    curExpgain[i] = maxGain[i];
                }
            }
        } else {
            curExptime[0] = maxET[0];
            curExpgain[0] = targetExpVal[0] / curExptime[0];
            if (curExpgain[0] < minGain[0]) {
                 curExpgain[0] = minGain[0];
                 curExptime[0] = targetExpVal[0] / curExpgain[0];
                 if (curExptime[0] < minET[0])
                     curExptime[0] = minET[0];
            } else if (curExpgain[0] > maxGain[0]) {
                curExpgain[0] = maxGain[0];
            }

            for (i = 1; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                curExptime[i] = maxET[i];
                curExpgain[i] = curExpgain[i - 1] / channelGainRatio; // Minimum possible
                if ((curExpgain[i] * curExptime[i]) > targetExpVal[i]) {
                    // Reduce Exp time, as  min gain is still higher
                    curExptime[i] = targetExpVal[i] / curExpgain[i];
                    if (curExptime[i] < minET[i])
                        curExptime[i] = minET[i];
                } else {
                    curExpgain[i] = curExpgain[i - 1];
                    if ((curExpgain[i] * curExptime[i]) > targetExpVal[i]) {
                        curExptime[i] = targetExpVal[i] / curExpgain[i];
                        if (curExptime[i] < minET[i])
                            curExptime[i] = minET[i];
                    } else {
                        curExpgain[i] = curExpgain[i - 1] * channelGainRatio; // Maximum possible
                        curExptime[i] = targetExpVal[i] / curExpgain[i];
                        if (curExptime[i] < minET[i])
                           curExptime[i] = minET[i];
                    }
                }

                if (curExpgain[i] < minGain[i]) {
                    curExpgain[i] = minGain[i];
                    curExptime[i] = targetExpVal[i] / curExpgain[i];
                    if (curExptime[i] > maxET[i])
                        curExptime[i] = maxET[i];
                }
            }
        }

        for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            aeControl->exposureTime[i].valid = NVMEDIA_TRUE;
            aeControl->exposureTime[i].value = curExptime[i];

            aeControl->sensorGain[i].valid = NVMEDIA_TRUE;
            aeControl->sensorGain[i].value = curExpgain[i];

            LOG_DBG("Estimated ET[%d]: %.6f Gain[%d]: %.6f\n",
                i, curExptime[i],
                i, curExpgain[i]);
        }
    }
}

NvMediaStatus
NvSampleACPProcess(
    NvMediaIPPPlugin *pluginHandle,
    NvMediaIPPPluginInput *pluginInput,
    NvMediaIPPPluginOutput *pluginOutput)
{
    PluginContext *ctx = (PluginContext *) pluginHandle;
    NvMediaIPPExposureControl *aeControl = &ctx->runningPluginOutput.exposureControl;
    NvMediaIPPWBGainControl *awbGainControl = &ctx->runningPluginOutput.whiteBalanceGainControl;
    unsigned int i;

    if (pluginInput) {
        if (pluginInput->controlsProperties->aeLock == NVMEDIA_TRUE) {
            // Invalidate the settings, so that they are not changed.
            for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                aeControl->sensorGain[i].valid = NVMEDIA_FALSE;
                aeControl->exposureTime[i].valid = NVMEDIA_FALSE;
            }
        } else if (pluginInput->controlsProperties->aeMode == NVMEDIA_IPP_AE_MODE_OFF) {
            // Manual mode, copy from control properties
            memcpy(aeControl, &pluginInput->controlsProperties->exposureControl,
                   sizeof(NvMediaIPPExposureControl));
        } else {
            IPPPluginSimpleAutoExposure(pluginHandle, pluginInput);
        }

        if (pluginInput->controlsProperties->awbLock == NVMEDIA_TRUE) {
            // Invalidate the settings, so that they are not changed.
            for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                awbGainControl->wbGain[i].valid = NVMEDIA_FALSE;
            }
        } else if (pluginInput->controlsProperties->awbMode == NVMEDIA_IPP_AWB_MODE_OFF) {
            // Manual mode, copy from control properties
            memcpy(awbGainControl, &pluginInput->controlsProperties->wbGains,
                   sizeof(NvMediaIPPWBGainControl));
        } else {
            IPPPluginSimpleAWB(pluginHandle, pluginInput);
        }

        ctx->runningPluginOutput.aeLock = pluginInput->controlsProperties->aeLock;
        ctx->runningPluginOutput.awbLock = pluginInput->controlsProperties->awbLock;
    }

    // Copy running plugin outputs into plugin outputs to apply
    memcpy(pluginOutput, &ctx->runningPluginOutput, sizeof(NvMediaIPPPluginOutput));

    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        LOG_DBG("ET[%d]: %f Gain[%d]: %f\n",
            i, aeControl->exposureTime[i].value,
            i, aeControl->sensorGain[i].value);
    }

    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        LOG_DBG("WB gains[%d]: {%f, %f, %f, %f}\n", i,
            awbGainControl->wbGain[i].value[0],
            awbGainControl->wbGain[i].value[1],
            awbGainControl->wbGain[i].value[2],
            awbGainControl->wbGain[i].value[3]);
    }

    return NVMEDIA_STATUS_OK;
}
