/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _NVMEDIA_TEST_CMD_LINE_H_
#define _NVMEDIA_TEST_CMD_LINE_H_

#include "nvmedia_image.h"
#include "video_parser.h"

#define DEFAULT_GOP_SIZE                30
#define DEFAULT_FRAME_SIZE              15000
#define ARRAYS_ALOCATION_SIZE           1000
#define FILE_NAME_SIZE                  256
#define MAX_CONFIG_SECTIONS             32
#define INTERVAL_PATTERN_MAX_LENGTH     1000
#define MAX_PAYLOAD_ARRAY_SIZE          10
#define FILE_PATH_LENGTH_MAX            256

typedef struct {
    char        crcFilename[FILE_PATH_LENGTH_MAX];
    NvMediaBool crcGenMode;
    NvMediaBool crcCheckMode;
} CRCOptions;

typedef struct _EncodePicParams {
    unsigned int            encodePicFlags;
    unsigned long long      inputDuration;
    NvMediaEncodePicType    pictureType;
    unsigned int            PicParamsSectionNum;
    unsigned int            rcParamsSectionNum;
} EncodePicParams;

typedef struct _EncodePicParamsH264 {
    NvMediaBool     refPicFlag;
    unsigned int    forceIntraRefreshWithFrameCnt;
    unsigned char   sliceTypeData[ARRAYS_ALOCATION_SIZE];
    unsigned int    sliceTypeArrayCnt;
    char            payloadArrayIndexes[MAX_PAYLOAD_ARRAY_SIZE];
    unsigned int    payloadArraySize;
    unsigned int    mvcPicParamsSectionNum;
} EncodePicParamsH264;

typedef struct _EncodeH264SEIPayload {
    unsigned int    payloadSize;
    unsigned int    payloadType;
    unsigned char   payload[ARRAYS_ALOCATION_SIZE];
} EncodeH264SEIPayload;

typedef struct _EncodePicParamsH265 {
    NvMediaBool     refPicFlag;
    unsigned int    forceIntraRefreshWithFrameCnt;
    unsigned char   sliceTypeData[ARRAYS_ALOCATION_SIZE];
    unsigned int    sliceTypeArrayCnt;
    char            payloadArrayIndexes[MAX_PAYLOAD_ARRAY_SIZE];
    unsigned int    payloadArraySize;
} EncodePicParamsH265;

typedef struct _EncodeH265SEIPayload {
    unsigned int    payloadSize;
    unsigned int    payloadType;
    unsigned char   payload[ARRAYS_ALOCATION_SIZE];
} EncodeH265SEIPayload;

typedef struct _EncodeConfig {
    unsigned char               profile;
    unsigned char               level;
    int                         gopPattern;
    unsigned int                encodeWidth;
    unsigned int                encodeHeight;
    NvMediaBool                 enableLimitedRGB;
    unsigned int                darWidth;
    unsigned int                darHeight;
    unsigned int                frameRateNum;
    unsigned int                frameRateDen;
    unsigned char               maxNumRefFrames;
} EncodeConfig;

typedef struct _EncodeRCParams {
    NvMediaEncodeParamsRCMode   rcMode;
    unsigned int                rcConstQPSectionNum;
    unsigned int                averageBitRate;
    unsigned int                maxBitRate;
    unsigned int                vbvBufferSize;
    unsigned int                vbvInitialDelay;
    NvMediaBool                 enableMinQP;
    NvMediaBool                 enableMaxQP;
    unsigned int                rcMinQPSectionNum;
    unsigned int                rcMaxQPSectionNum;
} EncodeRCParams;

typedef struct _TestArgs {
    char                        infile[FILE_NAME_SIZE];
    char                        outfile[FILE_NAME_SIZE];
    unsigned int                inputFileFormat;
    unsigned int                startFrame;
    unsigned int                framesToBeEncoded;
    unsigned int                videoCodec;
    NvMediaSurfaceType          inputSurfType;
    NvU32                       inputSurfAttributes;
    NvMediaImageAdvancedConfig  inputSurfAdvConfig;

    unsigned int                maxInputBuffering;
    unsigned int                maxOutputBuffering;
    NvMediaBool                 sourceRectFlag;
    NvMediaRect                 sourceRect;
    unsigned int                rateControlSectionNum;
    char                        frameIntervalPattern[INTERVAL_PATTERN_MAX_LENGTH];
    unsigned int                frameIntervalPatternLength;
    EncodeConfig                configParams;
    NvMediaEncodeConfigH264     configH264Params;
    EncodePicParams             picParamsCollection[MAX_CONFIG_SECTIONS];
    EncodePicParamsH264         picH264ParamsCollection[MAX_CONFIG_SECTIONS];
    EncodeRCParams              rcParamsCollection[MAX_CONFIG_SECTIONS];
    EncodeH264SEIPayload        payloadsCollection[MAX_CONFIG_SECTIONS];
    NvMediaEncodeQP             quantizationParamsCollection[MAX_CONFIG_SECTIONS];

    NvMediaEncodeConfigH265     configH265Params;
    EncodePicParamsH265         picH265ParamsCollection[MAX_CONFIG_SECTIONS];
    EncodeH265SEIPayload        payloadsH265Collection[MAX_CONFIG_SECTIONS];

    NvMediaEncodeConfigVP9      configVP9Params;

    CRCOptions                  crcoption;
    NvMediaBool                 blackboxMode;
    int                         blackboxRecordingTime;
    int                         logLevel;
} TestArgs;

void PrintUsage(void);
int  ParseArgs(int argc, char **argv, TestArgs *args);

#endif /* _NVMEDIA_TEST_CMD_LINE_H_ */
