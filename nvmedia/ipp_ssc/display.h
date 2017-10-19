/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

NvMediaStatus
DisplayImage(IPPCtx *ctx, NvU32 pipelineNum, NvMediaImage *image);

NvMediaStatus
DisplayInit(IPPCtx *ctx);

void
DisplayFini(IPPCtx *ctx);

void
DisplayCycleCamera(IPPCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif // _DISPLAY_H_

