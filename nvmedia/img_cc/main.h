/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#include "cmdline.h"

enum {
    CAPTURE_ELEMENT = 0,
    SAVE_ELEMENT,
    COMPOSITE_ELEMENT,
    DISPLAY_ELEMENT,
    GRP_ACTIVATION_ELEMENT,
    CAPTURE_STATUS_ELEMENT,
    RUNTIME_SETTINGS_ELEMENT,
    MAX_NUM_ELEMENTS,
};

typedef struct {
    void                        *ctxs[MAX_NUM_ELEMENTS];
    TestArgs                    *testArgs;
    volatile NvMediaBool         quit;
} NvMainContext;

#endif

