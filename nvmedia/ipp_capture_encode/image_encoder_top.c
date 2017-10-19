/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "nvmedia_iep.h"
#include "nvmedia.h"
#include "image_encoder.h"
#include "image_encoder_top.h"
#include "log_utils.h"

typedef enum{
    GOP_PATTERN_I    = 0,
    GOP_PATTERN_IPP  = 1,
    GOP_PATTERN_IBP  = 2,
    GOP_PATTERN_IBBP = 3
} GOPPattern;

typedef enum {
    LOW_LATENCY_CONSTANT_BITRATE,
    CONSTANT_QUALITY,
    HIGH_QUALITY_CONSTANT_BITRATE
} EncodePreset;

typedef union {
    NvMediaEncodeConfigH264VUIParams    h264VUIParameters;
    NvMediaEncodeConfigH265VUIParams    h265VUIParameters;
} ImageEncodeVUIParams;

typedef struct {
    void                                *pImageEncoderHandle;
    ImageEncoderParams                  imageEncoderParams;
    ImageEncoderPicParams               encodePicParams;
    GOPPattern                          gopPattern;
    ImageEncodeVUIParams                imageEncodeVUIParams;
} EncoderTopContext;

#define DEFAULT_H264_FEATURES                               NVMEDIA_ENCODE_CONFIG_H264_ENABLE_OUTPUT_AUD
#define DEFAULT_H264_NUM_SLICE_COUNT_MINUS_1                4
#define DEFAULT_H264_DISABLE_DEBLOCKING_FILTER              0
#define DEFAULT_H264_MAX_SLICE_SIZE_IN_BYTES                0
#define DEFAULT_H264_ADAPTIVE_TRANSFORM_MODE                NVMEDIA_ENCODE_H264_ADAPTIVE_TRANSFORM_AUTOSELECT
#define DEFAULT_H264_BDIRECT_MODE                           NVMEDIA_ENCODE_H264_BDIRECT_MODE_SPATIAL
#define DEFAULT_H264_MOTION_PREDICTION_EXCLUSION_FLAGS      0

#define DEFAULT_H265_FEATURES                               NVMEDIA_ENCODE_CONFIG_H265_ENABLE_OUTPUT_AUD
#define DEFAULT_H265_NUM_SLICE_COUNT_MINUS_1                4
#define DEFAULT_H265_DISABLE_DEBLOCKING_FILTER              NVMEDIA_FALSE
#define DEFAULT_H265_MAX_SLICE_SIZE_IN_BYTES                0

#define MAX_INPUT_BUFFERING                                 4
#define MAX_OUTPUT_BUFFERING                                16

#define H265_MAIN_PROFILE                                   1

typedef struct {
    NvMediaEncodeProfile                    profile;
    NvMediaEncodeLevel                      level;
    NvMediaEncodeQuality                    quality;
    NvMediaEncodeH264EntropyCodingMode      entropyCodingMode;
    NvU32                                   gopLength;
    GOPPattern                              gopPattern;
    NvU32                                   idrPeriod;
    NvMediaEncodeParamsRCMode               rateControlMode;
    NvU32                                   averageBitRate;
    NvU32                                   maxBitRate;
    NvU32                                   qpIntra;
    NvU32                                   qpInterP;
    NvU32                                   qpInterB;
    NvMediaEncodeH264SPSPPSRepeatMode       repeatSPSPPS;
} H264PresetParams;

typedef struct {
    unsigned char                           profile;
    NvMediaEncodeLevel                      level;
    NvMediaEncodeQuality                    quality;
    NvU32                                   gopLength;
    GOPPattern                              gopPattern;
    NvU32                                   idrPeriod;
    NvMediaEncodeParamsRCMode               rateControlMode;
    NvU32                                   averageBitRate;
    NvU32                                   maxBitRate;
    NvU32                                   qpIntra;
    NvU32                                   qpInterP;
    NvU32                                   qpInterB;
    NvMediaEncodeH264SPSPPSRepeatMode       repeatSPSPPS;
} H265PresetParams;

