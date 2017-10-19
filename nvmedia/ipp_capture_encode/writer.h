/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __WRITER__
#define __WRITER__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "thread_utils.h"
#include "nvmedia_ipp.h"

void *
WriterInit(
        TestArgs                *pAllArgs,
        NvQueue                 *pInputEncodedImageQueue,
        NvU32                   streamNum,
        volatile NvMediaBool    *pQuit);

NvMediaStatus
WriterFini(void *pHandle);

NvMediaStatus
WriterStart(void *pHandle);

void
WriterStop(void *pHandle);

#ifdef __cplusplus
}
#endif

#endif // __WRITER__
