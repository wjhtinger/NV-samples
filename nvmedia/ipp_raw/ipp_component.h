/*
 * Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVMEDIA_IPP_COMPONENT_H_
#define _NVMEDIA_IPP_COMPONENT_H_

#include "ipp_raw.h"
#include "buffer_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

NvMediaStatus
IPPSetCaptureSettings (
    IPPCtx *ctx,
    CaptureConfigParams *config);

// Create Raw Pipeline
NvMediaStatus IPPCreateRawPipeline(IPPCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif //_NVMEDIA_IPP_COMPONENT_H_
