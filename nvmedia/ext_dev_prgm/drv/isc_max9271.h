/*
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_MAX9271_H_
#define _ISC_MAX9271_H_

#include "nvmedia_isc.h"

#define MAX9271_DEVICE_ID                     0x09
typedef enum {
    ISC_CONFIG_MAX9271_DEFAULT                   = 0,
    ISC_CONFIG_MAX9271_ENABLE_SERIAL_LINK,
    ISC_CONFIG_MAX9271_PCLKIN,
    ISC_CONFIG_MAX9271_ENABLE_REVERSE_CHANNEL,
    ISC_CONFIG_MAX9271_INVERT_VS,
} ConfigSetsMAX9271;

typedef enum {
    ISC_WRITE_PARAM_CMD_MAX9271_SET_TRANSLATOR_A = 0,
    ISC_WRITE_PARAM_CMD_MAX9271_SET_TRANSLATOR_B,
    ISC_WRITE_PARAM_CMD_MAX9271_SET_DEVICE_ADDRESS,
    ISC_WRITE_PARAM_CMD_MAX9271_CONFIG_INPUT_MODE,
    ISC_WRITE_PARAM_CMD_MAX9271_SET_PREEMP,
} WriteParametersCmdMAX9271;

typedef enum {
    ISC_READ_PARAM_CMD_MAX9271_GET_DEVICE_ADDRESS = 0,
} ReadParametersCmdMAX9271;

typedef enum {
    ISC_SET_PREEMP_MAX9271_PREEMP_OFF = 0,
    ISC_SET_PREEMP_MAX9271_NEG_1_2DB,
    ISC_SET_PREEMP_MAX9271_NEG_2_5DB,
    ISC_SET_PREEMP_MAX9271_NEG_4_1DB,
    ISC_SET_PREEMP_MAX9271_NEG_6_0DB,
    ISC_SET_PREEMP_MAX9271_PLU_1_1DB = 0x8,
    ISC_SET_PREEMP_MAX9271_PLU_2_2DB,
    ISC_SET_PREEMP_MAX9271_PLU_3_3DB,
    ISC_SET_PREEMP_MAX9271_PLU_4_4DB,
    ISC_SET_PREEMP_MAX9271_PLU_6_0DB,
    ISC_SET_PREEMP_MAX9271_PLU_8_0DB,
    ISC_SET_PREEMP_MAX9271_PLU_10_5DB,
    ISC_SET_PREEMP_MAX9271_PLU_14_0DB,
} SetPREEMPMAX9271;

#define ISC_INPUT_MODE_MAX9271_DOUBLE_INPUT_MODE      1
#define ISC_INPUT_MODE_MAX9271_SINGLE_INPUT_MODE      0
#define ISC_INPUT_MODE_MAX9271_HIGH_DATA_RATE_MODE    0
#define ISC_INPUT_MODE_MAX9271_LOW_DATA_RATE_MODE     1
#define ISC_INPUT_MODE_MAX9271_BWS_24_BIT_MODE        0
#define ISC_INPUT_MODE_MAX9271_BWS_32_BIT_MODE        1
#define ISC_INPUT_MODE_MAX9271_PCLKIN_RISING_EDGE     0
#define ISC_INPUT_MODE_MAX9271_PCLKIN_FALLING_EDGE    1
#define ISC_INPUT_MODE_MAX9271_HVEN_ENCODING_ENABLE   1
#define ISC_INPUT_MODE_MAX9271_HVEN_ENCODING_DISABLE  0
#define ISC_INPUT_MODE_MAX9271_EDC_1_BIT_PARITY       0
#define ISC_INPUT_MODE_MAX9271_EDC_6_BIT_CRC          1
#define ISC_INPUT_MODE_MAX9271_EDC_6_BIT_HAMMING_CODE 2
#define ISC_INPUT_MODE_MAX9271_EDC_NOT_USE            3

typedef union {
    struct {
        unsigned edc : 2;
        unsigned hven : 1;
        unsigned reserved : 1;
        unsigned es : 1;
        unsigned bws : 1;
        unsigned drs : 1;
        unsigned dbl : 1;
    } bits;
    unsigned char byte;
} ConfigureInputModeMAX9271;

typedef struct {
    union {
        struct {
            unsigned char source;
            unsigned char destination;
        } Translator;
        struct {
            unsigned char address;
        } DeviceAddress;
        ConfigureInputModeMAX9271 *inputmode;
        unsigned char preemp;
    };
} WriteReadParametersParamMAX9271;

NvMediaISCDeviceDriver *GetMAX9271Driver(void);

#endif /* _ISC_MAX9271_H_ */
