/*
 * Copyright (c) 2012-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cmdline.h"
#include "config_parser.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvcommon.h"
#include "nvmedia.h"
#include "surf_utils.h"
#include "nvmediatest_png.h"

#define ALIGN_16(_x) (((_x) + 15) & (~15))
#define ALIGN_32(_x) (((_x) + 31) & (~31))
#define ALIGN_64(_x) (((_x) + 63) & (~63))

typedef enum {
    YUV,
    PPM,
    YUV24, // YUV444
    YUV420_10bit,
    YUV444_10bit,
    PNG,
    UNKNOWN
} FileFormat;

static void AddIVFPrefix_VP9(TestArgs *args, FILE *outputFile, NvU32 frameSize, NvU32 frameNum, NvU32 totalFrameNum) {
    NvU8 ivfPrefix[32] = {0};
    NvU8 buffer[12] = {0};
    NvU32 framesToBeEncoded = args->framesToBeEncoded;

    if(framesToBeEncoded == 0) {
        framesToBeEncoded = totalFrameNum;
    }

    if(frameNum == 0) {
        //IVF file prefix

        memcpy(ivfPrefix, "DKIF", 4);

        *(ivfPrefix + 4) = 0;
        *(ivfPrefix + 5) = 0;
        *(ivfPrefix + 6) = 32;
        *(ivfPrefix + 7) = 0;

        memcpy(ivfPrefix + 8, "VP90", 4);

        *(ivfPrefix + 12) = (args->configParams.encodeWidth & 0xFF);
        *(ivfPrefix + 13) = (args->configParams.encodeWidth >> 8) & 0xFF;
        *(ivfPrefix + 14) = (args->configParams.encodeHeight & 0xFF);
        *(ivfPrefix + 15) = (args->configParams.encodeHeight >> 8) & 0xFF;

        *(ivfPrefix + 16) = (args->configParams.frameRateNum & 0xFF);    // time base den
        *(ivfPrefix + 17) = (args->configParams.frameRateNum>>8) & 0xFF;
        *(ivfPrefix + 18) = (args->configParams.frameRateNum>>16) & 0xFF;
        *(ivfPrefix + 19) = (args->configParams.frameRateNum>>24);

        *(ivfPrefix + 20) = (args->configParams.frameRateDen & 0xFF);    // time base num
        *(ivfPrefix + 21) = (args->configParams.frameRateDen>>8) & 0xFF;
        *(ivfPrefix + 22) = (args->configParams.frameRateDen>>16) & 0xFF;
        *(ivfPrefix + 23) = (args->configParams.frameRateDen>>24);

        *(ivfPrefix + 24) = (framesToBeEncoded & 0xFF);
        *(ivfPrefix + 25) = (framesToBeEncoded>>8) & 0xFF;
        *(ivfPrefix + 26) = (framesToBeEncoded>>16) & 0xFF;
        *(ivfPrefix + 27) = (framesToBeEncoded>>24);

        *(ivfPrefix + 28) = 0;
        *(ivfPrefix + 29) = 0;
        *(ivfPrefix + 30) = 0;
        *(ivfPrefix + 31) = 0;

        fwrite(ivfPrefix, 32, 1, outputFile);
    }

    *(buffer + 0) = (frameSize & 0xFF);
    *(buffer + 1) = (frameSize>>8) & 0xFF;
    *(buffer + 2) = (frameSize>>16) & 0xFF;
    *(buffer + 3) = (frameSize>>24);

    *(buffer + 4) = (frameNum & 0xFF);;
    *(buffer + 5) = (frameNum>>8) & 0xFF;
    *(buffer + 6) = (frameNum>>16) & 0xFF;
    *(buffer + 7) = (frameNum>>24);

    *(buffer + 8)  = 0;
    *(buffer + 9)  = 0;
    *(buffer + 10) = 0;
    *(buffer + 11) = 0;

    fwrite(buffer, 12, 1, outputFile);
}

static void SetEncoderInitParamsH264(NvMediaEncodeInitializeParamsH264 *params, TestArgs *args)
{
    params->encodeHeight          = args->configParams.encodeHeight;
    params->encodeWidth           = args->configParams.encodeWidth;
    params->enableLimitedRGB      = args->configParams.enableLimitedRGB;
    params->frameRateDen          = args->configParams.frameRateDen;
    params->frameRateNum          = args->configParams.frameRateNum;
    params->profile               = args->configParams.profile;
    params->level                 = args->configParams.level;
    params->maxNumRefFrames       = args->configParams.maxNumRefFrames;
    params->enableExternalMEHints = NVMEDIA_FALSE; //Not support yet
}

static void SetEncoderInitParamsH265(NvMediaEncodeInitializeParamsH265 *params, TestArgs *args)
{
    params->encodeHeight                = args->configParams.encodeHeight;
    params->encodeWidth                 = args->configParams.encodeWidth;
    params->enableLimitedRGB            = args->configParams.enableLimitedRGB;
    params->frameRateDen                = args->configParams.frameRateDen;
    params->frameRateNum                = args->configParams.frameRateNum;
    params->profile                     = args->configParams.profile;
    params->level                       = args->configParams.level;
    params->maxNumRefFrames             = args->configParams.maxNumRefFrames;
}

static void SetEncoderInitParamsVP9(NvMediaEncodeInitializeParamsVP9 *params, TestArgs *args)
{
    params->encodeHeight          = args->configParams.encodeHeight;
    params->encodeWidth           = args->configParams.encodeWidth;
    params->enableLimitedRGB      = args->configParams.enableLimitedRGB;
    params->frameRateDen          = args->configParams.frameRateDen;
    params->frameRateNum          = args->configParams.frameRateNum;
    params->maxNumRefFrames       = args->configParams.maxNumRefFrames;
}

static int SetEncodeConfigRCParam(NvMediaEncodeRCParams *rcParams, TestArgs *args, unsigned int rcSectionIndex)
{
    static unsigned int preRCIdex = (unsigned int)-1;

    if (preRCIdex == rcSectionIndex)
        return -1;
    else
       preRCIdex = rcSectionIndex;

    LOG_DBG("SetEncodeConfigRCParam: rc section index: %d, prev index: %d\n", rcSectionIndex, preRCIdex);

    rcParams->rateControlMode = args->rcParamsCollection[rcSectionIndex].rcMode;
    rcParams->numBFrames      = (args->configParams.gopPattern<2)? 0 : (args->configParams.gopPattern-1);

    switch(rcParams->rateControlMode)
    {
        case NVMEDIA_ENCODE_PARAMS_RC_CBR:
             rcParams->params.cbr.averageBitRate  = args->rcParamsCollection[rcSectionIndex].averageBitRate;
             rcParams->params.cbr.vbvBufferSize   = args->rcParamsCollection[rcSectionIndex].vbvBufferSize;
             rcParams->params.cbr.vbvInitialDelay = args->rcParamsCollection[rcSectionIndex].vbvInitialDelay;
             break;
        case NVMEDIA_ENCODE_PARAMS_RC_CONSTQP:
             memcpy(&rcParams->params.const_qp.constQP,
                    &args->quantizationParamsCollection[args->rcParamsCollection[rcSectionIndex].rcConstQPSectionNum - 1],
                    sizeof(NvMediaEncodeQP));
             break;
        case NVMEDIA_ENCODE_PARAMS_RC_VBR:
             rcParams->params.vbr.averageBitRate = args->rcParamsCollection[rcSectionIndex].averageBitRate;
             rcParams->params.vbr.maxBitRate     = args->rcParamsCollection[rcSectionIndex].maxBitRate;
             rcParams->params.vbr.vbvBufferSize  = args->rcParamsCollection[rcSectionIndex].vbvBufferSize;
             rcParams->params.vbr.vbvInitialDelay= args->rcParamsCollection[rcSectionIndex].vbvInitialDelay;
             break;
        case NVMEDIA_ENCODE_PARAMS_RC_VBR_MINQP:
             rcParams->params.vbr_minqp.averageBitRate  = args->rcParamsCollection[rcSectionIndex].averageBitRate;
             rcParams->params.vbr_minqp.maxBitRate      = args->rcParamsCollection[rcSectionIndex].maxBitRate;
             rcParams->params.vbr_minqp.vbvBufferSize   = args->rcParamsCollection[rcSectionIndex].vbvBufferSize;
             rcParams->params.vbr_minqp.vbvInitialDelay = args->rcParamsCollection[rcSectionIndex].vbvInitialDelay;
             if (args->rcParamsCollection[rcSectionIndex].enableMinQP)
             {
                memcpy(&rcParams->params.vbr_minqp.minQP,
                       &args->quantizationParamsCollection[args->rcParamsCollection[rcSectionIndex].rcMinQPSectionNum - 1],
                       sizeof(NvMediaEncodeQP));
             }
             break;
        case NVMEDIA_ENCODE_PARAMS_RC_CBR_MINQP:
             rcParams->params.cbr_minqp.averageBitRate  = args->rcParamsCollection[rcSectionIndex].averageBitRate;
             rcParams->params.cbr_minqp.vbvBufferSize   = args->rcParamsCollection[rcSectionIndex].vbvBufferSize;
             rcParams->params.cbr_minqp.vbvInitialDelay = args->rcParamsCollection[rcSectionIndex].vbvInitialDelay;
             if (args->rcParamsCollection[rcSectionIndex].enableMinQP)
             {
                memcpy(&rcParams->params.cbr_minqp.minQP,
                       &args->quantizationParamsCollection[args->rcParamsCollection[rcSectionIndex].rcMinQPSectionNum - 1],
                       sizeof(NvMediaEncodeQP));
             }
             break;
        default:
             return -1;
    }
    return 0;
}

static void SetEncodePicParamsH264(NvMediaEncodePicParamsH264 *picParams, TestArgs *args, int framesDecoded, int picParamsIndex)
{
    unsigned int h264ParamsIndex, rcSectionIndex, i;
    NvMediaEncodeH264SEIPayload *seiPayload = picParams->seiPayloadArray;

    h264ParamsIndex = args->picParamsCollection[picParamsIndex].PicParamsSectionNum - 1;
    rcSectionIndex  = args->picParamsCollection[picParamsIndex].rcParamsSectionNum - 1;

    picParams->pictureType = args->picParamsCollection[picParamsIndex].pictureType;
    picParams->encodePicFlags = args->picParamsCollection[picParamsIndex].encodePicFlags;

    if(!SetEncodeConfigRCParam(&picParams->rcParams, args, rcSectionIndex)) {
        picParams->encodePicFlags |= NVMEDIA_ENCODE_PIC_FLAG_RATECONTROL_CHANGE;
        LOG_DBG("SetEncodePicParams: PicParamsIndex =%d, RC changed\n", h264ParamsIndex);
    }

    picParams->nextBFrames = 0; //Not support yet
    //ME hint stuff

    //Later!!!

    //SEI payload
    picParams->seiPayloadArrayCnt = args->picH264ParamsCollection[h264ParamsIndex].payloadArraySize;

    for(i = 0; i < picParams->seiPayloadArrayCnt; i++) {
        seiPayload->payloadSize = args->payloadsCollection[args->picH264ParamsCollection[h264ParamsIndex].payloadArrayIndexes[i] - '1'].payloadSize;
        seiPayload->payloadType = args->payloadsCollection[args->picH264ParamsCollection[h264ParamsIndex].payloadArrayIndexes[i] - '1'].payloadType;
        seiPayload->payload     = args->payloadsCollection[args->picH264ParamsCollection[h264ParamsIndex].payloadArrayIndexes[i] - '1'].payload;
        LOG_DBG("SetEncodePicParams: Payload %d, size=%d, type=%d, payload=%x%x%x%x\n",
                i, seiPayload->payloadSize, seiPayload->payloadType, seiPayload->payload[0],
                seiPayload->payload[1],seiPayload->payload[2],seiPayload->payload[3]);
        seiPayload ++;
    }
}

static void SetEncodePicParamsH265(NvMediaEncodePicParamsH265 *picParams, TestArgs *args, int framesDecoded, int picParamsIndex)
{
    unsigned int h265ParamsIndex, rcSectionIndex, i;
    NvMediaEncodeH265SEIPayload *seiPayload = picParams->seiPayloadArray;

    h265ParamsIndex = args->picParamsCollection[picParamsIndex].PicParamsSectionNum - 1;
    rcSectionIndex  = args->picParamsCollection[picParamsIndex].rcParamsSectionNum - 1;

    picParams->pictureType = args->picParamsCollection[picParamsIndex].pictureType;
    picParams->encodePicFlags = args->picParamsCollection[picParamsIndex].encodePicFlags;

    if(!SetEncodeConfigRCParam(&picParams->rcParams, args, rcSectionIndex)) {
        picParams->encodePicFlags |= NVMEDIA_ENCODE_PIC_FLAG_RATECONTROL_CHANGE;
        LOG_DBG("SetEncodePicParams: PicParamsIndex =%d, RC changed\n", h265ParamsIndex);
    }

    picParams->nextBFrames = 0; //Not support yet
    //ME hint stuff

    //Later!!!

    //SEI payload
    picParams->seiPayloadArrayCnt = args->picH265ParamsCollection[h265ParamsIndex].payloadArraySize;

    for(i = 0; i < picParams->seiPayloadArrayCnt; i++) {
        seiPayload->payloadSize = args->payloadsCollection[args->picH265ParamsCollection[h265ParamsIndex].payloadArrayIndexes[i] - '1'].payloadSize;
        seiPayload->payloadType = args->payloadsCollection[args->picH265ParamsCollection[h265ParamsIndex].payloadArrayIndexes[i] - '1'].payloadType;
        seiPayload->payload     = args->payloadsCollection[args->picH265ParamsCollection[h265ParamsIndex].payloadArrayIndexes[i] - '1'].payload;
        LOG_DBG("SetEncodePicParams: Payload %d, size=%d, type=%d, payload=%x%x%x%x\n",
                i, seiPayload->payloadSize, seiPayload->payloadType, seiPayload->payload[0],
                seiPayload->payload[1],seiPayload->payload[2],seiPayload->payload[3]);
        seiPayload ++;
    }
}

static void SetEncodePicParamsVP9(NvMediaEncodePicParamsVP9 *picParams, TestArgs *args, int framesDecoded, int picParamsIndex)
{
    unsigned int vp9ParamsIndex, rcSectionIndex;

    vp9ParamsIndex = args->picParamsCollection[picParamsIndex].PicParamsSectionNum - 1;
    rcSectionIndex  = args->picParamsCollection[picParamsIndex].rcParamsSectionNum - 1;

    picParams->pictureType = args->picParamsCollection[picParamsIndex].pictureType;
    picParams->encodePicFlags = args->picParamsCollection[picParamsIndex].encodePicFlags;

    if(!SetEncodeConfigRCParam(&picParams->rcParams, args, rcSectionIndex)) {
        picParams->encodePicFlags |= NVMEDIA_ENCODE_PIC_FLAG_RATECONTROL_CHANGE;
        LOG_DBG("SetEncodePicParams: PicParamsIndex =%d, RC changed\n", vp9ParamsIndex);
    }

    picParams->nextBFrames = 0; //Not support yet
    //ME hint stuff

    //Later!!!
}

static NvMediaStatus
CheckVersion(void)
{
    NvMediaVersion version;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    memset(&version, 0, sizeof(NvMediaVersion));

    NVMEDIA_SET_VERSION(version, NVMEDIA_VERSION_MAJOR,
                                 NVMEDIA_VERSION_MINOR);
    status = NvMediaCheckVersion(&version);

    return status;
}

int main(int argc, char *argv[])
{
    TestArgs args;
    FILE *crcFile = NULL, *outputFile = NULL, *streamFile;
    char inFileName[FILE_NAME_SIZE], outFileName[FILE_NAME_SIZE], nextFileName[FILE_NAME_SIZE];
    FileFormat inputFileFormat;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaEncodeH264SEIPayload payloadArrH264[MAX_PAYLOAD_ARRAY_SIZE];
    NvMediaEncodeH265SEIPayload payloadArrH265[MAX_PAYLOAD_ARRAY_SIZE];
    NvMediaDevice *device = NULL;
    NvMediaDevice *optionalDevice;
    NvMediaVideoSurface *videoSurface = NULL;
    NvMediaVideoEncoder *testEncoder = NULL;
    NvMediaRect sourceRect;
    NvMediaBool nextFrameFlag = NVMEDIA_TRUE, encodeDoneFlag;
    NvMediaVideoCodec encodeType = NVMEDIA_VIDEO_CODEC_H264;
    union {
        NvMediaEncodeInitializeParamsH264 encoderInitParamsH264;
        NvMediaEncodeInitializeParamsH265 encoderInitParamsH265;
        NvMediaEncodeInitializeParamsVP9  encoderInitParamsVP9;
    } encoderInitParams;
    union {
        NvMediaEncodePicParamsH264 encodePicParamsH264;
        NvMediaEncodePicParamsH265 encodePicParamsH265;
        NvMediaEncodePicParamsVP9  encodePicParamsVP9;
    } encodePicParams;
    NvMediaSurfaceType surfaceType = NvMediaSurfaceType_Video_420;
    unsigned int currFrameParamsSectionIndex, currIdInIntervalPattern = (unsigned int )-1;
    unsigned int localBitrate = 0, maxBitrate = 0, minBitrate = 0xFFFFFFFF;
    long long totalBytes = 0;
    long fileLength;
    NvU8 *buffer;
    NvU16 width, height, encodedWidth, encodedHeight;
    NvU32 framesNum = 0, frameCounter = 1, bytes, bytesAvailable = 0, calcCrc;
    NvU32 frameSize = 0;
    NvU64 startTime, endTime1, endTime2;
    double encodeTime = 0;
    double getbitsTime = 0;
    int crcMisMatch = 0;

    memset(&args,0,sizeof(TestArgs));
    args.configH264Params.h264VUIParameters = calloc(1, sizeof(NvMediaEncodeConfigH264VUIParams));
    args.configH265Params.h265VUIParameters = calloc(1, sizeof(NvMediaEncodeConfigH265VUIParams));

    LOG_DBG("main: Parsing command and reading encoding parameters from config file\n");
    if(ParseArgs(argc, argv, &args)) {
        LOG_ERR("main: Parsing arguments failed\n");
        return -1;
    }

    if(CheckVersion() != NVMEDIA_STATUS_OK) {
        goto fail;
    }

    if(args.crcoption.crcGenMode && args.crcoption.crcCheckMode) {
        LOG_ERR("main: crcGenMode and crcCheckMode cannot be enabled at the same time\n");
        goto fail;
    }

    if(args.videoCodec > 2) {
        LOG_ERR("main: Only H.264, H.265 codec and VP9 is currently supported by NvMedia Encoder\n");
        goto fail;
    }

    if(!args.nosave) {
        LOG_DBG("main: Opening output file\n");
        strcpy(outFileName, args.outfile);
        outputFile = fopen(outFileName, "w+");
        if(!outputFile) {
            LOG_ERR("main: Failed opening '%s' file for writing\n", args.outfile);
            goto fail;
        }
    } else {
        LOG_DBG("main: -nosave option enabled\n");
    }

    LOG_DBG("main: NvMediaDeviceCreate\n");
    device = NvMediaDeviceCreate();
    if(!device) {
        LOG_ERR("main: NvMediaDeviceCreate failed\n");
        goto fail;
    }

    optionalDevice = device;

    frameCounter += args.startFrame - 1;
    LOG_DBG("main: Encode start from frame %d\n", frameCounter);

    switch(args.inputFileFormat) {
        case 0:
        case 1:
            inputFileFormat = YUV;
            break;
        case 2:
            inputFileFormat = PPM;
            break;
        case 3:
            inputFileFormat = YUV24;
            break;
        case 4:
            inputFileFormat = YUV420_10bit;
            break;
        case 5:
            inputFileFormat = YUV444_10bit;
            break;
        case 6:
            inputFileFormat = PNG;
            break;
        default:
            inputFileFormat = UNKNOWN;
    }

    switch(inputFileFormat) {
        case YUV:
        case YUV24:
        case YUV420_10bit:
        case YUV444_10bit:
            strcpy(inFileName, args.infile);
            streamFile = fopen(inFileName, "rb");
            if(!streamFile) {
                LOG_ERR("main: Error opening '%s' for reading\n", inFileName);
                goto fail;
            }
            fseek(streamFile, 0, SEEK_END);
            fileLength = ftell(streamFile);
            fclose(streamFile);
            if(!fileLength) {
                LOG_ERR("main: Zero file length for file %s, len=%d\n", inFileName, (int)fileLength);
                goto fail;
            }
            if (inputFileFormat == YUV) {
                frameSize = args.configParams.encodeWidth * args.configParams.encodeHeight * 3 / 2;
            } else if (inputFileFormat == YUV24) {
                frameSize = args.configParams.encodeWidth * args.configParams.encodeHeight * 3;
            } else if(inputFileFormat == YUV420_10bit) {
                // bpp = 2
                frameSize = args.configParams.encodeWidth * args.configParams.encodeHeight * 3;
            } else if (inputFileFormat == YUV444_10bit) {
                // bpp = 2
                frameSize = args.configParams.encodeWidth * args.configParams.encodeHeight * 6;
            }

            framesNum = fileLength / frameSize;
            break;
        case PPM:
            sprintf(inFileName, args.infile, frameCounter);
            status = GetPPMFileDimensions(inFileName,
                                          (NvU16 *)&args.configParams.encodeWidth,
                                          (NvU16 *)&args.configParams.encodeHeight);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("main: readPPMFile failed\n");
                goto fail;
            }
            break;
        case PNG:
            sprintf(inFileName, args.infile, frameCounter);
            status = GetPNGDimensions(inFileName,
                                      &args.configParams.encodeWidth,
                                      &args.configParams.encodeHeight);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("main: readPPMFile failed\n");
                goto fail;
            }
            break;
        default:
            LOG_ERR("main: Unsupported input file type format");
            goto fail;
    }

    switch(args.videoCodec) {
    case 0: // H264, 16 aglinment
        encodedWidth = ALIGN_16(args.configParams.encodeWidth);
        encodedHeight = ALIGN_16(args.configParams.encodeHeight);
        break;
    case 1: // HEVC, 32 alignment
        if (inputFileFormat == YUV420_10bit || inputFileFormat == YUV444_10bit) {
            encodedWidth = ALIGN_64(args.configParams.encodeWidth);
            encodedHeight = ALIGN_64(args.configParams.encodeHeight);
        } else {
            encodedWidth = ALIGN_32(args.configParams.encodeWidth);
            encodedHeight = ALIGN_32(args.configParams.encodeHeight);
        }
        break;
    case 2: // VP9, 64 alignment
        encodedWidth = ALIGN_64(args.configParams.encodeWidth);
        encodedHeight = ALIGN_64(args.configParams.encodeHeight);
        break;
    default:
        LOG_ERR("main: unknown codec type \n");
        goto fail;
    }

    LOG_DBG("main: Source File Type: %s, Source File Resolution: %ux%u (Default encode size: %ux%u macroblocks: %ux%u)\n",
            inputFileFormat == YUV ? "YUV" : (inputFileFormat == PPM ? "PPM" : (inputFileFormat == YUV24 ? "YUV444" : (inputFileFormat == PNG ? "PNG" : "YUV 10bit"))),
            args.configParams.encodeWidth, args.configParams.encodeHeight, encodedWidth, encodedHeight, encodedWidth>>4, encodedHeight>>4);

    // Set surface type
    switch(inputFileFormat) {
        case YUV:
            surfaceType = NvMediaSurfaceType_YV12;
            break;
        case YUV24:
            surfaceType = NvMediaSurfaceType_YV24;
            break;
        case PPM:
            surfaceType = NvMediaSurfaceType_R8G8B8A8;
        case PNG:
            surfaceType = NvMediaSurfaceType_R8G8B8A8;
            break;
        case YUV420_10bit:
            surfaceType = NvMediaSurfaceType_Video_420_10bit;
            break;
        case YUV444_10bit:
            surfaceType = NvMediaSurfaceType_Video_444_10bit;
            break;
        default:
            LOG_ERR("main: Unsupported input file type format\n");
            goto fail;
    }

    videoSurface = NvMediaVideoSurfaceCreate(device, surfaceType, encodedWidth, encodedHeight);

    if(!videoSurface) {
        LOG_ERR("main: NvMediaVideoSurfaceCreate failed\n");
        return -1;
    }

    LOG_DBG("main: Setting encoder initialization params\n");

    sourceRect.x0 = args.sourceRectFlag == NVMEDIA_TRUE ? args.sourceRect.x0 : 0;
    sourceRect.y0 = args.sourceRectFlag == NVMEDIA_TRUE ? args.sourceRect.y0 : 0;
    sourceRect.x1 = args.sourceRectFlag == NVMEDIA_TRUE ? args.sourceRect.x1 : args.configParams.encodeWidth;
    sourceRect.y1 = args.sourceRectFlag == NVMEDIA_TRUE ? args.sourceRect.y1 : args.configParams.encodeHeight;

    switch(args.videoCodec) {
    case 0: // H264
        encodeType = NVMEDIA_VIDEO_CODEC_H264;
        memset(&encoderInitParams.encoderInitParamsH264, 0, sizeof(NvMediaEncodeInitializeParamsH264));
        SetEncoderInitParamsH264(&encoderInitParams.encoderInitParamsH264, &args);
        args.configH264Params.gopLength = args.configParams.gopLength;
        if (args.configParams.gopPattern == 0)
            args.configH264Params.gopLength = 1;

        LOG_DBG("main: Creating video encoder\n I-Frames only: %s, GOP Size: %u, Frames to encode (0 means all frames): %u, Source rectangle (%ux%u)-(%ux%u)\n",
                    args.configParams.gopPattern == 0 ? "Yes" : "No",
                    args.configH264Params.gopLength,
                    args.framesToBeEncoded,
                    sourceRect.x0, sourceRect.y0, sourceRect.x1, sourceRect.y1);
        break;
    case 1: // HEVC
        encodeType = NVMEDIA_VIDEO_CODEC_HEVC;
        memset(&encoderInitParams.encoderInitParamsH265, 0, sizeof(NvMediaEncodeInitializeParamsH265));
        SetEncoderInitParamsH265(&encoderInitParams.encoderInitParamsH265, &args);
        if(args.configParams.gopPattern > 1)
            args.configParams.gopPattern = 1;

        args.configH265Params.gopLength = args.configParams.gopLength;
        if (args.configParams.gopPattern == 0)
            args.configH265Params.gopLength = 1;

        LOG_DBG("main: Creating video encoder\n I-Frames only: %s, GOP Size: %u, Frames to encode (0 means all frames): %u, Source rectangle (%ux%u)-(%ux%u)\n",
                args.configParams.gopPattern == 0 ? "Yes" : "No",
                args.configH265Params.gopLength,
                args.framesToBeEncoded,
                sourceRect.x0, sourceRect.y0, sourceRect.x1, sourceRect.y1);
        break;
    case 2: // VP9
        encodeType = NVMEDIA_VIDEO_CODEC_VP9;
        memset(&encoderInitParams.encoderInitParamsVP9, 0, sizeof(NvMediaEncodeInitializeParamsVP9));
        SetEncoderInitParamsVP9(&encoderInitParams.encoderInitParamsVP9, &args);
        if(args.configParams.gopPattern > 1)
            args.configParams.gopPattern = 1;

        args.configVP9Params.gopLength = args.configParams.gopLength;
        if (args.configParams.gopPattern == 0)
            args.configVP9Params.gopLength = 1;

        LOG_DBG("main: Creating video encoder\n Key-Frames only: %s, GOP Size: %u, Frames to encode (0 means all frames): %u, Source rectangle (%ux%u)-(%ux%u)\n",
                args.configParams.gopPattern == 0 ? "Yes" : "No",
                args.configVP9Params.gopLength,
                args.framesToBeEncoded,
                sourceRect.x0, sourceRect.y0, sourceRect.x1, sourceRect.y1);
        break;
    default:
        LOG_ERR("main: unknown codec type\n");
        goto fail;
    }

    testEncoder = NvMediaVideoEncoderCreate(device,
                                            encodeType,
                                            &encoderInitParams,             // init params
                                            surfaceType,                    // inputFormat
                                            args.maxInputBuffering,         // maxInputBuffering
                                            args.maxOutputBuffering,        // maxOutputBuffering
                                            optionalDevice);
    if(!testEncoder) {
        LOG_ERR("main: NvMediaVideoEncoderCreate failed\n");
        goto fail;
    }

    LOG_DBG("main: NvMediaVideoEncoderCreate, %p\n", testEncoder);

    if (args.videoCodec == 0)
    {
        SetEncodeConfigRCParam(&args.configH264Params.rcParams, &args, args.rateControlSectionNum - 1);
        status = NvMediaVideoEncoderSetConfiguration(testEncoder, &args.configH264Params);
    } else if(args.videoCodec == 1){
        SetEncodeConfigRCParam(&args.configH265Params.rcParams, &args, args.rateControlSectionNum - 1);
        status = NvMediaVideoEncoderSetConfiguration(testEncoder, &args.configH265Params);
    } else {
        SetEncodeConfigRCParam(&args.configVP9Params.rcParams, &args, args.rateControlSectionNum - 1);
        status = NvMediaVideoEncoderSetConfiguration(testEncoder, &args.configVP9Params);
    }

    if(status != NVMEDIA_STATUS_OK) {
       LOG_ERR("main: SetConfiguration failed\n");
       goto fail;
    }

    LOG_DBG("main: NvMediaVideoEncoderSetConfiguration done\n");

    if(args.crcoption.crcGenMode){
        crcFile = fopen(args.crcoption.crcFilename, "wt");
        if(!crcFile){
            LOG_ERR("main: Cannot open crc gen file for writing\n");
            goto fail;
        }
    } else if(args.crcoption.crcCheckMode){
        crcFile = fopen(args.crcoption.crcFilename, "rb");
        if(!crcFile){
            LOG_ERR("main: Cannot open crc gen file for reading\n");
            goto fail;
        }
    }

    while(nextFrameFlag) {
        GetTimeMicroSec(&startTime);
        static int numBframes = 0, gopLength = 0, frameNumInGop = 0, idrPeriod = 0, frameNumInIDRperiod = 0;
        unsigned int YUVFrameNum = 0;
        unsigned int nextBFrames = 0;
        NvMediaEncodePicType pictureType;
        currIdInIntervalPattern = (currIdInIntervalPattern + 1) < args.frameIntervalPatternLength ? currIdInIntervalPattern + 1 : 0;
        currFrameParamsSectionIndex = args.frameIntervalPattern[currIdInIntervalPattern] - '0' - 1;
        if (args.videoCodec == 0)
        {
            memset(&encodePicParams.encodePicParamsH264, 0, sizeof(NvMediaEncodePicParamsH264));
            encodePicParams.encodePicParamsH264.seiPayloadArray = &payloadArrH264[0];
            SetEncodePicParamsH264(&encodePicParams.encodePicParamsH264, &args, frameCounter - 1, currFrameParamsSectionIndex);
            pictureType = encodePicParams.encodePicParamsH264.pictureType;
        } else if(args.videoCodec == 1) {
            memset(&encodePicParams.encodePicParamsH265, 0, sizeof(NvMediaEncodePicParamsH265));
            encodePicParams.encodePicParamsH265.seiPayloadArray = &payloadArrH265[0];
            SetEncodePicParamsH265(&encodePicParams.encodePicParamsH265, &args, frameCounter - 1, currFrameParamsSectionIndex);
            pictureType = encodePicParams.encodePicParamsH265.pictureType;
        } else {
            memset(&encodePicParams.encodePicParamsVP9, 0, sizeof(NvMediaEncodePicParamsVP9));
            SetEncodePicParamsVP9(&encodePicParams.encodePicParamsVP9, &args, frameCounter - 1, currFrameParamsSectionIndex);
            pictureType = encodePicParams.encodePicParamsVP9.pictureType;
        }

        if (args.configParams.gopPattern == 0) { //Ionly
            pictureType = NVMEDIA_ENCODE_PIC_TYPE_AUTOSELECT;
            YUVFrameNum = frameCounter - 1;
        } else if (args.configParams.gopPattern == 1) { //IP
            if (pictureType != NVMEDIA_ENCODE_PIC_TYPE_P_INTRA_REFRESH)
               pictureType = NVMEDIA_ENCODE_PIC_TYPE_AUTOSELECT;
            YUVFrameNum = frameCounter - 1;
        } else {
            numBframes = args.configParams.gopPattern - 1;
            gopLength  = args.configH264Params.gopLength;
            idrPeriod  = args.configH264Params.idrPeriod;
            if (idrPeriod == 0)
                idrPeriod = gopLength;
            if (frameCounter == 1) {
                pictureType = NVMEDIA_ENCODE_PIC_TYPE_IDR;
                YUVFrameNum = 0;
            } else {
                YUVFrameNum = frameCounter - 1;
                if (frameNumInGop % gopLength == 0 || frameNumInGop % idrPeriod == 0) {
                    pictureType = NVMEDIA_ENCODE_PIC_TYPE_I;
                    frameNumInGop = 0;
                    LOG_DBG("main: pictureType I\n");
                } else if ((frameNumInGop-1) % args.configParams.gopPattern == 0) {
                    pictureType = NVMEDIA_ENCODE_PIC_TYPE_P;
                    LOG_DBG("main: pictureType P\n");
                    if ((frameNumInGop+numBframes)>=((gopLength<idrPeriod)?gopLength:idrPeriod)) {
                        nextBFrames = ((gopLength<idrPeriod)?gopLength:idrPeriod) - frameNumInGop - 1;
                        YUVFrameNum += ((gopLength<idrPeriod)?gopLength:idrPeriod) - frameNumInGop - 1;
                    } else {
                        nextBFrames = numBframes;
                        YUVFrameNum += numBframes;
                    }

                    if (YUVFrameNum >= framesNum)
                        goto Done;
                } else {
                    YUVFrameNum --;
                    pictureType = NVMEDIA_ENCODE_PIC_TYPE_B;
                    nextBFrames = 0;
                    LOG_DBG("main: pictureType B\n");
                }

                if ((frameNumInIDRperiod >= idrPeriod) && (pictureType != NVMEDIA_ENCODE_PIC_TYPE_B) )
                {
                    if (pictureType == NVMEDIA_ENCODE_PIC_TYPE_P)
                        YUVFrameNum = frameCounter - 1;
                    pictureType = NVMEDIA_ENCODE_PIC_TYPE_IDR;
                    nextBFrames = 0;
                    LOG_DBG("main: pictureType IDR\n");
                    frameNumInGop  = 0;
                    frameNumInIDRperiod = 0;
                }
            }
            frameNumInGop++;
            frameNumInIDRperiod++;
        }
        if (args.videoCodec == 0)
        {
            encodePicParams.encodePicParamsH264.pictureType = pictureType;
            encodePicParams.encodePicParamsH264.nextBFrames = nextBFrames;
        } else if(args.videoCodec == 1) {
            encodePicParams.encodePicParamsH265.pictureType = pictureType;
        } else {
            encodePicParams.encodePicParamsVP9.pictureType  = pictureType;
        }

        GetTimeMicroSec(&endTime1);
        encodeTime += (double)(endTime1 - startTime) / 1000.0;
        // Read Frame
        switch(inputFileFormat) {
            case YUV:
            case YUV24:
            case YUV420_10bit:
            case YUV444_10bit:
                LOG_DBG("main: Reading YUV frame %d from file %s to surface location: %p. (W:%d, H:%d)\n",
                        YUVFrameNum, inFileName, videoSurface, args.configParams.encodeWidth, args.configParams.encodeHeight);
                status = ReadYUVFrame(inFileName,
                                      YUVFrameNum,
                                      args.configParams.encodeWidth,
                                      args.configParams.encodeHeight,
                                      videoSurface,
                                      (args.inputFileFormat != 1) ? 1 : 0);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("readYUVFile failed\n");
                    goto fail;
                }
                LOG_DBG("main: ReadYUVFrame %d done\n", YUVFrameNum);
                break;
            case PPM:
                status = GetPPMFileDimensions(inFileName, &width, &height);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("main: GetPPMFileDimensions failed\n");
                    goto fail;
                }

                if(args.configParams.encodeWidth != width ||
                    args.configParams.encodeHeight != height) {
                    LOG_ERR("main: Bad resolution for file %s (%ux%u) must be (%ux%u)\n",
                            inFileName, width, height, args.configParams.encodeWidth, args.configParams.encodeHeight);
                    status = NVMEDIA_STATUS_BAD_PARAMETER;
                    goto fail;
                }
                LOG_DBG("main: Reading PPM frame\n");
                status = ReadPPMFrame(inFileName, videoSurface);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("main: ReadPPMFrame failed\n");
                    goto fail;
                }

                LOG_DBG("main: Encoding file %s as frame %d\n", inFileName, frameCounter);
                break;
            case PNG:
                // Read PNG frame
                LOG_DBG("main: Reading PNG frame\n");
                status = ReadPNGFrame(inFileName, args.configParams.encodeWidth, args.configParams.encodeHeight, videoSurface);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("main: ReadPNGFrame failed\n");
                    goto fail;
                }

                LOG_DBG("main: Encoding file %s as frame %d\n", inFileName, frameCounter);
                break;
            default:
                LOG_ERR("main: Unsupported input file type format\n");
                goto fail;
        }

        GetTimeMicroSec(&startTime);
        LOG_DBG("main: Encoding frame #%d\n", frameCounter);

        status = NvMediaVideoEncoderFeedFrame(testEncoder,
                                              videoSurface,
                                              &sourceRect,
                                              &encodePicParams);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("main: NvMediaVideoEncoderFeedFrame failed: %x\n", status);
            goto fail;
        }

        encodeDoneFlag = NVMEDIA_FALSE;
        while(!encodeDoneFlag) {
            bytesAvailable = 0;
            bytes = 0;
            status = NvMediaVideoEncoderBitsAvailable(testEncoder,
                                                      &bytesAvailable,
                                                      NVMEDIA_ENCODE_BLOCKING_TYPE_IF_PENDING,
                                                      NVMEDIA_VIDEO_ENCODER_TIMEOUT_INFINITE);
            switch(status) {
                case NVMEDIA_STATUS_OK:
                    // Encode Time
                    GetTimeMicroSec(&endTime1);
                    encodeTime += (double)(endTime1 - startTime) / 1000.0;

                    buffer = malloc(bytesAvailable);
                    if(!buffer) {
                        LOG_ERR("main: Error allocating %d bytes\n", bytesAvailable);
                        goto fail;
                    }

                    status = NvMediaVideoEncoderGetBits(testEncoder, &bytes, buffer);
                    if(status != NVMEDIA_STATUS_OK && status != NVMEDIA_STATUS_NONE_PENDING) {
                        LOG_ERR("main: Error getting encoded bits\n");
                        goto fail;
                    }

                    if(bytes != bytesAvailable) {
                        LOG_ERR("main: byte counts do not match %d vs. %d\n", bytesAvailable, bytes);
                        goto fail;
                    }

                    GetTimeMicroSec(&endTime2);
                    getbitsTime += (double)(endTime2 - endTime1) / 1000.0;

                    if(args.crcoption.crcGenMode){
                        //calculate CRC from buffer 'buffer'
                        calcCrc = 0;
                        calcCrc = CalculateBufferCRC(bytesAvailable, calcCrc, buffer);
                        if(!fprintf(crcFile, "%08x\n",calcCrc))
                            LOG_ERR("main: Failed writing calculated CRC to file %s\n", crcFile);
                    } else if(args.crcoption.crcCheckMode){
                        //calculate CRC from buffer 'buffer'
                        NvU32 refCrc;
                        calcCrc = 0;
                        calcCrc = CalculateBufferCRC(bytesAvailable, calcCrc, buffer);
                        if (fscanf(crcFile, "%8x\n", &refCrc) == 1) {
                            if(refCrc != calcCrc){
                                LOG_ERR("main: Frame %d crc 0x%x does not match with ref crc 0x%x\n",
                                        frameCounter, calcCrc, refCrc);
                                crcMisMatch++;
                            }
                        } else {
                            LOG_ERR("main: Failed checking CRC. Failed reading file %s\n", crcFile);
                        }
                    }

                    if(outputFile && args.videoCodec == 2) {
                        AddIVFPrefix_VP9(&args, outputFile, bytesAvailable,
                                         frameCounter-1, framesNum);
                    }

                    if(outputFile && fwrite(buffer, bytesAvailable, 1, outputFile) != 1) {
                        LOG_ERR("main: Error writing %d bytes\n", bytesAvailable);
                        goto fail;
                    }

                    free(buffer);
                    //Tracking the bitrate
                    totalBytes += bytesAvailable;
                    if (frameCounter<=(30+args.startFrame))
                    {
                        localBitrate += bytesAvailable;
                        if (frameCounter == (30+args.startFrame))
                        {
                            maxBitrate = minBitrate = localBitrate;
                        }
                    } else {
                        localBitrate = (localBitrate*29/30 + bytesAvailable);
                        if (localBitrate > maxBitrate)
                            maxBitrate = localBitrate;
                        if (localBitrate < minBitrate)
                            minBitrate = localBitrate;
                    }

                    encodeDoneFlag = 1;
                    break;
                case NVMEDIA_STATUS_PENDING:
                    LOG_DBG("main: Status - pending\n");
                    break;
                case NVMEDIA_STATUS_NONE_PENDING:
                    LOG_ERR("main: No encoded data is pending\n");
                    goto fail;
                default:
                    LOG_ERR("main: Error occured\n");
                    goto fail;
            }
        }

        // Next frame
        frameCounter++;

        if(args.framesToBeEncoded && frameCounter == (args.framesToBeEncoded + 1)) {
            nextFrameFlag = NVMEDIA_FALSE;
        } else {
            switch(inputFileFormat) {
                case YUV:
                case YUV24:
                case YUV420_10bit:
                case YUV444_10bit:
                    if(frameCounter == (framesNum + 1)) {
                        nextFrameFlag = NVMEDIA_FALSE;
                    }
                    break;
                case PPM:
                case PNG:
                    sprintf(nextFileName, args.infile, frameCounter);
                    if(strcmp(nextFileName, inFileName)) {
                        struct stat sts;
                        // Check existence
                        if(stat(nextFileName, &sts) == -1) {
                            nextFrameFlag = NVMEDIA_FALSE;
                        }
                    } else {
                        // Same file name
                        nextFrameFlag = NVMEDIA_FALSE;
                    }
                    strcpy(inFileName, nextFileName);
                    break;
                default:
                    LOG_ERR("main: Unsupported input file type format\n");
                    goto fail;
            }
        }
    }

    if(crcMisMatch) {
        goto fail;
    }

Done:
    //get encoding time info
    LOG_MSG("\nTotal Encoding time for %d frames: %.3f ms\n", frameCounter-args.startFrame, encodeTime + getbitsTime);
    LOG_MSG("Coding time per frame %.4f ms \n", encodeTime / (frameCounter-args.startFrame));
    LOG_MSG("Get bits time per frame %.4f ms \n", getbitsTime / (frameCounter-args.startFrame));
    //Get the bitrate info
    LOG_MSG("\nTotal encoded frames = %d, avg. bitrate=%d, maxBitrate=%d, minBitrate=%d\n",
            frameCounter-args.startFrame,
            (int)(totalBytes*8*30/(frameCounter-args.startFrame)),
            maxBitrate*8, minBitrate*8);
    if (args.crcoption.crcGenMode){
        LOG_MSG("\n***crc gold file %s has been generated***\n", args.crcoption.crcFilename);
    } else if (args.crcoption.crcCheckMode){
        LOG_MSG("\n***crc checking with file %s is successful\n", args.crcoption.crcFilename);
    }

    LOG_MSG("\n***ENCODING PROCESS ENDED SUCCESSFULY***\n");

fail:
    if(args.configH264Params.h264VUIParameters)
        free(args.configH264Params.h264VUIParameters);
    if(args.configH265Params.h265VUIParameters)
        free(args.configH265Params.h265VUIParameters);

    if(outputFile) {
        fclose(outputFile);
    }

    if(crcFile) {
        fclose(crcFile);
    }

    if(videoSurface) {
        NvMediaVideoSurfaceDestroy(videoSurface);
    }

    if(testEncoder) {
        NvMediaVideoEncoderDestroy(testEncoder);
    }

    if(device) {
        NvMediaDeviceDestroy(device);
    }

    LOG_MSG("total encoded frames: %d \n", frameCounter - args.startFrame);
    LOG_MSG("total failures: %d \n", crcMisMatch);

    return 0;
}
