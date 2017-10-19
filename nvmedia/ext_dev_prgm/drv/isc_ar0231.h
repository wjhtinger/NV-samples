/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_AR0231_H_
#define _ISC_AR0231_H_

#include "nvmedia_isc.h"

#define AR0231_REG_PLL_VT_PIXDIV        0x302A
#define ISC_AR0231_T1_EXPOSURE_MASK     (1u << 0)
#define ISC_AR0231_T2_EXPOSURE_MASK     (1u << 1)
#define ISC_AR0231_T3_EXPOSURE_MASK     (1u << 2)
#define ISC_AR0231_ALL_EXPOSURE_MASK    (ISC_AR0231_T1_EXPOSURE_MASK  |\
                                         ISC_AR0231_T2_EXPOSURE_MASK |\
                                         ISC_AR0231_T3_EXPOSURE_MASK)
#define SIZE_FUSE_ID   16u

typedef enum {
    ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208 = 0,  //default
    ISC_CONFIG_AR0231_RESET_FRAME_ID,
    ISC_CONFIG_AR0231_ENABLE_USER_KNEE_LUT,
    ISC_CONFIG_AR0231_ENABLE_TEMP_SENSOR,
    ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1008,
    ISC_CONFIG_AR0231_ENABLE_STREAMING,
}ConfigSetsAR0231;

typedef enum {
    ISC_CONFIG_AR0231_1920X1208 = 0,   // 30 fps
    ISC_CONFIG_AR0231_1920X1008,       // 36 fps
    ISC_CONFIG_AR0231_NUM_RESOLUTION,
} ConfigResolutionAR0231;

typedef enum {
    ISC_CONFIG_AR0231_CONFIG_SET0 = 0, // internal sync
    ISC_CONFIG_AR0231_CONFIG_SET1,     // external sync
    ISC_CONFIG_AR0231_NUM_CONFIG_SET,
} ConfigSetAR0231;

typedef enum {
    ISC_READ_PARAM_CMD_AR0231_CONFIG_INFO = 0,
    ISC_READ_PARAM_CMD_AR0231_EXP_LINE_RATE,
    ISC_READ_PARAM_CMD_AR0231_FUSE_ID,
} ReadParametersCmdAR0231;

typedef enum {
    ISC_WRITE_PARAM_CMD_AR0231_CONFIG_INFO = 0,
    ISC_WRITE_PARAM_CMD_AR0231_SET_KNEE_LUT,
    ISC_WRITE_PARAM_CMD_AR0231_EXPO_MODE,
} WriteParametersCmdAR0231;

typedef struct {
    float exposureLineRate;
    int enumeratedDeviceConfig;
    ConfigResolutionAR0231 resolution;
    unsigned int frameRate;
    unsigned int sensorVersion;
    unsigned short int fineIntegTime[4];
    float maxGain;
} ConfigInfoAR0231;

typedef struct {
    union {
        ConfigInfoAR0231 *configInfo;
        unsigned char *KneeLutReg;
        unsigned int exposureModeMask;
    };
} WriteReadParametersParamAR0231;

typedef struct {
    unsigned int oscMHz;
    float maxGain;
    NvMediaISCModuleConfig moduleConfig;
    unsigned int frameRate;
    unsigned int configSetIdx;
} ContextAR0231;

typedef enum {
    EMB_PARSED = 0,
    EMB_UNPARSED,
}EmbeddedDataType;

NvMediaISCDeviceDriver *GetAR0231Driver(void);
NvMediaStatus GetAR0231ConfigSet(const char *resolution, const char *inputFormat, int *configSet);

#endif /* _ISC_AR0231_H_ */
