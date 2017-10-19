/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _DEV_PRIV_H_
#define _DEV_PRIV_H_

#include "img_dev.h"

typedef struct {
    char                       *resolution; /* resolution, ex)"1280x800" */
    NvU32                       osc; /* image device OSC value, unit is MHz.
                                      * If OSC is 24MHz, the value is 24 */
    NvU32                       frameRate; /* frame per second */
    NvU32                       pixelFrequency; /* In general, pixelFreq can be calculated as VTS x HTS x frame rate (Hz).
                                                   But in some sensors, pixelFreq may be dictated by PLL setting of PCLK. */
    NvU32                       embLinesTop; /* number of lines for emb data */
    NvU32                       embLinesBottom; /* number of lines for emb data */
    char                       *inputFormat; /* 422p(yuv8bit), 422p10(yuv10bit), raw12 */
    char                       *pixelOrder; /* bggr */
} ImgProperty;

typedef struct {
    char *name;
    ExtImgDevice *(*Init)(ExtImgDevParam *param);
    void (*Deinit)(ExtImgDevice *device);
    NvMediaStatus (*RegisterCallback)(ExtImgDevice *device, NvU32 sigNum, void (*cb)(void *), void *context);
    NvMediaStatus (*GetError)(ExtImgDevice *device, NvU32 *link, ExtImgDevFailureType *errorType);
    ImgProperty    *properties;
    NvU32          numProperties;
    NvMediaStatus (*Start)(ExtImgDevice *device);
} ImgDevDriver;

#endif /* _DEV_PRIV_H_ */
