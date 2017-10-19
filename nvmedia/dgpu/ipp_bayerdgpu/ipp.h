/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _IPP_H_
#define _IPP_H_

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

NvMediaStatus
IPPInit(IPPCtx *ctx, TestArgs *args);

void
IPPFini(IPPCtx *ctx);

NvMediaStatus
IPPStart(IPPCtx *ctx);

NvMediaStatus
IPPStop(IPPCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif // _IPP_H_

