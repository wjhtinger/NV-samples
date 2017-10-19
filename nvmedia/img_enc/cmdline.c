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
#include <sys/stat.h>
#include "cmdline.h"
#include "config_parser.h"
#include "log_utils.h"

SectionMap sectionsMap[] = {
    {SECTION_QP,              "QP_Params",            0, sizeof(NvMediaEncodeQP)},
    {SECTION_RC,              "RC_Params",            0, sizeof(EncodeRCParams)},
    {SECTION_ENCODE_PIC,      "EncodePic_Params",     0, sizeof(EncodePicParams)},
    {SECTION_ENCODE_PIC_H264, "EncodePicH264_Params", 0, sizeof(EncodePicParamsH264)},
    {SECTION_PAYLOAD,         "Payload",              0, sizeof(EncodeH264SEIPayload)},
    {SECTION_ENCODE_PIC_H265, "EncodePicH265_Params", 0, sizeof(EncodePicParamsH265)},
    {SECTION_PAYLOAD,         "PayloadH265",          0, sizeof(EncodeH265SEIPayload)},
    {SECTION_NONE,            "",                     0, 0} // Has to be the last item - specifies end of array
};

void PrintUsage()
{
    LOG_MSG("Usage: nvmimg_enc [options]\n");
    LOG_MSG("Options:\n");
    LOG_MSG("-h                         Prints usage\n");
    LOG_MSG("-cf  [base config]         Encoder configuration file (basic)\n");
    LOG_MSG("-sf  [specific config]     Encoder configuration file (specific)\n");
    LOG_MSG("                           Parameters in this file will overrite parameters specified in the base config file\n");
    LOG_MSG("-p   [p1=v1]..[pN=vN]      Set parameter <p1> with value <v1>...<pN> with value <vN>\n");
    LOG_MSG("                           Overrides any parameter set through configuration files\n");
    LOG_MSG("-blackbox [num_second]     Blackbox mode with num_second recording time\n");
    LOG_MSG("-crc [gen/chk][crcs.txt]   Check (chk) or generate (gen) CRC values\n");
    LOG_MSG("-v   [level]:              Logging Level = 0(Errors), 1(Warnings), 2(Info), 3(Debug)\n");
}

