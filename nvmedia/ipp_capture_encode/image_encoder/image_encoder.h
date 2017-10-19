/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __IMAGE_ENCODER_H__
#define __IMAGE_ENCODER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmdline.h"
#include "thread_utils.h"
#include "nvmedia_iep.h"
#include "op_stream_handler.h"

// taking the maximum encoded buffer size to be the maximum uncompressed image size
#define MAX_ENCODED_BUFFER_SIZE     (3479040)    // 1920*1208*1.5

typedef union {
    NvMediaEncodeInitializeParamsH264   encoderInitParamsH264;
    NvMediaEncodeInitializeParamsH265   encoderInitParamsH265;
} ImageEncoderInitParams;

typedef union {
    NvMediaEncodePicParamsH264          encodePicParamsH264;
    NvMediaEncodePicParamsH265          encodePicParamsH265;
} ImageEncoderPicParams;

typedef union {
    NvMediaEncodeConfigH264             encodeConfigParamsH264;
    NvMediaEncodeConfigH265             encodeConfigParamsH265;
} ImageEncodeConfig;

typedef ImageEncoderPicParams   *(*RunTimePicParamsCallback)(void *pHostContext, NvU32 frame_count);
typedef NvMediaStatus           (*ImageEncoderPutBufferToEncoderFunc)(void *pEncodedBufferContainer);
typedef NvMediaStatus           (*ImageEncoderReturnUncompressedBufferFunc)(OutputBufferContext *pBufferContext);

typedef struct {
    NvU32                               encodedBufferSizeBytes;
    ImageEncoderPutBufferToEncoderFunc  pPutBufferFunc;
    void                                *pImageEncoderCtx;
    NvU8                                encodedBuffer[MAX_ENCODED_BUFFER_SIZE];
} EncodedBufferContainer;

typedef struct {
    // encoder to use
    NvMediaIEPType                              videoCodec;

    ImageEncoderInitParams                      encoderInitParams;

    // input surface type, possible inputs are
    // NvMediaSurfaceType_Image_YUV_420 and NvMediaSurfaceType_Image_RGBA
    NvMediaSurfaceType                          inputFormat;

    NvU32                                       maxInputBuffering;

    NvU32                                       maxOutputBuffering;

    NvU32                                       encodeWidth;

    NvU32                                       encodeHeight;

    // source rectangle within the input image to display
    NvMediaRect                                 sourceRect;

    ImageEncodeConfig                           encodeConfig;

    // reference to input queue. If set to NULL, image
    // encoder will create its own queue
    NvQueue                                     *pInputImageQueue;

    // reference to a function to allow the image encoder
    // to return uncompressed buffers back to the source
    ImageEncoderReturnUncompressedBufferFunc    pReturnUncompressedBufferFunc;
} ImageEncoderParams;

// pOutputQueueRef is used to collect a reference to the output
// queue used by the image encoder instance. Set this to NULL
// if it is not needed to collect the reference to the output
// queue used by the image encoder
void *
ImageEncoderInit(
ImageEncoderParams      *pImageEncoderParams,
volatile NvMediaBool    *pQuit,
NvQueue                 **pOutputQueueRef);

NvMediaStatus
ImageEncoderFini(void *pHandle);

NvMediaStatus
ImageEncoderStart(void *pHandle);

void
ImageEncoderStop(void *pHandle);

NvMediaStatus
ImageEncoderRegisterRunTimePicParamsCallback(
        void                        *pHandle,
        RunTimePicParamsCallback    pGetRunTimePicParams,
        void                        *pHostContext);

#ifdef __cplusplus
}
#endif

#endif // __IMAGE_ENCODER_H__
