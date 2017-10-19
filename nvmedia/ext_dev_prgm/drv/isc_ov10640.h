/*
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_OV10640_H_
#define _ISC_OV10640_H_

#include "nvmedia_isc.h"

#define ISC_OV10640_CHIP_ID     0xA640

#define ISC_OV10640_LONG_EXPOSURE_MASK  (1 << 0)
#define ISC_OV10640_SHORT_EXPOSURE_MASK (1 << 1)
#define ISC_OV10640_VS_EXPOSURE_MASK    (1 << 2)
#define ISC_OV10640_ALL_EXPOSURE_MASK   (ISC_OV10640_LONG_EXPOSURE_MASK  |\
                                         ISC_OV10640_SHORT_EXPOSURE_MASK |\
                                         ISC_OV10640_VS_EXPOSURE_MASK)

typedef enum {
    ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x800 = 0,
    ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x1080,
    ISC_CONFIG_OV10640_DVP_DEFAULT,
    ISC_CONFIG_OV10640_ENABLE_FSIN,
    ISC_CONFIG_OV10640_RESET_FRAME_ID,
    ISC_CONFIG_OV10640_DISABLE_LENS_SHADING,
    ISC_CONFIG_OV10640_ENABLE_STREAMING,
    ISC_CONFIG_OV10640_ENABLE_TEMP_CALIBRATION,
} ConfigSetsOV10640;

typedef enum {
    ISC_CONFIG_OV10640_1280x800 = 0,
    ISC_CONFIG_OV10640_1280x1080,
    ISC_CONFIG_OV10640_NUM_RESOLUTION_SETS
} ConfigResolutionOV10640;

enum {
    ISC_CONFIG_OV10640_HTS = 0,
    ISC_CONFIG_OV10640_VTS,
    ISC_CONFIG_OV10640_SCLK,
    ISC_CONFIG_OV10640_NUM_TIMING_INFORMATION
};

typedef enum {
    ISC_READ_PARAM_CMD_OV10640_CONFIG_INFO = 0,
    ISC_READ_PARAM_CMD_OV10640_EXP_LINE_RATE,
} ReadParametersCmdOV10640;

typedef enum {
    ISC_WRITE_PARAM_CMD_OV10640_CONFIG_INFO = 0,
    ISC_WRITE_PARAM_CMD_OV10640_EXPO_MODE,
    ISC_WRITE_PARAM_CMD_OV10640_PRE_MATRIX,
    ISC_WRITE_PARAM_CMD_OV10640_TEST_ID6,
    ISC_WRITE_PARAM_CMD_OV10640_TEST_ID7,
} WriteParametersCmdOV10640;

typedef struct {
    unsigned char cg_gain_reg;
    float exposureLineRate;
    unsigned int vts;
    int enumeratedDeviceConfig;
    ConfigResolutionOV10640 resolution;
} ConfigInfoOV10640;

typedef struct {
    NvMediaBool enable;
    float       arr[3][3];
} ShortChannelPreMatrix;

typedef struct {
    union {
        ConfigInfoOV10640 *configInfo;
        unsigned int exposureModeMask;
        ShortChannelPreMatrix *matrix;
    };
} WriteReadParametersParamOV10640;

typedef struct {
    unsigned int oscMHz;
    NvMediaISCModuleConfig moduleConfig;
} ContextOV10640;


NvMediaISCDeviceDriver *GetOV10640Driver(void);
NvMediaStatus
GetOV10640ConfigSet(
    char *resolution,
    char *inputFormat,
    int *configSet
);

#endif /* _ISC_OV10640_H_ */