int ParseArgs(int argc, char **argv, TestArgs *args)
{
    char *configFileContent;
    int paramsCounter, contentLen, paramsNum;
    char *filename = NULL;
    NvMediaEncodeConfigH264VUIParams *h264VUIParameters = args->configH264Params.h264VUIParameters;
    NvMediaEncodeConfigH265VUIParams *h265VUIParameters = args->configH265Params.h265VUIParameters;

    // Map Syntax: {ParamNameInFile, &args->VariableName, ParamType, InitialValue, LimitType, MinLimit, MaxLimit, CharSize, pointer to CharSize, section}
    ConfigParamsMap paramsMap[] = {
        {"InputFile",                          &args->infile,                     TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, FILE_NAME_SIZE, 0, SECTION_NONE},
        {"InputFileFormat",                    &args->inputFileFormat,            TYPE_UINT,      0, LIMITS_BOTH, 0, 3, 0, 0, SECTION_NONE},
        {"OutputFile",                         &args->outfile,                    TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, FILE_NAME_SIZE, 0, SECTION_NONE},
        {"StartFrame",                         &args->startFrame,                 TYPE_UINT,      1, LIMITS_MIN,  1, 0, 0, 0, SECTION_NONE},
        {"FramesToBeEncoded",                  &args->framesToBeEncoded,          TYPE_UINT,      0, LIMITS_MIN,  0, 0, 0, 0, SECTION_NONE},
        {"EPCodec",                            &args->videoCodec,                 TYPE_UINT,      0, LIMITS_BOTH, 0, 6, 0, 0, SECTION_NONE},
        {"EPSourceRectSpecified",              &args->sourceRectFlag,             TYPE_UCHAR,     0, LIMITS_BOTH, 0, 1, 0, 0, SECTION_NONE},
        {"EPRateControlSectionIndex",          &args->rateControlSectionNum,      TYPE_UINT,      0, LIMITS_NONE, 0, 0, 0, 0, SECTION_NONE},
        {"ExplicitFrameIntervalPatternLength", &args->frameIntervalPatternLength, TYPE_UINT,      0, LIMITS_NONE, 0, 0, 0, 0, SECTION_NONE},
        {"ExplicitFrameIntervalPattern",       &args->frameIntervalPattern,       TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, INTERVAL_PATTERN_MAX_LENGTH, &args->frameIntervalPatternLength, SECTION_NONE},

        // NvMediaRect
        {"EPSourceRectX0", &args->sourceRect.x0, TYPE_USHORT, 0, LIMITS_MIN, 0, 0, 0, 0, SECTION_NONE},
        {"EPSourceRectY0", &args->sourceRect.y0, TYPE_USHORT, 0, LIMITS_MIN, 0, 0, 0, 0, SECTION_NONE},
        {"EPSourceRectX1", &args->sourceRect.x1, TYPE_USHORT, 0, LIMITS_MIN, 0, 0, 0, 0, SECTION_NONE},
        {"EPSourceRectY1", &args->sourceRect.y1, TYPE_USHORT, 0, LIMITS_MIN, 0, 0, 0, 0, SECTION_NONE},

        // EncodeConfig
        {"EPEncodeWidth",      &args->configParams.encodeWidth,      TYPE_UINT,     0, LIMITS_MIN,  0, 0, 0, 0, SECTION_NONE},
        {"EPEncodeHeight",     &args->configParams.encodeHeight,     TYPE_UINT,     0, LIMITS_MIN,  0, 0, 0, 0, SECTION_NONE},
        {"EPEnableLimitedRGB", &args->configParams.enableLimitedRGB, TYPE_UCHAR,    0, LIMITS_BOTH, 0, 1, 0, 0, SECTION_NONE},
        {"EPFrameRateNum",     &args->configParams.frameRateNum,     TYPE_UINT,     0, LIMITS_NONE, 0, 0, 0, 0, SECTION_NONE},
        {"EPFrameRateDen",     &args->configParams.frameRateDen,     TYPE_UINT,     1, LIMITS_MIN,  1, 0, 0, 0, SECTION_NONE},
        {"EPGopLength",        &args->configH264Params.gopLength,    TYPE_UINT,     0, LIMITS_MIN,  0, 0, 0, 0, SECTION_NONE},
        {"EPGopPattern",       &args->configParams.gopPattern,       TYPE_INT,      0, LIMITS_NONE, 0, 0, 0, 0, SECTION_NONE},
        {"EPMaxNumRefFrames",  &args->configParams.maxNumRefFrames,  TYPE_UCHAR,    0, LIMITS_BOTH, 0, 2, 0, 0, SECTION_NONE},

        // NvMediaEncodeConfigH264
        {"H264Profile",                        &args->configParams.profile,                            TYPE_UCHAR,    0, LIMITS_BOTH, 0, 100, 0, 0, SECTION_NONE},
        {"H264Level",                          &args->configParams.level,                              TYPE_UCHAR,    0, LIMITS_BOTH, 0, 100, 0, 0, SECTION_NONE},
        {"H264Features",                       &args->configH264Params.features,                       TYPE_UINT_HEX, 0, LIMITS_BOTH, 0, 0xF, 0, 0, SECTION_NONE},
        {"H264IdrPeriod",                      &args->configH264Params.idrPeriod,                      TYPE_UINT,     0, LIMITS_MIN,  0, 0,   0, 0, SECTION_NONE},
        {"H264RepeatSPSPPSMode",               &args->configH264Params.repeatSPSPPS,                   TYPE_UINT,     0, LIMITS_BOTH, 0, 2,   0, 0, SECTION_NONE},
        {"H264NumSliceCountMinus1",            &args->configH264Params.numSliceCountMinus1,            TYPE_USHORT,   0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H264DisableDeblockingFilterIDC",     &args->configH264Params.disableDeblockingFilterIDC,     TYPE_UCHAR,    0, LIMITS_BOTH, 0, 2,   0, 0, SECTION_NONE},
        {"H264IntraRefreshPeriod",             &args->configH264Params.intraRefreshPeriod,             TYPE_UINT,     0, LIMITS_MIN,  0, 0,   0, 0, SECTION_NONE},
        {"H264IntraRefreshCnt",                &args->configH264Params.intraRefreshCnt,                TYPE_UINT,     0, LIMITS_MIN,  0, 0,   0, 0, SECTION_NONE},
        {"H264MaxSliceSizeInBytes",            &args->configH264Params.maxSliceSizeInBytes,            TYPE_UINT,     0, LIMITS_MIN,  0, 0,   0, 0, SECTION_NONE},
        {"H264AdaptiveTransformMode",          &args->configH264Params.adaptiveTransformMode,          TYPE_UINT,     0, LIMITS_BOTH, 0, 2,   0, 0, SECTION_NONE},
        {"H264BdirectMode",                    &args->configH264Params.bdirectMode,                    TYPE_UINT,     0, LIMITS_BOTH, 0, 3,   0, 0, SECTION_NONE},
        {"H264EntropyCodingMode",              &args->configH264Params.entropyCodingMode,              TYPE_UINT,     0, LIMITS_BOTH, 0, 2,   0, 0, SECTION_NONE},
        {"H264MotionPredictionExclusionFlags", &args->configH264Params.motionPredictionExclusionFlags, TYPE_UINT_HEX, 0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H264EncodeQuality",                  &args->configH264Params.quality,                        TYPE_UINT,     0, LIMITS_BOTH, 0, 3,   0, 0, SECTION_NONE},

        // NvMediaEncodeConfigH265
        {"H265Profile",                        &args->configParams.profile,                            TYPE_UCHAR,    0, LIMITS_BOTH, 0, 100, 0, 0, SECTION_NONE},
        {"H265Level",                          &args->configParams.level,                              TYPE_UCHAR,    0, LIMITS_BOTH, 0, 100, 0, 0, SECTION_NONE},
        {"H265Features",                       &args->configH265Params.features,                       TYPE_UINT_HEX, 0, LIMITS_BOTH, 0, 0xF, 0, 0, SECTION_NONE},
        {"H265IdrPeriod",                      &args->configH265Params.idrPeriod,                      TYPE_UINT,     0, LIMITS_MIN,  0, 0,   0, 0, SECTION_NONE},
        {"H265RepeatSPSPPSMode",               &args->configH265Params.repeatSPSPPS,                   TYPE_UINT,     0, LIMITS_BOTH, 0, 2,   0, 0, SECTION_NONE},
        {"H265NumSliceCountMinus1",            &args->configH265Params.numSliceCountMinus1,            TYPE_USHORT,   0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H265DisableDeblockingFilter",        &args->configH265Params.disableDeblockingFilter,        TYPE_UCHAR,    0, LIMITS_BOTH, 0, 2,   0, 0, SECTION_NONE},
        {"H265IntraRefreshPeriod",             &args->configH265Params.intraRefreshPeriod,             TYPE_UINT,     0, LIMITS_MIN,  0, 0,   0, 0, SECTION_NONE},
        {"H265IntraRefreshCnt",                &args->configH265Params.intraRefreshCnt,                TYPE_UINT,     0, LIMITS_MIN,  0, 0,   0, 0, SECTION_NONE},
        {"H265MaxSliceSizeInBytes",            &args->configH265Params.maxSliceSizeInBytes,            TYPE_UINT,     0, LIMITS_MIN,  0, 0,   0, 0, SECTION_NONE},
        {"H265EncodeQuality",                  &args->configH265Params.quality,                        TYPE_UINT,     0, LIMITS_BOTH, 0, 2,   0, 0, SECTION_NONE},

        // NvMediaEncodeConfigVP9
        {"VP9Features",                       &args->configVP9Params.features,                         TYPE_UINT_HEX, 0, LIMITS_BOTH, 0, 0x1F, 0, 0, SECTION_NONE},
        {"VP9IdrPeriod",                      &args->configVP9Params.idrPeriod,                        TYPE_UINT,     0, LIMITS_MIN,  0, 0,    0, 0, SECTION_NONE},

        // NvMediaEncodeQP
        {"QPBSlice", &args->quantizationParamsCollection[0].qpInterB, TYPE_UINT, 0, LIMITS_BOTH, 0, 51, 0, 0, SECTION_QP},
        {"QPISlice", &args->quantizationParamsCollection[0].qpIntra,  TYPE_UINT, 0, LIMITS_BOTH, 0, 51, 0, 0, SECTION_QP},
        {"QPPSlice", &args->quantizationParamsCollection[0].qpInterP, TYPE_UINT, 0, LIMITS_BOTH, 0, 51, 0, 0, SECTION_QP},

        // NvMediaEncodeRCParams
        {"RCMode",            &args->rcParamsCollection[0].rcMode,              TYPE_UINT_HEX, 0, LIMITS_BOTH, 0, 4, 0, 0, SECTION_RC},
        {"RCAverageBitrate",  &args->rcParamsCollection[0].averageBitRate,      TYPE_UINT,     0, LIMITS_MIN,  0, 0, 0, 0, SECTION_RC},
        {"RCMaxBitrate",      &args->rcParamsCollection[0].maxBitRate,          TYPE_UINT,     0, LIMITS_MIN,  0, 0, 0, 0, SECTION_RC},
        {"RCVbvBufferSize",   &args->rcParamsCollection[0].vbvBufferSize,       TYPE_UINT,     0, LIMITS_MIN,  0, 0, 0, 0, SECTION_RC},
        {"RCVbvInitialDelay", &args->rcParamsCollection[0].vbvInitialDelay,     TYPE_UINT,     0, LIMITS_MIN,  0, 0, 0, 0, SECTION_RC},
        {"RCEnableMinQP",     &args->rcParamsCollection[0].enableMinQP,         TYPE_UCHAR,    0, LIMITS_BOTH, 0, 1, 0, 0, SECTION_RC},
        {"RCEnableMaxQP",     &args->rcParamsCollection[0].enableMaxQP,         TYPE_UCHAR,    0, LIMITS_BOTH, 0, 1, 0, 0, SECTION_RC},
        {"RCConstQPIndex",    &args->rcParamsCollection[0].rcConstQPSectionNum, TYPE_UINT,     0, LIMITS_NONE, 0, 0, 0, 0, SECTION_RC},
        {"RCMinQPIndex",      &args->rcParamsCollection[0].rcMinQPSectionNum,   TYPE_UINT,     0, LIMITS_NONE, 0, 0, 0, 0, SECTION_RC},
        {"RCMaxQPIndex",      &args->rcParamsCollection[0].rcMaxQPSectionNum,   TYPE_UINT,     0, LIMITS_NONE, 0, 0, 0, 0, SECTION_RC},

        // EncodePicParams
        {"EPEencodePicFlags",    &args->picParamsCollection[0].encodePicFlags,          TYPE_UINT_HEX, 0, LIMITS_BOTH, 0x0, 0x40, 0, 0, SECTION_ENCODE_PIC},
        {"EPInputDuration",      &args->picParamsCollection[0].inputDuration,           TYPE_ULLONG,   0, LIMITS_MIN,    0,    0, 0, 0, SECTION_ENCODE_PIC},
        {"EPPictureType",        &args->picParamsCollection[0].pictureType,             TYPE_INT,      0, LIMITS_BOTH, 0x0, 0xFF, 0, 0, SECTION_ENCODE_PIC},
        {"EPH264PicParamsIndex", &args->picParamsCollection[0].PicParamsSectionNum,     TYPE_UINT,     0, LIMITS_NONE,   0,    0, 0, 0, SECTION_ENCODE_PIC},
        {"EPRCParamsIndex",      &args->picParamsCollection[0].rcParamsSectionNum,      TYPE_UINT,     0, LIMITS_NONE,   0,    0, 0, 0, SECTION_ENCODE_PIC},

        // EncodeH264SEIPayload
        {"H264PayloadSize", &args->payloadsCollection[0].payloadSize, TYPE_UINT,      0, LIMITS_NONE, 0, 0, 0, 0, SECTION_PAYLOAD},
        {"H264PayloadType", &args->payloadsCollection[0].payloadType, TYPE_UINT,      0, LIMITS_NONE, 0, 0, 0, 0, SECTION_PAYLOAD},
        {"H264Payload",     &args->payloadsCollection[0].payload,     TYPE_UCHAR_ARR, 0, LIMITS_NONE, 0, 0, ARRAYS_ALOCATION_SIZE, &args->payloadsCollection[0].payloadSize, SECTION_PAYLOAD},

        // EncodePicParamsH264
        {"H264PayloadArraySize",    &args->picH264ParamsCollection[0].payloadArraySize,    TYPE_UINT,     0, LIMITS_NONE, 0, 0, 0, 0, SECTION_ENCODE_PIC_H264},
        {"H264PayloadArrayIndexes", &args->picH264ParamsCollection[0].payloadArrayIndexes, TYPE_CHAR_ARR, 0, LIMITS_NONE, 0, 0, MAX_PAYLOAD_ARRAY_SIZE, &args->picH264ParamsCollection[0].payloadArraySize, SECTION_ENCODE_PIC_H264},

        // NvMediaEncodeConfigH264VUIParams
        {"VUIAspectRatioInfoPresentFlag",   h264VUIParameters ? &(h264VUIParameters->aspectRatioInfoPresentFlag):NULL,   TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"VUIAspectRatioIDC",               h264VUIParameters ? &(h264VUIParameters->aspectRatioIdc):NULL,               TYPE_UCHAR,  0, LIMITS_BOTH, 0, 255, 0, 0, SECTION_NONE},
        {"VUIAspectSARWidth",               h264VUIParameters ? &(h264VUIParameters->aspectSARWidth):NULL,               TYPE_USHORT, 0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"VUIAspectSARHeight",              h264VUIParameters ? &(h264VUIParameters->aspectSARHeight):NULL,              TYPE_USHORT, 0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"VUIOverscanInfoPresentFlag",      h264VUIParameters ? &(h264VUIParameters->overscanInfoPresentFlag):NULL,      TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"VUIOverscanInfo",                 h264VUIParameters ? &(h264VUIParameters->overscanAppropriateFlag):NULL,      TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"VUIVideoSignalTypePresentFlag",   h264VUIParameters ? &(h264VUIParameters->videoSignalTypePresentFlag):NULL,   TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"VUIVideoFormat",                  h264VUIParameters ? &(h264VUIParameters->videoFormat):NULL,                  TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"VUIVideoFullRangeFlag",           h264VUIParameters ? &(h264VUIParameters->videoFullRangeFlag):NULL,           TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"VUIColourDescriptionPresentFlag", h264VUIParameters ? &(h264VUIParameters->colourDescriptionPresentFlag):NULL, TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"VUIColourPrimaries",              h264VUIParameters ? &(h264VUIParameters->colourPrimaries):NULL,              TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"VUITransferCharacteristics",      h264VUIParameters ? &(h264VUIParameters->transferCharacteristics):NULL,      TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"VUIMatrixCoefficients",           h264VUIParameters ? &(h264VUIParameters->colourMatrix):NULL,                 TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},

         //H265
        {"EPH265PicParamsIndex", &args->picParamsCollection[0].PicParamsSectionNum, TYPE_UINT,     0, LIMITS_NONE,   0,    0, 0, 0, SECTION_ENCODE_PIC},
        {"H265PayloadSize", &args->payloadsCollection[0].payloadSize, TYPE_UINT,      0, LIMITS_NONE, 0, 0, 0, 0, SECTION_PAYLOAD},
        {"H265PayloadType", &args->payloadsCollection[0].payloadType, TYPE_UINT,      0, LIMITS_NONE, 0, 0, 0, 0, SECTION_PAYLOAD},
        {"H265Payload",     &args->payloadsCollection[0].payload,     TYPE_UCHAR_ARR, 0, LIMITS_NONE, 0, 0, ARRAYS_ALOCATION_SIZE, &args->payloadsCollection[0].payloadSize, SECTION_PAYLOAD},
        {"H265PayloadArraySize",    &args->picH265ParamsCollection[0].payloadArraySize,    TYPE_UINT,     0, LIMITS_NONE, 0, 0, 0, 0, SECTION_ENCODE_PIC_H265},
        {"H265PayloadArrayIndexes", &args->picH265ParamsCollection[0].payloadArrayIndexes, TYPE_CHAR_ARR, 0, LIMITS_NONE, 0, 0, MAX_PAYLOAD_ARRAY_SIZE, &args->picH264ParamsCollection[0].payloadArraySize, SECTION_ENCODE_PIC_H265},

        // NvMediaEncodeConfigH265VUIParams
        {"H265VUIAspectRatioInfoPresentFlag",   h265VUIParameters ? &(h265VUIParameters->aspectRatioInfoPresentFlag):NULL,   TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"H265VUIAspectRatioIDC",               h265VUIParameters ? &(h265VUIParameters->aspectRatioIdc):NULL,               TYPE_UCHAR,  0, LIMITS_BOTH, 0, 255, 0, 0, SECTION_NONE},
        {"H265VUIAspectSARWidth",               h265VUIParameters ? &(h265VUIParameters->aspectSARWidth):NULL,               TYPE_USHORT, 0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H265VUIAspectSARHeight",              h265VUIParameters ? &(h265VUIParameters->aspectSARHeight):NULL,              TYPE_USHORT, 0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H265VUIOverscanInfoPresentFlag",      h265VUIParameters ? &(h265VUIParameters->overscanInfoPresentFlag):NULL,      TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"H265VUIOverscanInfo",                 h265VUIParameters ? &(h265VUIParameters->overscanAppropriateFlag):NULL,      TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H265VUIVideoSignalTypePresentFlag",   h265VUIParameters ? &(h265VUIParameters->videoSignalTypePresentFlag):NULL,   TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"H265VUIVideoFormat",                  h265VUIParameters ? &(h265VUIParameters->videoFormat):NULL,                  TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H265VUIVideoFullRangeFlag",           h265VUIParameters ? &(h265VUIParameters->videoFullRangeFlag):NULL,           TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"H265VUIColourDescriptionPresentFlag", h265VUIParameters ? &(h265VUIParameters->colourDescriptionPresentFlag):NULL, TYPE_UCHAR,  0, LIMITS_BOTH, 0, 1,   0, 0, SECTION_NONE},
        {"H265VUIColourPrimaries",              h265VUIParameters ? &(h265VUIParameters->colourPrimaries):NULL,              TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H265VUITransferCharacteristics",      h265VUIParameters ? &(h265VUIParameters->transferCharacteristics):NULL,      TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},
        {"H265VUIMatrixCoefficients",           h265VUIParameters ? &(h265VUIParameters->matrixCoeffs):NULL,                 TYPE_UCHAR,  0, LIMITS_NONE, 0, 0,   0, 0, SECTION_NONE},

        // VP9
        {"EPVP9PicParamsIndex", &args->picParamsCollection[0].PicParamsSectionNum, TYPE_UINT,     0, LIMITS_NONE,   0,    0, 0, 0, SECTION_ENCODE_PIC},

        {NULL} // Specifies the end of the array
    };

    // Defaults
    args->maxInputBuffering = 0;
    args->maxOutputBuffering = 0;

    if((argc == 2 && (strcasecmp(argv[1], "-h") == 0)) || argc < 2) {
        PrintUsage();
        exit(-1);
    }

    paramsCounter = 1;
    while(paramsCounter < argc) {
        if(strcasecmp(argv[paramsCounter], "-v") == 0) {
            if(argv[paramsCounter + 1] && argv[paramsCounter + 1][0] != '-') {
                args->logLevel = atoi(argv[paramsCounter + 1]);
                if(args->logLevel < LEVEL_ERR || args->logLevel > LEVEL_DBG) {
                    LOG_ERR("ParseArgs: Invalid logging level chosen (%d). ", args->logLevel);
                    LOG_ERR(" Setting logging level to LEVEL_ERR (0)\n");
                }
                paramsCounter++;
            } else {
                args->logLevel = LEVEL_DBG; // Max logging level
            }
            SetLogLevel((enum LogLevel)args->logLevel);
        }
        paramsCounter++;
    }

    ConfigParser_InitParamsMap(paramsMap);

    //init crcoption
    args->crcoption.crcGenMode = NVMEDIA_FALSE;
    args->crcoption.crcCheckMode = NVMEDIA_FALSE;

    // Parse rest of the options
    paramsCounter = 1;
    while(paramsCounter < argc) {
        if(strcasecmp(argv[paramsCounter], "-h") == 0) {
            PrintUsage();
            exit(-1);
        } else if(((strcasecmp(argv[paramsCounter], "-cf")) == 0)) {
            filename = argv[paramsCounter + 1];
            paramsCounter += 2;
            ConfigParser_ParseFile(paramsMap, MAX_CONFIG_SECTIONS, sectionsMap, filename);
        } else if(strcasecmp(argv[paramsCounter], "-sf") == 0) {
            filename = argv[paramsCounter + 1];
            LOG_DBG("ParseArgs: Parsing Configfile %s\n", argv[paramsCounter + 1]);
            ConfigParser_ParseFile(paramsMap, MAX_CONFIG_SECTIONS, sectionsMap, filename);
            paramsCounter += 2;
        } else if(strcasecmp(argv[paramsCounter], "-p") == 0) {
            char tempFileName[64];
            FILE *tmpFile;
            struct stat st;
            memset(&st, 0, sizeof(struct stat));
            if (stat("/tmp", &st) == -1) {
                mkdir("/tmp", 0777);
            }
            strncpy(tempFileName,"/tmp/imageenc-parser-temp-XXXXXX", 63);
            mkstemp(tempFileName);
            tmpFile = fopen(tempFileName, "w+");
            if (!tmpFile) {
                LOG_ERR("ParseArgs: Failed creating temp config file for parameters parsing\n");
                return 1;
            }
            // Collect all data until next parameter, put it into content, and parse content.
            ++paramsCounter;
            contentLen = 0;
            paramsNum = paramsCounter;

            // determine the necessary size for content
            while(paramsNum < argc && argv[paramsNum][0] != '-') {
                contentLen += (int)strlen(argv[paramsNum++]);
            }

            contentLen += 1000; // Additional 1000 bytes for spaces and \0s
            configFileContent = malloc(contentLen);
            if(configFileContent == NULL) {
                LOG_ERR("ParseArgs: Failed allocating space for config content. No free memory\n");
                return 1;
            }
            configFileContent[0] = '\0';

            // concatenate all parameters identified before
            while(paramsCounter < paramsNum) {
                LOG_DBG("ParseArgs: Reading command line parameter (-p)\n");
                char *src = &argv[paramsCounter][0];
                char *dest = &configFileContent[(int)strlen(configFileContent)];

                *dest++=' ';
                while(*src != '\0') {
                    if(*src == '=') { // The Parser expects whitespace before and after '='
                        *dest++=' ';
                        *dest++='=';
                        *dest++=' ';
                    } else {
                        *dest++=*src;
                    }
                    src++;
                }
                *dest = '\0';
                paramsCounter++;
            }

            fwrite(configFileContent, 1, strlen(configFileContent), tmpFile);
            fclose(tmpFile);
            ConfigParser_ParseFile(paramsMap, MAX_CONFIG_SECTIONS, sectionsMap, tempFileName);
            free (configFileContent);
            unlink(tempFileName);
        } else if(strcasecmp(argv[paramsCounter], "-v") == 0) {
            // verbose flag was already set earlier
            if(argv[paramsCounter + 1] && argv[paramsCounter + 1][0] != '-') {
                paramsCounter++;
            }
            paramsCounter++;
        } else if(strcasecmp(argv[paramsCounter], "-blackbox") == 0) {
            // blackbox recoding mode
            args->blackboxMode = NV_TRUE;
            if(argv[paramsCounter + 1] && argv[paramsCounter + 1][0] != '-') {
                args->blackboxRecordingTime = atoi(argv[paramsCounter + 1]);
                paramsCounter++;
            } else {
                args->blackboxRecordingTime = 10; //default recording time set to 10 seconds
            }
            LOG_DBG("ParseArgs: Black Box mode enabled, recording time set to %d seconds\n",
                    args->blackboxRecordingTime);
            paramsCounter++;
        } else if(strcmp(&argv[paramsCounter][1], "crc") == 0) {
            if(argv[paramsCounter + 1] && (!strcasecmp(argv[paramsCounter + 1], "chk") || !strcasecmp(argv[paramsCounter + 1], "gen"))) {
                if (!strcasecmp(argv[paramsCounter + 1], "chk"))
                    args->crcoption.crcCheckMode = NVMEDIA_TRUE;
                else if (!strcasecmp(argv[paramsCounter + 1], "gen"))
                    args->crcoption.crcGenMode = NVMEDIA_TRUE;

                if(argv[paramsCounter + 2] && argv[paramsCounter + 2][0] != '-') {
                    strcpy(args->crcoption.crcFilename, argv[paramsCounter + 2]);
                    paramsCounter = paramsCounter + 3;
                } else {
                    LOG_ERR("ParseArgs: -crc must be followed by gen/chk and file name.\n");
                    return -1;
                }
            } else {
                LOG_ERR("ParseArgs: -crc must be followed by gen/chk.\n");
                return -1;
            }
        } else {
            LOG_ERR ("ParseArgs: Error in command line. Unexpected option was specified: %s\n", argv[paramsCounter]);
            PrintUsage();
            return 1;
        }
    }

    ConfigParser_ValidateParams(paramsMap, sectionsMap);

    switch (args->inputFileFormat) {
        case 0: //yuv, yv12
        case 1:
            args->inputSurfAttributes |= (NVMEDIA_IMAGE_ATTRIBUTE_UNMAPPED | NVMEDIA_IMAGE_ATTRIBUTE_SEMI_PLANAR);
            args->inputSurfType = NvMediaSurfaceType_Image_YUV_420;
            break;
        case 2: //ppm
            args->inputSurfType = NvMediaSurfaceType_Image_RGBA;
            break;
        case 3: //png
        default:
            LOG_ERR("main: Unsupported input file type format");
            return 1;
    }
    LOG_DBG("ParseArgs: Displaying Parameters\n");
    if (args->logLevel > 0)
        ConfigParser_DisplayParams(paramsMap, sectionsMap);

    return 0;
}