// low latency, constant bit rate (e.g. video conf)
#define H264_PRESET__LOW_LATENCY_CONSTANT_BITRATE   {                       \
    .profile            = NVMEDIA_ENCODE_PROFILE_BASELINE,                  \
    .level              = NVMEDIA_ENCODE_LEVEL_AUTOSELECT,                  \
    .quality            = NVMEDIA_ENCODE_QUALITY_L1,                        \
    .entropyCodingMode  = NVMEDIA_ENCODE_H264_ENTROPY_CODING_MODE_CAVLC,    \
    .gopLength          = 200,                                              \
    .gopPattern         = GOP_PATTERN_IPP,                                  \
    .idrPeriod          = 200,                                              \
    .rateControlMode    = NVMEDIA_ENCODE_PARAMS_RC_CBR,                     \
    .averageBitRate     = 2000000,                                          \
    .repeatSPSPPS       = NVMEDIA_ENCODE_SPSPPS_REPEAT_IDR_FRAMES,          \
}

// Constant quality (e.g. digital recorder)
// gopLength = 0 ==> align to frame rate in Hz
// idrPeriod = 0 ==> align to frame rate in Hz
#define H264_PRESET__CONSTANT_QUALITY   {                                   \
    .profile            = NVMEDIA_ENCODE_PROFILE_HIGH,                      \
    .level              = NVMEDIA_ENCODE_LEVEL_AUTOSELECT,                  \
    .quality            = NVMEDIA_ENCODE_QUALITY_L1,                        \
    .entropyCodingMode  = NVMEDIA_ENCODE_H264_ENTROPY_CODING_MODE_CABAC,    \
    .gopLength          = 0,                                                \
    .gopPattern         = GOP_PATTERN_IPP,                                  \
    .idrPeriod          = 0,                                                \
    .rateControlMode    = NVMEDIA_ENCODE_PARAMS_RC_CONSTQP,                 \
    .qpIntra            = 20,                                               \
    .qpInterP           = 23,                                               \
    .qpInterB           = 25,                                               \
    .repeatSPSPPS       = NVMEDIA_ENCODE_SPSPPS_REPEAT_IDR_FRAMES,          \
}

// main quality, constant bit rate (e.g. digital broadcast)
// gopLength = 0 ==> align to frame rate in Hz
// idrPeriod = 0 ==> align to frame rate in Hz
#define H264_PRESET__MAIN_QUALITY_CONSTANT_BITRATE {                        \
    .profile            = NVMEDIA_ENCODE_PROFILE_MAIN,                      \
    .level              = NVMEDIA_ENCODE_LEVEL_AUTOSELECT,                  \
    .quality            = NVMEDIA_ENCODE_QUALITY_L1,                        \
    .entropyCodingMode  = NVMEDIA_ENCODE_H264_ENTROPY_CODING_MODE_CAVLC,    \
    .gopLength          = 0,                                                \
    .gopPattern         = GOP_PATTERN_IPP,                                  \
    .idrPeriod          = 0,                                                \
    .rateControlMode    = NVMEDIA_ENCODE_PARAMS_RC_CBR,                     \
    .averageBitRate     = 2000000,                                          \
    .repeatSPSPPS       = NVMEDIA_ENCODE_SPSPPS_REPEAT_IDR_FRAMES,          \
}

// low latency, constant bit rate (e.g. video conf)
#define H265_PRESET__LOW_LATENCY_CONSTANT_BITRATE   {                       \
    .profile            = H265_MAIN_PROFILE,                                \
    .level              = NVMEDIA_ENCODE_LEVEL_AUTOSELECT,                  \
    .quality            = NVMEDIA_ENCODE_QUALITY_L0,                        \
    .gopLength          = 200,                                              \
    .gopPattern         = GOP_PATTERN_IPP,                                  \
    .idrPeriod          = 200,                                              \
    .rateControlMode    = NVMEDIA_ENCODE_PARAMS_RC_CBR,                     \
    .averageBitRate     = 2000000,                                          \
    .repeatSPSPPS       = NVMEDIA_ENCODE_SPSPPS_REPEAT_IDR_FRAMES,          \
}

