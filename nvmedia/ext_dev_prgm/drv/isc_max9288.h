/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_MAX9288_H_
#define _ISC_MAX9288_H_

#include "nvmedia_isc.h"

#define MAX9288_REG_HSYNC_VSYNC             0x14

typedef enum {
    ISC_CONFIG_MAX9288_DEFAULT                      = 0,
    ISC_CONFIG_MAX9288_ENABLE_LANES_0123,
    ISC_CONFIG_MAX9288_SET_DATA_TYPE_RGB_OLDI,
    ISC_CONFIG_MAX9288_RGB_1280x720,
    ISC_CONFIG_MAX9288_RGB_960x540,
} ConfigSetsMAX9288;

typedef enum {
    ISC_CONFIG_MAX9288_NUM_CONFIG,
} WriteParametersCmdMAX9288;

typedef struct {
    union {
    };
} WriteParametersParamMAX9288;

typedef struct {
} ContextMAX9288;

NvMediaISCDeviceDriver *GetMAX9288Driver(void);

NvMediaStatus
GetMAX9288ConfigSet(
        char *resolution,
        int *configSet);

#endif /* _ISC_MAX9288_H_ */
