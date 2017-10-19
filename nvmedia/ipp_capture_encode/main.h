/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NUM_OUTPUT_STREAM_HANDLER   4
#define MAX_NUM_ENCODER                 MAX_NUM_OUTPUT_STREAM_HANDLER
#define MAX_NUM_WRITER                  MAX_NUM_OUTPUT_STREAM_HANDLER

#include "cmdline.h"

enum {
    STREAMING_ELEMENT = 0,
    DISPLAY_ELEMENT,
    ERR_HANDLER_ELEMENT,
    MAX_NUM_ELEMENTS,
};

typedef struct {
    void                        *ctxs[MAX_NUM_ELEMENTS];
    void                        *pOutputStreamHandlerContext[MAX_NUM_OUTPUT_STREAM_HANDLER];
    void                        *pImageEncoderContext[MAX_NUM_ENCODER];
    void                        *pWriterContext[MAX_NUM_WRITER];
    TestArgs                    *testArgs;
    volatile NvMediaBool         quit;
} NvMainContext;

#ifdef __cplusplus
}
#endif

#endif // _MAIN_H_