// Constant quality (e.g. digital recorder)
// gopLength = 0 ==> align to frame rate in Hz
// idrPeriod = 0 ==> align to frame rate in Hz
#define H265_PRESET__CONSTANT_QUALITY   {                                   \
    .profile            = H265_MAIN_PROFILE,                                \
    .level              = NVMEDIA_ENCODE_LEVEL_AUTOSELECT,                  \
    .quality            = NVMEDIA_ENCODE_QUALITY_L0,                        \
    .gopLength          = 0,                                                \
    .gopPattern         = GOP_PATTERN_IPP,                                  \
    .idrPeriod          = 0,                                                \
    .rateControlMode    = NVMEDIA_ENCODE_PARAMS_RC_CONSTQP,                 \
    .qpIntra            = 20,                                               \
    .qpInterP           = 23,                                               \
    .qpInterB           = 25,                                               \
    .repeatSPSPPS       = NVMEDIA_ENCODE_SPSPPS_REPEAT_IDR_FRAMES,          \
}

// main quality, constant bit rate (e.g. digital broadcast)
// gopLength = 0 ==> align to frame rate in Hz
// idrPeriod = 0 ==> align to frame rate in Hz
#define H265_PRESET__MAIN_QUALITY_CONSTANT_BITRATE {                        \
    .profile            = H265_MAIN_PROFILE,                                \
    .level              = NVMEDIA_ENCODE_LEVEL_AUTOSELECT,                  \
    .quality            = NVMEDIA_ENCODE_QUALITY_L0,                        \
    .gopLength          = 0,                                                \
    .gopPattern         = GOP_PATTERN_IPP,                                  \
    .idrPeriod          = 0,                                                \
    .rateControlMode    = NVMEDIA_ENCODE_PARAMS_RC_CBR,                     \
    .averageBitRate     = 2000000,                                          \
    .repeatSPSPPS       = NVMEDIA_ENCODE_SPSPPS_REPEAT_IDR_FRAMES,          \
}

static void
SetEncoderInitParamsH264(
        NvMediaEncodeInitializeParamsH264   *params,
        H264PresetParams                    *pH264Preset,
        TestArgs                            *pArgs)
{
    params->encodeHeight            = pArgs->outputHeight;
    params->encodeWidth             = pArgs->outputWidth;
    params->enableLimitedRGB        = pArgs->enableLimitedRGB;
    params->frameRateDen            = pArgs->frameRateDen;
    params->frameRateNum            = pArgs->frameRateNum;
    params->profile                 = pH264Preset->profile;
    params->enableExternalMEHints   = NVMEDIA_FALSE; //Not support yet

    LOG_DBG("%s: encodeWidth = %d, encodeHeight = %d",
            __func__,
            params->encodeWidth,
            params->encodeHeight);
}

static void
SetEncoderInitParamsH265(
        NvMediaEncodeInitializeParamsH265 *params,
        H265PresetParams                  *pH265Preset,
        TestArgs *args)
{
    params->encodeHeight          = args->outputHeight;
    params->encodeWidth           = args->outputWidth;
    params->enableLimitedRGB      = args->enableLimitedRGB;
    params->frameRateDen          = args->frameRateDen;
    params->frameRateNum          = args->frameRateNum;
    params->profile               = pH265Preset->profile;

    LOG_DBG("%s: encodeWidth = %d, encodeHeight = %d",
            __func__,
            params->encodeWidth,
            params->encodeHeight);
}

static void
SetEncodePicParamsH264(
        EncoderTopContext   *pEncoderTopContext,
        NvU32               frame_count)
{
    NvMediaEncodePicParamsH264  *picParams;

    picParams = &pEncoderTopContext->encodePicParams.encodePicParamsH264;
    picParams->pictureType = NVMEDIA_ENCODE_PIC_TYPE_AUTOSELECT;
    picParams->encodePicFlags = 0;
    picParams->nextBFrames = 0;

    return;
}

static void
SetEncodePicParamsH265(EncoderTopContext   *pEncoderTopContext)
{
    NvMediaEncodePicParamsH265  *picParams;

    picParams = &pEncoderTopContext->encodePicParams.encodePicParamsH265;
    picParams->pictureType = NVMEDIA_ENCODE_PIC_TYPE_AUTOSELECT;
    picParams->encodePicFlags = 0;
    picParams->nextBFrames = 0;
}

