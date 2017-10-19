/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#ifndef _DEV_PROPERTY_H_
#define _DEV_PROPERTY_H_

#include "dev_priv.h"

#define IMG_PROPERTY_ENTRY(_resolution, oscMHz, fps, pclk, topEmbedded, bottomeEmbedded, _inputFormat, _pixelOrder) \
    {                                                                                                         \
        .resolution = #_resolution,                                                                           \
        .osc = oscMHz,                                                                                        \
        .frameRate = fps,                                                                                     \
        .pixelFrequency = pclk,                                                                               \
        .embLinesTop = topEmbedded,                                                                           \
        .embLinesBottom = bottomeEmbedded,                                                                    \
        .inputFormat = #_inputFormat,                                                                         \
        .pixelOrder = #_pixelOrder                                                                            \
    }

/*
 * IMG_PROPERTY_ENTRY_NO_PCLK is for the sensor module that doesn't expose VTS and HTS.
 * In this case, pixelFrequency of ExtImgDevProperty will be zero, capture component
 * won't use pixelFrequency to set CSI settle time but it will assume 204MHz as pixel Frequency.
 * For the stable CSI operation, user should use IMG_PROPERTY_ENTRY with accurate pixel frequency.
 */
#define IMG_PROPERTY_ENTRY_NO_PCLK(_resolution, oscMHz, fps, topEmbedded, bottomeEmbedded, _inputFormat, _pixelOrder) \
    {                                                                                                         \
        .resolution = #_resolution,                                                                           \
        .osc = oscMHz,                                                                                        \
        .frameRate = fps,                                                                                     \
        .pixelFrequency = 0,                                                                               \
        .embLinesTop = topEmbedded,                                                                           \
        .embLinesBottom = bottomeEmbedded,                                                                    \
        .inputFormat = #_inputFormat,                                                                         \
        .pixelOrder = #_pixelOrder                                                                            \
    }

NvMediaStatus
ImgDevSetProperty(
    ImgDevDriver *device,
    ExtImgDevParam *configParam,
    ExtImgDevice   *isc);

NvMediaStatus
ImgDevGetModuleConfig(
    NvMediaISCModuleConfig *cameraModuleCfg,
    char *captureModuleName);

#endif /* _DEV_PROPERTY_H_ */
