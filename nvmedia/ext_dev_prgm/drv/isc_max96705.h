/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_MAX96705_H_
#define _ISC_MAX96705_H_

#include "nvmedia_isc.h"

#define MAX96705_DEVICE_ID    0x41 //could also be 0x43/0x51/0x53
#define CHECK_96705ID(id)     (((id & 0xED) == MAX96705_DEVICE_ID)? 1:0)

typedef enum {
    ISC_CONFIG_MAX96705_DEFAULT                   = 0,
    ISC_CONFIG_MAX96705_ENABLE_SERIAL_LINK,
    ISC_CONFIG_MAX96705_PCLKIN,
    ISC_CONFIG_MAX96705_ENABLE_REVERSE_CHANNEL,
    ISC_CONFIG_MAX96705_REGEN_VSYNC,
    ISC_CONFIG_MAX96705_SET_AUTO_CONFIG_LINK,
    ISC_CONFIG_MAX96705_DOUBLE_INPUT_MODE,
    ISC_CONFIG_MAX96705_CONFIG_SERIALIZER_COAX,
    ISC_CONFIG_MAX96705_SET_XBAR,
    ISC_CONFIG_MAX96705_SET_MAX_REMOTE_I2C_MASTER_TIMEOUT,
} ConfigSetsMAX96705;

typedef enum {
    ISC_WRITE_PARAM_CMD_MAX96705_SET_TRANSLATOR_A = 0,
    ISC_WRITE_PARAM_CMD_MAX96705_SET_TRANSLATOR_B,
    ISC_WRITE_PARAM_CMD_MAX96705_SET_DEVICE_ADDRESS,
    ISC_WRITE_PARAM_CMD_MAX96705_CONFIG_INPUT_MODE,
    ISC_WRITE_PARAM_CMD_MAX96705_SET_PREEMP,
} WriteParametersCmdMAX96705;


typedef enum {
    ISC_SET_PREEMP_MAX96705_PREEMP_OFF = 0,
    ISC_SET_PREEMP_MAX96705_NEG_1_2DB,
    ISC_SET_PREEMP_MAX96705_NEG_2_5DB,
    ISC_SET_PREEMP_MAX96705_NEG_4_1DB,
    ISC_SET_PREEMP_MAX96705_NEG_6_0DB,
    ISC_SET_PREEMP_MAX96705_PLU_1_1DB = 0x8,
    ISC_SET_PREEMP_MAX96705_PLU_2_2DB,
    ISC_SET_PREEMP_MAX96705_PLU_3_3DB,
    ISC_SET_PREEMP_MAX96705_PLU_4_4DB,
    ISC_SET_PREEMP_MAX96705_PLU_6_0DB,
    ISC_SET_PREEMP_MAX96705_PLU_8_0DB,
    ISC_SET_PREEMP_MAX96705_PLU_10_5DB,
    ISC_SET_PREEMP_MAX96705_PLU_14_0DB,
} SetPREEMPMAX96705;

typedef enum {
    ISC_READ_PARAM_CMD_MAX96705_GET_DEVICE_ADDRESS = 0,
} ReadParametersCmdMAX96705;

#define ISC_INPUT_MODE_MAX96705_DOUBLE_INPUT_MODE      1
#define ISC_INPUT_MODE_MAX96705_SINGLE_INPUT_MODE      0
#define ISC_INPUT_MODE_MAX96705_HIGH_BANDWIDTH_MODE    1
#define ISC_INPUT_MODE_MAX96705_LOW_BANDWIDTH_MODE     0
#define ISC_INPUT_MODE_MAX96705_BWS_22_BIT_MODE        0
#define ISC_INPUT_MODE_MAX96705_BWS_30_BIT_MODE        1
#define ISC_INPUT_MODE_MAX96705_PCLKIN_RISING_EDGE     0
#define ISC_INPUT_MODE_MAX96705_PCLKIN_FALLING_EDGE    1
#define ISC_INPUT_MODE_MAX96705_HVEN_ENCODING_ENABLE   1
#define ISC_INPUT_MODE_MAX96705_HVEN_ENCODING_DISABLE  0
#define ISC_INPUT_MODE_MAX96705_EDC_1_BIT_PARITY       0
#define ISC_INPUT_MODE_MAX96705_EDC_6_BIT_CRC          1
#define ISC_INPUT_MODE_MAX96705_EDC_6_BIT_HAMMING_CODE 2
#define ISC_INPUT_MODE_MAX96705_EDC_NOT_USE            3

typedef union {
    struct {
        unsigned edc : 2;
        unsigned hven : 1;
        unsigned reserved : 1;
        unsigned es : 1;
        unsigned bws : 1;
        unsigned hibw : 1;
        unsigned dbl : 1;
    } bits;
    unsigned char byte;
} ConfigureInputModeMAX96705;

typedef struct {
    union {
        struct {
            unsigned char source;
            unsigned char destination;
        } Translator;
        struct {
            unsigned char address;
        } DeviceAddress;
        ConfigureInputModeMAX96705 *inputmode;
        unsigned char preemp;
    };
} WriteReadParametersParamMAX96705;

NvMediaISCDeviceDriver *GetMAX96705Driver(void);

#endif /* _ISC_MAX96705_H_ */