static ImageEncoderPicParams *
GetRunTimePicParams (
        void    *pHostContext,
        NvU32   frame_count)
{
    EncoderTopContext   *pEncoderTopContext = NULL;

    if (NULL == pHostContext) {

        LOG_ERR("%s: invalid context\n",
                __func__);

        return NULL;
    }

    pEncoderTopContext = (EncoderTopContext *)pHostContext;

    memset ((void *)&pEncoderTopContext->encodePicParams, 0, sizeof(ImageEncoderPicParams));

    LOG_DBG("%s: frame number = %d\n",
            __func__,
            frame_count);

    if (NVMEDIA_IMAGE_ENCODE_H264 == pEncoderTopContext->imageEncoderParams.videoCodec)
    {
        SetEncodePicParamsH264(pEncoderTopContext, frame_count);

    } else {
        SetEncodePicParamsH265(pEncoderTopContext);
    }

    return &pEncoderTopContext->encodePicParams;
}

void
ImageEncoderTopStop(
        void *pHandle)
{
    EncoderTopContext       *pEncoderTopContext = NULL;

    pEncoderTopContext = (EncoderTopContext *)pHandle;

    if (NULL == pEncoderTopContext) {
        // nothing to do, simply return
        return;
    }

    ImageEncoderStop(pEncoderTopContext->pImageEncoderHandle);

    return;
}

NvMediaStatus
ImageEncoderTopFini(
        void *pHandle)
{
    NvMediaStatus           status;
    EncoderTopContext       *pEncoderTopContext = NULL;

    pEncoderTopContext = (EncoderTopContext *)pHandle;

    if (NULL == pEncoderTopContext) {
        // nothing to do, simply return
        return NVMEDIA_STATUS_OK;
    }

    status = ImageEncoderFini(pEncoderTopContext->pImageEncoderHandle);

    free (pEncoderTopContext);

    return status;
}

