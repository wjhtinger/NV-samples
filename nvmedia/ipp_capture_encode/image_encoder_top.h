/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _IMAGE_ENCODER_TOP_H_
#define _IMAGE_ENCODER_TOP_H_

#include "image_encoder.h"
#include "nvmedia.h"
#include "cmdline.h"

#ifdef __cplusplus
extern "C" {
#endif

// pOutputQueueRef is used to collect a reference to the output
// queue used by the image encoder instance. Set this to NULL
// if it is not needed to collect the reference to the output
// queue used by the image encoder
void *ImageEncoderTopInit(
        TestArgs                                    *pAllArgs,
        NvQueue                                     *pInputImageQueue,
        ImageEncoderReturnUncompressedBufferFunc    pImageEncoderReturnUncompressedBufferFunc,
        volatile NvMediaBool                        *pQuit,
        NvQueue                                     **pOutputQueueRef);

NvMediaStatus
ImageEncoderTopStart(
        void *pHandle);

NvMediaStatus
ImageEncoderTopFini(
        void *pHandle);

void
ImageEncoderTopStop(
        void *pHandle);

#ifdef __cplusplus
}
#endif

#endif //_IMAGE_ENCODER_TOP_H_
