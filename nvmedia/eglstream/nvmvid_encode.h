/*
 * Copyright (c) 2012-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef _NVMVIDEO_ENCODE_H_
#define _NVMVIDEO_ENCODE_H_

#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <stdio.h>
#include "egl_utils.h"
#include "nvmedia.h"
#include "nvcommon.h"

extern NvBool verbose;
extern NvU32 uFramesNum;

typedef struct {
    NvMediaEncodeConfigH264     encodeConfigH264Params;

    //internal variables
    FILE                        *outputFile;
    NvMediaVideoEncoder         *h264Encoder;
    NvU32                       uFrameCounter;
    NvMediaEncodePicParamsH264  encodePicParams;
} InputParameters;

int InitEncoder(InputParameters *pParams, int width, int height, NvMediaSurfaceType surfaceType);

int EncodeOneFrame(InputParameters *pParams, NvMediaVideoSurface *pVideoSurface,
                   NvMediaRect *sourceRect);

#endif //END OF _NVMVIDEO_ENCODE_H_