NvMediaStatus
ImageEncoderTopStart(
        void *pHandle)
{
    NvMediaStatus       status;
    EncoderTopContext   *pEncoderTopContext;

    pEncoderTopContext = (EncoderTopContext *)pHandle;

    if ((NULL == pEncoderTopContext) ||
        (NULL == pEncoderTopContext->pImageEncoderHandle)) {
        LOG_ERR("%s: Bad arguments\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    status = ImageEncoderStart(pEncoderTopContext->pImageEncoderHandle);

    return status;
}

static void SetEncodeConfigH265(
        EncoderTopContext       *pEncoderTopContext,
        H265PresetParams        *pH265Preset,
        TestArgs                *pAllArgs)
{
    NvMediaEncodeConfigH265             *pNvMediaEncodeConfigH265;
    NvMediaEncodeInitializeParamsH265   *pEncoderInitParamsH265;

    pEncoderInitParamsH265              = &pEncoderTopContext->imageEncoderParams.encoderInitParams.encoderInitParamsH265;
    pNvMediaEncodeConfigH265            = (NvMediaEncodeConfigH265 *)&pEncoderTopContext->imageEncoderParams.encodeConfig.encodeConfigParamsH265;
    pNvMediaEncodeConfigH265->features  = DEFAULT_H265_FEATURES;

    if (NVMEDIA_TRUE == pAllArgs->losslessH265Compression) {
        pNvMediaEncodeConfigH265->features |= NVMEDIA_ENCODE_CONFIG_H265_ENABLE_LOSSLESS_COMPRESSION;
    }

    if (0 == pH265Preset->gopLength) {
        pNvMediaEncodeConfigH265->gopLength = pAllArgs->frameRateNum/pAllArgs->frameRateDen;
    } else {
        pNvMediaEncodeConfigH265->gopLength = pH265Preset->gopLength;
    }

    if (0 == pH265Preset->idrPeriod) {
        pNvMediaEncodeConfigH265->idrPeriod = pNvMediaEncodeConfigH265->gopLength;
    } else {
        pNvMediaEncodeConfigH265->idrPeriod = pH265Preset->gopLength;
    }

    pEncoderTopContext->gopPattern = pH265Preset->gopPattern;

    if (pH265Preset->gopPattern > GOP_PATTERN_IPP) {
        pEncoderInitParamsH265->maxNumRefFrames = 2;
    } else {
        pEncoderInitParamsH265->maxNumRefFrames = pH265Preset->gopPattern;
    }

    pNvMediaEncodeConfigH265->rcParams.rateControlMode = pH265Preset->rateControlMode;
    pNvMediaEncodeConfigH265->rcParams.numBFrames      = 0;

    if (NVMEDIA_ENCODE_PARAMS_RC_CBR == pNvMediaEncodeConfigH265->rcParams.rateControlMode) {
        pNvMediaEncodeConfigH265->rcParams.params.cbr.averageBitRate    = pH265Preset->averageBitRate;
    } else if (NVMEDIA_ENCODE_PARAMS_RC_CONSTQP == pNvMediaEncodeConfigH265->rcParams.rateControlMode) {
        pNvMediaEncodeConfigH265->rcParams.params.const_qp.constQP.qpInterB = pH265Preset->qpInterB;
        pNvMediaEncodeConfigH265->rcParams.params.const_qp.constQP.qpInterP = pH265Preset->qpInterP;
        pNvMediaEncodeConfigH265->rcParams.params.const_qp.constQP.qpIntra  = pH265Preset->qpIntra;
    } else if (NVMEDIA_ENCODE_PARAMS_RC_VBR == pNvMediaEncodeConfigH265->rcParams.rateControlMode) {
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.averageBitRate  = pH265Preset->averageBitRate;
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.maxBitRate      = pH265Preset->maxBitRate;
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.minQP.qpInterB  = pH265Preset->qpInterB;
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.minQP.qpInterP  = pH265Preset->qpInterP;
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.minQP.qpIntra   = pH265Preset->qpIntra;
    } else {
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.averageBitRate = pH265Preset->averageBitRate;
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.maxBitRate     = pH265Preset->maxBitRate;
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.minQP.qpInterB = pH265Preset->qpInterB;
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.minQP.qpInterP = pH265Preset->qpInterP;
        pNvMediaEncodeConfigH265->rcParams.params.vbr_minqp.minQP.qpIntra  = pH265Preset->qpIntra;
    }

    pNvMediaEncodeConfigH265->repeatSPSPPS                                  = pH265Preset->repeatSPSPPS;
    pNvMediaEncodeConfigH265->numSliceCountMinus1                           = DEFAULT_H265_NUM_SLICE_COUNT_MINUS_1;
    pNvMediaEncodeConfigH265->disableDeblockingFilter                       = DEFAULT_H265_DISABLE_DEBLOCKING_FILTER;
    pNvMediaEncodeConfigH265->maxSliceSizeInBytes                           = DEFAULT_H265_MAX_SLICE_SIZE_IN_BYTES;
    pNvMediaEncodeConfigH265->h265VUIParameters                             = &pEncoderTopContext->imageEncodeVUIParams.h265VUIParameters;
    pNvMediaEncodeConfigH265->h265VUIParameters->aspectRatioInfoPresentFlag = NVMEDIA_FALSE;
    pNvMediaEncodeConfigH265->h265VUIParameters->overscanInfoPresentFlag    = NVMEDIA_FALSE;
    pNvMediaEncodeConfigH265->h265VUIParameters->videoSignalTypePresentFlag = NVMEDIA_FALSE;
    pNvMediaEncodeConfigH265->quality                                       = pH265Preset->quality;

    return;
}

static void SetEncodeConfigH264(
        EncoderTopContext       *pEncoderTopContext,
        H264PresetParams        *pH264Preset,
        TestArgs                *pAllArgs)
{
    NvMediaEncodeConfigH264             *pNvMediaEncodeConfigH264;
    NvMediaEncodeInitializeParamsH264   *pEncoderInitParamsH264;

    pEncoderInitParamsH264              = &pEncoderTopContext->imageEncoderParams.encoderInitParams.encoderInitParamsH264;
    pNvMediaEncodeConfigH264            = (NvMediaEncodeConfigH264 *)&pEncoderTopContext->imageEncoderParams.encodeConfig.encodeConfigParamsH264;
    pNvMediaEncodeConfigH264->features  = DEFAULT_H264_FEATURES;

    if (0 == pH264Preset->gopLength) {
        pNvMediaEncodeConfigH264->gopLength = pAllArgs->frameRateNum/pAllArgs->frameRateDen;
    } else {
        pNvMediaEncodeConfigH264->gopLength = pH264Preset->gopLength;
    }

    if (0 == pH264Preset->idrPeriod) {
        pNvMediaEncodeConfigH264->idrPeriod = pNvMediaEncodeConfigH264->gopLength;
    } else {
        pNvMediaEncodeConfigH264->idrPeriod = pH264Preset->gopLength;
    }

    pEncoderTopContext->gopPattern = pH264Preset->gopPattern;

    if (pH264Preset->gopPattern > GOP_PATTERN_IPP) {
        pEncoderInitParamsH264->maxNumRefFrames = 2;
    } else {
        pEncoderInitParamsH264->maxNumRefFrames = pH264Preset->gopPattern;
    }

    pNvMediaEncodeConfigH264->rcParams.rateControlMode = pH264Preset->rateControlMode;
    pNvMediaEncodeConfigH264->rcParams.numBFrames      = pH264Preset->gopPattern < GOP_PATTERN_IBP ? 0 : (pH264Preset->gopPattern - 1);

    if (NVMEDIA_ENCODE_PARAMS_RC_CBR == pNvMediaEncodeConfigH264->rcParams.rateControlMode) {
        pNvMediaEncodeConfigH264->rcParams.params.cbr.averageBitRate    = pH264Preset->averageBitRate;
    } else if (NVMEDIA_ENCODE_PARAMS_RC_CONSTQP == pNvMediaEncodeConfigH264->rcParams.rateControlMode) {
        pNvMediaEncodeConfigH264->rcParams.params.const_qp.constQP.qpInterB = pH264Preset->qpInterB;
        pNvMediaEncodeConfigH264->rcParams.params.const_qp.constQP.qpInterP = pH264Preset->qpInterP;
        pNvMediaEncodeConfigH264->rcParams.params.const_qp.constQP.qpIntra  = pH264Preset->qpIntra;
    } else if (NVMEDIA_ENCODE_PARAMS_RC_VBR == pNvMediaEncodeConfigH264->rcParams.rateControlMode) {
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.averageBitRate  = pH264Preset->averageBitRate;
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.maxBitRate      = pH264Preset->maxBitRate;
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.minQP.qpInterB  = pH264Preset->qpInterB;
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.minQP.qpInterP  = pH264Preset->qpInterP;
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.minQP.qpIntra   = pH264Preset->qpIntra;
    } else {
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.averageBitRate = pH264Preset->averageBitRate;
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.maxBitRate = pH264Preset->maxBitRate;
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.minQP.qpInterB = pH264Preset->qpInterB;
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.minQP.qpInterP = pH264Preset->qpInterP;
        pNvMediaEncodeConfigH264->rcParams.params.vbr_minqp.minQP.qpIntra = pH264Preset->qpIntra;
    }

    pNvMediaEncodeConfigH264->repeatSPSPPS                                  = pH264Preset->repeatSPSPPS;
    pNvMediaEncodeConfigH264->numSliceCountMinus1                           = DEFAULT_H264_NUM_SLICE_COUNT_MINUS_1;
    pNvMediaEncodeConfigH264->disableDeblockingFilterIDC                    = DEFAULT_H264_DISABLE_DEBLOCKING_FILTER;
    pNvMediaEncodeConfigH264->adaptiveTransformMode                         = DEFAULT_H264_ADAPTIVE_TRANSFORM_MODE;
    pNvMediaEncodeConfigH264->bdirectMode                                   = DEFAULT_H264_BDIRECT_MODE;
    pNvMediaEncodeConfigH264->entropyCodingMode                             = pH264Preset->entropyCodingMode;
    pNvMediaEncodeConfigH264->maxSliceSizeInBytes                           = DEFAULT_H264_MAX_SLICE_SIZE_IN_BYTES;
    pNvMediaEncodeConfigH264->h264VUIParameters                             = &pEncoderTopContext->imageEncodeVUIParams.h264VUIParameters;
    pNvMediaEncodeConfigH264->h264VUIParameters->aspectRatioInfoPresentFlag = NVMEDIA_FALSE;
    pNvMediaEncodeConfigH264->h264VUIParameters->overscanInfoPresentFlag    = NVMEDIA_FALSE;
    pNvMediaEncodeConfigH264->h264VUIParameters->videoSignalTypePresentFlag = NVMEDIA_FALSE;
    pNvMediaEncodeConfigH264->motionPredictionExclusionFlags                = DEFAULT_H264_MOTION_PREDICTION_EXCLUSION_FLAGS;
    pNvMediaEncodeConfigH264->quality                                       = pH264Preset->quality;

    return;
}

void *ImageEncoderTopInit(
        TestArgs                                    *pAllArgs,
        NvQueue                                     *pInputImageQueue,
        ImageEncoderReturnUncompressedBufferFunc    pReturnUncompressedBufferFunc,
        volatile NvMediaBool                        *pQuit,
        NvQueue                                     **pOutputQueueRef)
{
    void                                *pImageEncoderHandle = NULL;
    NvMediaEncodeInitializeParamsH264   *pEncoderInitParamsH264 = NULL;
    NvMediaEncodeInitializeParamsH265   *pEncoderInitParamsH265 = NULL;
    EncoderTopContext                   *pEncoderTopContext = NULL;

    H264PresetParams    H264Preset_LOW_LATENCY_CONSTANT_BITRATE     = H264_PRESET__LOW_LATENCY_CONSTANT_BITRATE;
    H264PresetParams    H264Preset_CONSTANT_QUALITY                 = H264_PRESET__CONSTANT_QUALITY;
    H264PresetParams    H264Preset_HIGH_QUALITY_CONSTANT_BITRATE    = H264_PRESET__MAIN_QUALITY_CONSTANT_BITRATE;
    H264PresetParams    *pH264PresetParams;

    H265PresetParams    H265Preset_LOW_LATENCY_CONSTANT_BITRATE     = H265_PRESET__LOW_LATENCY_CONSTANT_BITRATE;
    H265PresetParams    H265Preset_CONSTANT_QUALITY                 = H265_PRESET__CONSTANT_QUALITY;
    H265PresetParams    H265Preset_HIGH_QUALITY_CONSTANT_BITRATE    = H265_PRESET__MAIN_QUALITY_CONSTANT_BITRATE;
    H265PresetParams    *pH265PresetParams;


    if ((NULL == pAllArgs) ||
        (NULL == pQuit) ||
        (NULL == pReturnUncompressedBufferFunc)) {
        LOG_ERR ("%s : invalid inputs\n",
                __func__);
        return NULL;
    }

    if ((NVMEDIA_IMAGE_ENCODE_H264 != pAllArgs->videoCodec) &&
        (NVMEDIA_IMAGE_ENCODE_HEVC != pAllArgs->videoCodec)) {
        LOG_ERR ("%s : only H264 and HEVC encoding supported");
        return NULL;
    }

    if ((LOW_LATENCY_CONSTANT_BITRATE != pAllArgs->encodePreset) &&
        (CONSTANT_QUALITY != pAllArgs->encodePreset) &&
        (HIGH_QUALITY_CONSTANT_BITRATE != pAllArgs->encodePreset)) {
            LOG_ERR("%s: Invalid encode preset %d\n",
                    __func__,
                    pAllArgs->encodePreset);
            return NULL;
    }

    pEncoderTopContext = (EncoderTopContext *)calloc (1, sizeof(EncoderTopContext));

    if (NULL == pEncoderTopContext) {
        LOG_ERR ("%s: failed to allocate context for image encoder top\n",
                __func__);
        return NULL;
    }

    pEncoderTopContext->imageEncoderParams.videoCodec           = pAllArgs->videoCodec;
    pEncoderTopContext->imageEncoderParams.maxInputBuffering    = MAX_INPUT_BUFFERING;
    pEncoderTopContext->imageEncoderParams.maxOutputBuffering   = MAX_OUTPUT_BUFFERING;
    pEncoderTopContext->imageEncoderParams.sourceRect.x0        = 0;
    pEncoderTopContext->imageEncoderParams.sourceRect.y0        = 0;
    pEncoderTopContext->imageEncoderParams.sourceRect.x1        = pAllArgs->outputWidth;
    pEncoderTopContext->imageEncoderParams.sourceRect.y1        = pAllArgs->outputHeight;

    if (NVMEDIA_IMAGE_ENCODE_H264 == pEncoderTopContext->imageEncoderParams.videoCodec) {
        switch (pAllArgs->encodePreset) {
        case LOW_LATENCY_CONSTANT_BITRATE :
            pH264PresetParams = &H264Preset_LOW_LATENCY_CONSTANT_BITRATE;
            break;
        case CONSTANT_QUALITY:
            pH264PresetParams = &H264Preset_CONSTANT_QUALITY;
            break;
        case HIGH_QUALITY_CONSTANT_BITRATE:
            pH264PresetParams = &H264Preset_HIGH_QUALITY_CONSTANT_BITRATE;
            break;
        }

        if ((LOW_LATENCY_CONSTANT_BITRATE == pAllArgs->encodePreset) || (HIGH_QUALITY_CONSTANT_BITRATE == pAllArgs->encodePreset)) {
            // for CBR encoding, if the user wants to override the preset bit rate, use it
            if (pAllArgs->cbrEncodedDataRateMbps) {
                pH264PresetParams->averageBitRate = pAllArgs->cbrEncodedDataRateMbps * 1000000;
            }
        } else {
            // CONSTANT_QUALITY == pAllArgs->encodePreset
            // fOR constant QP mode, if the user wants to override the QP values, use them
            if (pAllArgs->qpI) {
                pH264PresetParams->qpIntra = pAllArgs->qpI;
            }

            if (pAllArgs->qpP) {
                pH264PresetParams->qpInterP = pAllArgs->qpP;
            }
        }

        pEncoderInitParamsH264 = &pEncoderTopContext->imageEncoderParams.encoderInitParams.encoderInitParamsH264;
        SetEncoderInitParamsH264(pEncoderInitParamsH264, pH264PresetParams, pAllArgs);
        SetEncodeConfigH264(pEncoderTopContext, pH264PresetParams, pAllArgs);
    } else {
        switch (pAllArgs->encodePreset) {
        case LOW_LATENCY_CONSTANT_BITRATE :
            pH265PresetParams = &H265Preset_LOW_LATENCY_CONSTANT_BITRATE;
            break;
        case CONSTANT_QUALITY:
            pH265PresetParams = &H265Preset_CONSTANT_QUALITY;
            break;
        case HIGH_QUALITY_CONSTANT_BITRATE:
            pH265PresetParams = &H265Preset_HIGH_QUALITY_CONSTANT_BITRATE;
            break;
        }

        if ((LOW_LATENCY_CONSTANT_BITRATE == pAllArgs->encodePreset) || (HIGH_QUALITY_CONSTANT_BITRATE == pAllArgs->encodePreset)) {
            // for CBR encoding, if the user wants to override the preset bit rate, use it
            if (pAllArgs->cbrEncodedDataRateMbps) {
                pH265PresetParams->averageBitRate = pAllArgs->cbrEncodedDataRateMbps * 1000000;
            }
        } else {
            // CONSTANT_QUALITY == pAllArgs->encodePreset
            // fOR constant QP mode, if the user wants to override the QP values, use them
            if (pAllArgs->qpI) {
                pH265PresetParams->qpIntra = pAllArgs->qpI;
            }

            if (pAllArgs->qpP) {
                pH265PresetParams->qpInterP = pAllArgs->qpP;
            }
        }

        pEncoderInitParamsH265 = &pEncoderTopContext->imageEncoderParams.encoderInitParams.encoderInitParamsH265;
        SetEncoderInitParamsH265(pEncoderInitParamsH265, pH265PresetParams, pAllArgs);
        SetEncodeConfigH265(pEncoderTopContext, pH265PresetParams, pAllArgs);
    }

    pEncoderTopContext->imageEncoderParams.inputFormat                      = NvMediaSurfaceType_Image_YUV_420;
    pEncoderTopContext->imageEncoderParams.pInputImageQueue                 = pInputImageQueue;
    pEncoderTopContext->imageEncoderParams.pReturnUncompressedBufferFunc    = pReturnUncompressedBufferFunc;

    // create the image encoder
    pImageEncoderHandle = ImageEncoderInit(&pEncoderTopContext->imageEncoderParams,
                                           pQuit,
                                           pOutputQueueRef);

    ImageEncoderRegisterRunTimePicParamsCallback(pImageEncoderHandle,
                                                 &GetRunTimePicParams,
                                                 pEncoderTopContext);

    pEncoderTopContext->pImageEncoderHandle = pImageEncoderHandle;

    return (void *)pEncoderTopContext;
}
