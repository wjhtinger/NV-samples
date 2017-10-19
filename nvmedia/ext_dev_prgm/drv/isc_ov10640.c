/*
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "nvmedia_isc.h"
#include "isc_ov10640.h"
#include "isc_ov10640_setting.h"
#include "log_utils.h"

#define REGISTER_ADDRESS_BYTES  2
#define REG_WRITE_BUFFER        256

#define EXPOSURE_LONG           0x0510

#define EXPO_L_H                0x30E6
#define EXPO_L_L                0x30E7
#define EXPO_S_H                0x30E8
#define EXPO_S_L                0x30E9
#define EXPO_VS                 0x30EA
#define CG_AGAIN                0x30EB
#define EXP_REG_BLOCK_SIZE      (CG_AGAIN - EXPO_L_H + 1)

#define DGAIN_L_H               0x30EC
#define DGAIN_L_L               0x30ED
#define DGAIN_S_H               0x30EE
#define DGAIN_S_L               0x30EF
#define DGAIN_VS_H              0x30F0
#define DGAIN_VS_L              0x30F1
#define DGAIN_REG_BLOCK_SIZE    (DGAIN_VS_L - DGAIN_L_H + 1)

#define INTERFACE_CTRL          0x3119
#define TMP_02                  0x304C
#define BLC_TARGET_L_H          0x30C3
#define DARK_CURRENT_L_H        0x30D0
#define ROW_AVERAG_L_H          0x30D6
#define INTERFACE_CTRL          0x3119

#define FCNT_3                  0x309C
#define FCNT_2                  0x309D
#define FCNT_1                  0x309E
#define FCNT_0                  0x309F

#define LOG_MOD_BIAS            0x31BE

#define GROUP_MODE              0x8000
#define GROUP_CTRL              0x302C
#define OPERATION_CTRL          0x302F
#define R_GAIN_L_I_H            0x31C3
#define R_R_OFFSET_L_I_H        0x31DB
#define AWB_REG_BLOCK_SIZE      60

#define MAX_REG_UPDATE_LIST     AWB_REG_BLOCK_SIZE

#define R_COMBINE_CONTROL       0x3132
#define S_CHANNEL_MATRIX        0x317A

#define TOP_LINE_ADDRESS_BASE    0x3000
#define BOTTOM_LINE_ADDRESS_BASE 0x4000
#define SENSOR_FRAME_ID_ADDRESS 0x300D
#define SENSOR_FRAME_ID_OFFSET  0xD

#define C_BLC_DEFAULT           0x80

#define TEST_PATTERN_REG        0x3129

#define OPT_BUFFER_DATA_A       0x34A8
#define MANUAL_BUFFER_ADDRESS   0x3496
#define MANUAL_READ_MODE        0X3495
#define OTP_READ                0x349C
#define SENSOR_OFFSET_REG       0X303c
#define SENSOR_TEMP_REG         0x304C

#if !defined(__INTEGRITY)
#define MIN(a,b)            (((a) < (b)) ? (a) : (b))
#define MAX(a,b)            (((a) > (b)) ? (a) : (b))
#endif
#define GET_SIZE(x)         sizeof(x)
#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   ((unsigned char*)&x[1])
#define SET_NEXT_BLOCK(x)   x += (x[0] + 1)

#define Y_BLACK_BIG             4080
#define Y_DARK_SMALL            15
#define ANALOG_GAINS            4
#define Y_BLACK_LIMIT           2900
#define HIGH_TEMP_ANALOG_GAIN   85

#define MEMBIST_ID_REG          0x7F00
#define SRAMBIST_CTRL_REG       0x7F01
#define POWER_ON_SELFTEST_REG   0x3016
#define STANDBY_REG             0x3012

#define EXPOSURE_PRINT(args...)
//#define EXPOSURE_PRINT(args...) printf(args)

//#define DECOMPOSITION_LOG
#ifdef DECOMPOSITION_LOG
#define DECOMPOSITION_PRINT(args...) printf(args)
#else
#define DECOMPOSITION_PRINT(args...)
#endif

#define REG_UPDATE(regList, regAddress, regBaseAddress, data) \
    { \
        (regList).regValue[(regAddress) - (regBaseAddress)] = (data); \
        (regList).regUpdated[(regAddress) - (regBaseAddress)] = 1; \
    }

typedef struct {
    unsigned char regValue[MAX_REG_UPDATE_LIST];
    char regUpdated[MAX_REG_UPDATE_LIST];
} RegUpdateList;

const int c_adc[9] = { 0, 126, 312, 0, 560, 0, 0, 0, 1056 };
const float sensor_min_gain[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = { 1.35, 3.00, 1.35 };
const float conv_gain[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = { 2.57, 1.00, 2.57 };
const float max_analog_gain[2][NVMEDIA_ISC_EXPOSURE_MODE_MAX] =
    {
        { 4.0, 4.0, 4.0 },  // Normal temperature
        { 4.0, 4.0, 4.0 },  // High temperature (> 85 C)
    };
#ifdef DECOMPOSITION_LOG
    const float y_white[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = { 3500.0, 1600.0, 3500.0 };
#endif

typedef struct {
    NvMediaISCSupportFunctions *funcs;
    const unsigned char *default_setting;
    const unsigned char **sensor_settings;
    const unsigned char **sensor_pll_settings;
    ConfigInfoOV10640 config_info;
    int temperature;
    int temperatureLast;
    int temperatureDir;
    int temperatureDirCount;
    unsigned int c_blc[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    unsigned int y_dark[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    unsigned int y_black[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    float minGain[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    NvMediaISCModuleConfig moduleCfg;
    unsigned int exposureModeMask;
} _DriverHandle;

static const unsigned char ov10640_sync[] = {
    'w', 3, 0x30, 0x8c, 0xb3,
    'w', 3, 0x30, 0x93, 0x01, // row reset to 1. Default value is 0. Without this
                              // change when FSIN comes a new frame will start,
                              // causing wrong internal timing and sensor frame
                              // counter jump. Bug 1764950
    'w', 3, 0x30, 0x8a, 0x10,
    'e'
};

// Maximum possible exposure time in nanoseconds, Order - L, S, VS
static const float maxET[ISC_CONFIG_OV10640_NUM_RESOLUTION_SETS][NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {
    // ISC_CONFIG_OV10640_1280x800
    {33096111, 33096111, 160183},
    // ISC_CONFIG_OV10640_1280x1080
    {33154000, 33154000, 119618}
};

// Mininum possible exposure time in nanoseconds, Order - L, S, VS
static const float minET[ISC_CONFIG_OV10640_NUM_RESOLUTION_SETS][NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {
    // ISC_CONFIG_OV10640_1280x800
    {40361, 40361, 22703},
    // ISC_CONFIG_OV10640_1280x1080
    {30140, 30140, 16954}
};

// Maximum possible gain values, Order - L, S, VS
static const float maxGain[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {444.096, 252.0, 444.096};

static const unsigned int
ov10640_timing_information[ISC_CONFIG_OV10640_NUM_RESOLUTION_SETS][ISC_CONFIG_OV10640_NUM_TIMING_INFORMATION] = {
    {1453, 826, 36},
    {1507, 1106, 50},
};

static const unsigned char ov10640_reset_frame_id[] = {
    3, 0x30, 0x0d, 0x00
};

static const unsigned char ov10640_disable_lens_shading[] = {
    3, 0x31, 0x27, 0x43
};

static NvMediaStatus
WriteRegister(NvMediaISCDriverHandle *handle, NvMediaISCTransactionHandle *transaction,
        unsigned int registerNum, unsigned int dataLength, unsigned char *dataBuff);

static NvMediaStatus
ReadRegister(NvMediaISCDriverHandle *handle, NvMediaISCTransactionHandle *transaction,
        unsigned int registerNum, unsigned int dataLength, unsigned char *dataBuff);

static NvMediaStatus
CalculateExposureLineRate(
    unsigned int enumeratedDeviceConfig,
    float *lineRate,
    ConfigResolutionOV10640 *pResolution)
{
    unsigned int hts, sclk;
    const unsigned int *resolutionSet;
    ConfigResolutionOV10640 resolution;

    if(!lineRate)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x800:
            resolution = ISC_CONFIG_OV10640_1280x800;
            break;
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x1080:
            resolution = ISC_CONFIG_OV10640_1280x1080;
            break;
        default:
            return NVMEDIA_STATUS_BAD_PARAMETER;
    }
    *pResolution = resolution;
    resolutionSet = ov10640_timing_information[resolution];

    hts = resolutionSet[ISC_CONFIG_OV10640_HTS];
    sclk = resolutionSet[ISC_CONFIG_OV10640_SCLK];

    // Multiply by 1000000 since exposure time will be in seconds
    *lineRate = (float)sclk / hts * 1000000;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DriverCreate(
    NvMediaISCDriverHandle **handle,
    NvMediaISCSupportFunctions *supportFunctions,
    void *clientContext)
{
    _DriverHandle *driverHandle;
    ContextOV10640 *ctx;
    int i;
    unsigned int lenCfgName;

    if(!handle || !supportFunctions)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    driverHandle = calloc(1, sizeof(_DriverHandle));
    if(!driverHandle)
        return NVMEDIA_STATUS_OUT_OF_MEMORY;

    driverHandle->funcs = supportFunctions;
    *handle = (NvMediaISCDriverHandle *)driverHandle;

    driverHandle->default_setting = ov10640_default;
    driverHandle->sensor_settings = ov10640_settings;
    driverHandle->sensor_pll_settings = ov10640_pll_settings[ISC_OV10640_CLK_SRC_OSC24];

    if(clientContext) {
        ctx = clientContext;
        switch(ctx->oscMHz) {
            case 25:
                driverHandle->sensor_pll_settings = ov10640_pll_settings[ISC_OV10640_CLK_SRC_OSC25];
                break;
            default:
            case 24:
                break;
        }

        lenCfgName = strlen(ctx->moduleConfig.cameraModuleCfgName);
        if(sizeof(driverHandle->moduleCfg.cameraModuleCfgName) > lenCfgName)
            strncpy(driverHandle->moduleCfg.cameraModuleCfgName,
                    ctx->moduleConfig.cameraModuleCfgName,
                    lenCfgName);
        else
            return NVMEDIA_STATUS_OUT_OF_MEMORY;

        driverHandle->moduleCfg.cameraModuleConfigPass1 = ctx->moduleConfig.cameraModuleConfigPass1;
        driverHandle->moduleCfg.cameraModuleConfigPass2 = ctx->moduleConfig.cameraModuleConfigPass2;
    }

    // Set defaults
    for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        driverHandle->minGain[i] = sensor_min_gain[i];
        driverHandle->c_blc[i] = C_BLC_DEFAULT;
    }

    driverHandle->exposureModeMask = ISC_OV10640_ALL_EXPOSURE_MASK;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DriverDestroy(
    NvMediaISCDriverHandle *handle)
{
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    free(handle);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
EnableLink(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int instanceNumber,
    NvMediaBool enable)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaStatus
WriteArray(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int arrayByteLength,
    const unsigned char *arrayData)
{
    NvMediaISCSupportFunctions *funcs;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    while(arrayByteLength) {
        funcs->Write(
            transaction,                 // transaction
            GET_BLOCK_LENGTH(arrayData), // dataLength
            GET_BLOCK_DATA(arrayData));  // data

        if((arrayData[1] == 0x30) && (arrayData[2] == 0x13))
            usleep(10000); // wait for 10ms

        arrayByteLength -= GET_BLOCK_LENGTH(arrayData) + 1;
        SET_NEXT_BLOCK(arrayData);
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
WriteArrayWithCommand(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    const unsigned char *arrayData)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;
    while(arrayData[0] != 'e') {
        switch (arrayData[0]) {
            case 'w':
                status = funcs->Write(
                    transaction,                 // transaction
                    arrayData[1], // dataLength
                    (unsigned char*)&arrayData[2]);
#ifdef PRINT_LOG
                {
                    unsigned char data[1] = {0};

                    usleep(250);

                    status = funcs->Read(
                        transaction,                // transaction
                        REGISTER_ADDRESS_BYTES,     // regLength
                        (unsigned char*)&arrayData[2],  // regData
                        1,                          // dataLength
                        data);                      // dataBuff

                    printf("++ %.2X%.2X %x\n", (unsigned int)arrayData[2],
                        (unsigned int)arrayData[3],
                        data[0]);
                }
#endif
                arrayData += (arrayData[1] + 2);

                break;
            case 'd':
                usleep((arrayData[1] << 8) + arrayData[2]);
                arrayData += 3;
                break;
            default:
                break;
        }

        if(status != NVMEDIA_STATUS_OK) {
            break;
        }

    }

    return status;
}

static NvMediaStatus
SetDefaults(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    return WriteArrayWithCommand(
                handle,
                transaction,
                ((_DriverHandle *)handle)->default_setting);
}

static NvMediaStatus
TempSensorCalibration(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status;
    unsigned char buff[2];
    unsigned short otp_offset;
    unsigned short sensor_offset;

    // Clear OTP register buffer
    buff[0] = 0x00;
    status = WriteRegister(handle, transaction, OPT_BUFFER_DATA_A, 1, buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }

    // Set start and end manual register buffer address range
    buff[0] = 0x0a;
    buff[1] = 0x0a;
    status = WriteRegister(handle, transaction, MANUAL_BUFFER_ADDRESS, 2, buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }

    // Set manual read mode by setting MANUAL_READ_MODE[0] (OTP bank 0)
    buff[0] = 0x40;
    status = WriteRegister(handle, transaction, MANUAL_READ_MODE, 1, buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }

    // Set read strobe to load register buffer value
    buff[0] = 0x01;
    status = WriteRegister(handle, transaction, OTP_READ, 1, buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }

    usleep(25000);    // wait for 25 ms

    // Read OTP register buffer value
    status = ReadRegister(handle, transaction, OPT_BUFFER_DATA_A, 1, buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }
    otp_offset = buff[0];

    // Read sensor temperature offset value
    status = ReadRegister(handle, transaction, SENSOR_OFFSET_REG, 2, buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }
    sensor_offset = (buff[0] << 8) | buff[1];
    // If otp_offset value is negative, convert it to 2's complement
    if ((otp_offset & 0xf0) >= 0x80) {
        otp_offset = (0xff - otp_offset + 1);
        sensor_offset = sensor_offset - (otp_offset << 1);
    } else {
        sensor_offset = sensor_offset + (otp_offset << 1);
    }
    buff[1] = (sensor_offset & 0xFF);
    buff[0] = (sensor_offset >> 8);
    // Set the new offset value to the sensor temperaure offset register
    status = WriteRegister(handle, transaction, SENSOR_OFFSET_REG, 2, buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }
    return status;
}

static NvMediaStatus
SetDeviceConfig(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int enumeratedDeviceConfig)
{
    NvMediaStatus status;

    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x800:
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x1080:
            ((_DriverHandle *)handle)->config_info.enumeratedDeviceConfig =
                     enumeratedDeviceConfig;
            status = CalculateExposureLineRate(enumeratedDeviceConfig,
                     &((_DriverHandle *)handle)->config_info.exposureLineRate,
                     &((_DriverHandle *)handle)->config_info.resolution);
            if(status != NVMEDIA_STATUS_OK)
                return status;
            break;
    }

    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x800:
            status = WriteArrayWithCommand(
                handle,
                transaction,
                ((_DriverHandle *)handle)->sensor_pll_settings[ISC_CONFIG_OV10640_RAW12_COMP_1280x800]);
            if(status != NVMEDIA_STATUS_OK)
                return status;
            return WriteArrayWithCommand(
                handle,
                transaction,
                ((_DriverHandle *)handle)->sensor_settings[ISC_CONFIG_OV10640_RAW12_COMP_1280x800]);
        case ISC_CONFIG_OV10640_DVP_DEFAULT:
            return WriteArrayWithCommand(
                handle,
                transaction,
                ((_DriverHandle *)handle)->default_setting);
        case ISC_CONFIG_OV10640_ENABLE_FSIN:
            return WriteArrayWithCommand(
                handle,
                transaction,
                ov10640_sync);
        case ISC_CONFIG_OV10640_RESET_FRAME_ID:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(ov10640_reset_frame_id),
                ov10640_reset_frame_id);
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x1080:
            status = WriteArrayWithCommand(
                handle,
                transaction,
                ((_DriverHandle *)handle)->sensor_pll_settings[ISC_CONFIG_OV10640_RAW12_COMP_1280x1080]);
            if(status != NVMEDIA_STATUS_OK)
                return status;
            return WriteArrayWithCommand(
                handle,
                transaction,
                ((_DriverHandle *)handle)->sensor_settings[ISC_CONFIG_OV10640_RAW12_COMP_1280x1080]);
        case ISC_CONFIG_OV10640_DISABLE_LENS_SHADING:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(ov10640_disable_lens_shading),
                ov10640_disable_lens_shading);
        case ISC_CONFIG_OV10640_ENABLE_STREAMING:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(ov10640_enable_streaming),
                ov10640_enable_streaming);
        case ISC_CONFIG_OV10640_ENABLE_TEMP_CALIBRATION:
            return TempSensorCalibration(handle, transaction);
        default:
            break;
    }

    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaStatus
SetConfigInfo(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        ConfigInfoOV10640 *configInfo)
{
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    /* copy configInfo to current handle */
    memcpy(&((_DriverHandle *)handle)->config_info, configInfo, sizeof(ConfigInfoOV10640));

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetConfigInfo(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        ConfigInfoOV10640 *configInfo)
{
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    /* copy config info of current handle to input param */
    memcpy(configInfo, &((_DriverHandle *)handle)->config_info, sizeof(ConfigInfoOV10640));

    return NVMEDIA_STATUS_OK;
}

/* Set exposure mode based on the mask, only support single exposure
   and full exposure mode, otherwise, return error
 */
static NvMediaStatus
SetExposureMode(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int exposureModeMask)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    unsigned char reg_data;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if (!handle || !transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if (exposureModeMask == driverHandle->exposureModeMask) {
        // No change, just return
        return NVMEDIA_STATUS_OK;
    }

    reg_data = 0x44;  //value in 0x3119

    switch (exposureModeMask) {
        case ISC_OV10640_ALL_EXPOSURE_MASK:
            break;
        case ISC_OV10640_LONG_EXPOSURE_MASK:
            reg_data |= 0x01;
            break;
        case ISC_OV10640_SHORT_EXPOSURE_MASK:
            reg_data |= 0x02;
            break;
        case ISC_OV10640_VS_EXPOSURE_MASK:
            reg_data |= 0x03;
            break;
        default:
            return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    status = WriteRegister(handle, transaction, INTERFACE_CTRL, 1, &reg_data);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }

    driverHandle->exposureModeMask = exposureModeMask;

    if (!(exposureModeMask & ISC_OV10640_LONG_EXPOSURE_MASK)) {
        // If long exposure mode is disable, need to set max exposure time
        // to long exposure to make sure short or VS exposure work correctly
        unsigned int exposure_fix_16_5;

        // Convert to 16.5 fix point format
        exposure_fix_16_5 = (unsigned int)(EXPOSURE_LONG * 32.0 + 0.5);

        reg_data = (exposure_fix_16_5 >> 13) & 0xFF;
        status = WriteRegister(handle, transaction, EXPO_L_H, 1, &reg_data);
        if (status != NVMEDIA_STATUS_OK) {
            return status;
        }

        reg_data = (exposure_fix_16_5 >> 5) & 0xFF;
        status = WriteRegister(handle, transaction, EXPO_L_L, 1, &reg_data);
        if (status != NVMEDIA_STATUS_OK) {
            return status;
        }
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetPreMatrix(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        ShortChannelPreMatrix *matrix)
{
    NvMediaStatus status;

    if (!handle || !transaction || !matrix)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if (matrix->enable) {
        int value;
        unsigned int i, j;
        unsigned int address = 0;
        unsigned char buff[18];

        status = ReadRegister(handle, transaction, R_COMBINE_CONTROL, 1, buff);
        if (status != NVMEDIA_STATUS_OK) {
            return status;
        }

        buff[0] = buff[0] | (1 << 3);
        status = WriteRegister(handle, transaction, R_COMBINE_CONTROL, 1, buff);
        if (status != NVMEDIA_STATUS_OK) {
            return status;
        }

        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                value = (int) (matrix->arr[i][j] * 256);
                buff[address++] = (unsigned char) ((value & 0x300) >> 8);
                buff[address++] = (unsigned char) (value & 0xFF);
            }
        }

        status = WriteRegister(handle, transaction, S_CHANNEL_MATRIX, 18, buff);
        if (status != NVMEDIA_STATUS_OK) {
            return status;
        }
    }

    return NVMEDIA_STATUS_OK;
}


static NvMediaStatus
SimpleOperationTest(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status;
    unsigned char buff = 0x00;
    unsigned int i = 0;

    if (!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    // Enable all BIST wrappers by writing all addresses to membist ID reg
    buff = 0xFF;
    status = WriteRegister(handle, transaction, MEMBIST_ID_REG, 1, &buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }
    // Write 1 to the start bit(Bit[0]) of control register
    buff = 0x1;
    status = WriteRegister(handle, transaction, SRAMBIST_CTRL_REG, 1, &buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }
    // Poll until start bit(Bit[0]) and error bit(Bit[1]) of control reg are 0
    while ((buff & 0x3) != 0 && i < 10) {
        status = ReadRegister(handle, transaction, SRAMBIST_CTRL_REG, 1, &buff);
        if (status != NVMEDIA_STATUS_OK) {
            return status;
        }
        i++;
    }
    if ((buff & 0x3) != 0) {
        return NVMEDIA_STATUS_ERROR;
    }
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
RetentionTest(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status;
    unsigned int i = 0;
    unsigned char buff = 0x00;

    if (!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    // Enable all BIST wrappers by writing all addresses to membist ID reg
    buff = 0xFF;
    status = WriteRegister(handle, transaction, MEMBIST_ID_REG, 1, &buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }
    // Enable start bit(Bit[0]) and retention bit(Bit[2]) of control reg
    buff = 0x05;
    status = WriteRegister(handle, transaction, SRAMBIST_CTRL_REG, 1, &buff);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }
    // While retention bit(Bit[2]) is 1, poll until start bit(Bit[0]) and
    // error bit(Bit[1]) of control reg are 0
    while (((buff & 0x7) != 0x4) && i < 10) {
        status = ReadRegister(handle, transaction, SRAMBIST_CTRL_REG, 1, &buff);
        if (status != NVMEDIA_STATUS_OK) {
            return status;
        }
        i++;
    }
    if ((buff & 0x7) != 0x4) {
        return NVMEDIA_STATUS_ERROR;
    } else {
        // Set start bit(Bit[0]) of control reg to 1
        buff = 0x1;
        status = WriteRegister(handle, transaction, SRAMBIST_CTRL_REG, 1, &buff);
        if (status != NVMEDIA_STATUS_OK) {
            return status;
        }
    }
    return status;
}

static NvMediaStatus
WriteParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        unsigned int parameterSize,
        void *parameter)
{
    WriteReadParametersParamOV10640 *param;
    NvMediaStatus status;
    unsigned char buff;
    unsigned int *testPattern;

    if(!parameter)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    param = (WriteReadParametersParamOV10640 *)parameter;

    switch(parameterType) {
        case ISC_WRITE_PARAM_CMD_OV10640_CONFIG_INFO:
            if(parameterSize != sizeof(param->configInfo))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetConfigInfo(
                handle,
                transaction,
                param->configInfo);
        case ISC_WRITE_PARAM_CMD_OV10640_EXPO_MODE:
            if(parameterSize != sizeof(param->exposureModeMask))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetExposureMode(
                handle,
                transaction,
                param->exposureModeMask);
        case ISC_WRITE_PARAM_CMD_OV10640_PRE_MATRIX:
            if(parameterSize != sizeof(param->matrix))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetPreMatrix(
                handle,
                transaction,
                param->matrix);
        case ISC_WRITE_PARAM_CMD_OV10640_TEST_ID6:
            if(parameterSize != sizeof(param->configInfo))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            // Refer to OV10640 Safety manual for "ID6-Memory BIST"
            LOG_DBG("Enable streaming mode for SRAM BIST \n");
            buff = 0x1;
            status = WriteRegister(handle, transaction, STANDBY_REG, 1, &buff);
            if (status != NVMEDIA_STATUS_OK) {
                return status;
            }
            // SRAM BIST
            status = SimpleOperationTest(handle, transaction);
            if (status != NVMEDIA_STATUS_OK) {
                return status;
            }
            status = RetentionTest(handle, transaction);
            if (status != NVMEDIA_STATUS_OK) {
                return status;
            }
            LOG_DBG("Test ID6 SRAM BIST successful \n");
            LOG_DBG("Disable streaming mode after SRAM BIST \n");
            buff = 0x0;
            status = WriteRegister(handle, transaction, STANDBY_REG, 1, &buff);
            if (status != NVMEDIA_STATUS_OK) {
                return status;
            }

            // ROM BIST
            status = ReadRegister(handle, transaction, POWER_ON_SELFTEST_REG, 1, &buff);
            if (status != NVMEDIA_STATUS_OK) {
                return status;
            }
            // If ROM loader bit(Bit[0]) or AEC ROM bit(Bit[1]) is low, return error
            if ((buff & 0x3) != 0x3 ) {
                return NVMEDIA_STATUS_ERROR;
            }
            LOG_DBG("Test ID6 ROM BIST successful \n");
            break;
        case ISC_WRITE_PARAM_CMD_OV10640_TEST_ID7:
            testPattern = (unsigned int *) parameter;
            if (*testPattern == 0) {
                // Write 0 to disable digital test pattern
                buff = 0x00;
           } else {
                // Write 1 to Bit[7] to enable digital test pattern
                buff = 0x80;
           }
           return WriteRegister(handle, transaction, TEST_PATTERN_REG, 1, &buff);
        default:
            break;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
ReadParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        unsigned int parameterSize,
        void *parameter)
{
    WriteReadParametersParamOV10640 *param;

    if(!parameter)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    param = (WriteReadParametersParamOV10640 *)parameter;

    switch(parameterType) {
        case ISC_READ_PARAM_CMD_OV10640_CONFIG_INFO:
            if(parameterSize != sizeof(param->configInfo))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return GetConfigInfo(
                handle,
                transaction,
                param->configInfo);
        case ISC_READ_PARAM_CMD_OV10640_EXP_LINE_RATE:
            if(parameterSize != sizeof(param->configInfo))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return CalculateExposureLineRate(param->configInfo->enumeratedDeviceConfig,
                                &param->configInfo->exposureLineRate,
                                &param->configInfo->resolution);
        default:
            break;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
ReadRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int registerNum,
    unsigned int dataLength,
    unsigned char *dataBuff)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char registerData[REGISTER_ADDRESS_BYTES];
    NvMediaStatus status;

    if(!handle || !dataBuff)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    registerData[0] = registerNum >> 8;//((registerNum & 0xff00) >> 8);
    registerData[1] = (registerNum & 0xFF);
    status = funcs->Read(
        transaction,    // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        registerData,   // regData
        dataLength,     // dataLength
        dataBuff);      // data
    if(status != NVMEDIA_STATUS_OK) {
        status = funcs->Read( // workaround for Bug 200287337
            transaction,
            REGISTER_ADDRESS_BYTES,
            registerData,
            dataLength,
            dataBuff);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_WARN("%s: failed to read from 0x%x, length %d\n", __func__, registerNum, dataLength);
        }
    }
    return status;
}

static NvMediaStatus
WriteRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int registerNum,
    unsigned int dataLength,
    unsigned char *dataBuff)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[REGISTER_ADDRESS_BYTES + REG_WRITE_BUFFER];
    NvMediaStatus status;

    if(!handle || !dataBuff)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = registerNum >> 8;
    data[1] = registerNum & 0xFF;
    memcpy(&data[2], dataBuff, MIN(REG_WRITE_BUFFER, dataLength));

    status = funcs->Write(
        transaction,                         // transaction
        dataLength + REGISTER_ADDRESS_BYTES, // dataLength
        data);                               // data
    if(status != NVMEDIA_STATUS_OK) {
        status = funcs->Write( // workaround for Bug 200287337
            transaction,
            dataLength + REGISTER_ADDRESS_BYTES,
            data);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_WARN("%s: failed write to 0x%x, length %d\n", __func__, registerNum, dataLength);
        }
    }
    return status;
}

static NvMediaStatus
GetTemperature(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    float *temperature)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned int reg = 0x304c;
    unsigned char buff = 0x00;

    status = ReadRegister(handle,
                          transaction,
                          reg,
                          1,
                          &buff);
    if(status != NVMEDIA_STATUS_OK)
        return status;

    if(buff > 0xc0)
        *temperature = buff - 0x100;
    else
        *temperature = buff;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
CheckPresence(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned int reg;
    unsigned char readBuff[2] = {0};

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    reg = 0x300a;
    status = ReadRegister(handle,
                          transaction,
                          reg,
                          2,
                          readBuff);

    if(status != NVMEDIA_STATUS_OK)
        return status;

    if((readBuff[0] != ((ISC_OV10640_CHIP_ID >> 8) & 0xff)) ||
        (readBuff[1] != (ISC_OV10640_CHIP_ID & 0xff))) {
        return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DumpRegisters(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char data[1] = {0};
    unsigned char *arrayData;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;
    arrayData = (unsigned char*)((_DriverHandle *)handle)->default_setting;

    while(arrayData[0] != 'e') {
        if(arrayData[0] == 'w') {
            status = funcs->Read(
                transaction,                // transaction
                REGISTER_ADDRESS_BYTES,     // regLength
                &arrayData[2],  // regData
                1,                          // dataLength
                data);                      // dataBuff

            printf("60 %.2X%.2X %.2x\n", (unsigned int)arrayData[2],
                (unsigned int)arrayData[3],
                data[0]);
            arrayData += (arrayData[1] + 2);
        } else {
            arrayData += 2;
        }
    }

    return status;
}

static NvMediaStatus
WriteRegUpdateList(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int registerBase,
    unsigned int regListSize,
    RegUpdateList *regList,
    NvMediaBool groupHold)
{
    NvMediaStatus status;
    unsigned int regWriteCount = 0;
    unsigned char reg_data;
    unsigned int startIdx = 0, i;
    unsigned int regAddrStart;

    if (!regList) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if (groupHold) {
        // Group hold
        reg_data = 0x00;
        status = WriteRegister(handle, transaction, GROUP_CTRL, 1, &reg_data);
        if (status != NVMEDIA_STATUS_OK)
            return status;
    }

    while (startIdx < regListSize) {
        // Find first changed data
        if (regList->regUpdated[startIdx]) {
            // Find continous block size
            regWriteCount = 0;

            for (i = startIdx; i < regListSize; i++) {
                if (!regList->regUpdated[i]) {
                    // Stop at the first not updated data
                    break;
                }
                regWriteCount++;
            }

            // Write block out if it has data
            if (regWriteCount) {
                regAddrStart = startIdx + registerBase;

                if (groupHold) {
                    // Add group mode register flag
                    regAddrStart |= GROUP_MODE;
                }

                status = WriteRegister(handle, transaction, regAddrStart,
                                       regWriteCount, &regList->regValue[startIdx]);
                if (status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: I2C write error: num_bytes: %d data: ", __func__, regWriteCount);
                    for (i = 0; i < regWriteCount; i++) {
                        printf("%02X ", regList->regValue[startIdx + i]);
                    }
                    printf("\n");

                    return status;
                }
            }

            startIdx += regWriteCount;
        } else {
            startIdx++;
        }
   }

    if (groupHold) {
        // Single Group 0 Launch (After SOF mode)
        reg_data = 0x10;
        status = WriteRegister(handle, transaction, GROUP_CTRL, 1, &reg_data);
        if (status != NVMEDIA_STATUS_OK)
            return status;

        // Group launch - Single mode
        reg_data = 0x01;
        status = WriteRegister(handle, transaction, OPERATION_CTRL, 1, &reg_data);
        if (status != NVMEDIA_STATUS_OK)
            return status;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DecomposeGains(
    NvMediaISCDriverHandle *handle,
    NvMediaISCExposureControl *exposureControl,
    float *digitalGain
)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    struct {
        float analogGain;
        unsigned char reg_bits;
    } again_translate[4] =
    {
        { 8.0, 3 },
        { 4.0, 2 },
        { 2.0, 1 },
        { 1.0, 0 }
    };
    int i, j, k, index;
    float sensorGain[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    int y_black[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    float gain_dig_min[ANALOG_GAINS][NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    int gain_analog = 1;
    float analogGain, conversionGain = 1.0, sensor_gain = 0.0;
    unsigned char cg_bits = 0;
    int analogGainIndex, tempIndex, invalidGain = 0;

#ifdef DECOMPOSITION_LOG
    float gain_extra_min[ANALOG_GAINS][NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    char *exposures[] = { "Long", "Short", "Very Short" };
#endif

    memset(&sensorGain, 0, sizeof(sensorGain));
    index = 0;
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if (driverHandle->exposureModeMask & (1 << i)) {
            if (exposureControl->sensorGain[index].valid) {
                sensorGain[i] = exposureControl->sensorGain[index].value;
            }
            index++;
        }
    }

    // Refer to "Implementation Guide_AN_1.1" for "ADC Range and minimum gain"
    // Calulate minimum gains for each analog gain
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
         unsigned int y_dark = driverHandle->y_dark[i];

        // Adjust y_black value. At low temperature y_black can be a big number
        // e.g. 0xFFE. In this case the y_dark has to be checked and if it is a small
        // number then y_black = y_black - 4096 so it can be negative.
        if(driverHandle->y_black[i] > Y_BLACK_BIG) {
            y_black[i] = (y_dark < Y_DARK_SMALL || y_dark > (4095 - Y_DARK_SMALL)) ?
                ((int)driverHandle->y_black[i] - 4096) : (int)driverHandle->y_black[i];
        } else {
            y_black[i] =  (int)driverHandle->y_black[i];
        }

        // Limit y_black to avoid negative gains
        if(y_black[i] > Y_BLACK_LIMIT) {
            y_black[i] = Y_BLACK_LIMIT;
        }
        gain_analog = 1;
        for(j = 0; j < ANALOG_GAINS; j++) {
            // Calculate digital minimum gain
            gain_dig_min[j][i] = (float)(4095 - driverHandle->c_blc[i]) /
                (4095 - (y_black[i] + c_adc[gain_analog]));

            // Make sure that the minimum digital gain is not lower than
            // the allowed L=1.35, S=3.0, VS=1.35 minimum digital gain values
            gain_dig_min[j][i] = MAX(gain_dig_min[j][i], sensor_min_gain[i]);

#ifdef DECOMPOSITION_LOG
            // Calculate extra gain in order to fill the available pixel range
            gain_extra_min[j][i] = (float)(4095 - driverHandle->c_blc[i]) /
                (y_white[i] * gain_dig_min[j][i]);
#endif

            // Set next analog gain
            gain_analog <<= 1;
        }

        // Set the minimum gain for AE algorithm
        driverHandle->minGain[i] = gain_dig_min[0][i];
    }

#ifdef DECOMPOSITION_LOG
    DECOMPOSITION_PRINT("Temperature: %d\n", driverHandle->temperature);
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        gain_analog = 1;
        for(j = 0; j < ANALOG_GAINS; j++) {
            DECOMPOSITION_PRINT("%s exposure ag: %d : y_dark: %d (0x%X) y_black: %d (0x%X) ==> y_black: %d dg_min: %f g_em: %f min_gain: %f\n",
                exposures[i], gain_analog,
                driverHandle->y_dark[i], driverHandle->y_dark[i],
                driverHandle->y_black[i], driverHandle->y_black[i],
                y_black[i], gain_dig_min[j][i], gain_extra_min[j][i],
                driverHandle->minGain[i]);
            gain_analog <<= 1;
        }
    }
    DECOMPOSITION_PRINT("\n");
#endif

    // Decompose sensor gain in analog, digital & conversion gain
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if (sensorGain[i] > 0) {
            // Set base state
            digitalGain[i] = 1.0;
            conversionGain = 1.0;
            sensor_gain = 0.0;
            cg_bits = 0;
            invalidGain = 0;

            // Check sensor gain is smaller that the allowed minimum
            if(sensorGain[i] < driverHandle->minGain[i]) {
                // Invalid sensor gain
                invalidGain = 1;
            }

            // Get temperature index
            tempIndex = (driverHandle->temperature > HIGH_TEMP_ANALOG_GAIN) ? 1 : 0;
            // Set base analog gain value
            analogGain = max_analog_gain[tempIndex][i];

            DECOMPOSITION_PRINT("Decompose: Exposure: %s Gain: %f Max analog: %f Bad gain: %d\n",
                exposures[i], sensorGain[i], analogGain, invalidGain);

            // Loop throught each analog gain starting with the highest
            while(analogGain >= 1.0) {
                int goodDecomposition = 0;

                digitalGain[i] = 1.0;
                conversionGain = 1.0;
                sensor_gain = sensorGain[i];

                analogGainIndex = (analogGain > 4.0) ? 3 : (int)(analogGain / 2.0);
                // Check that the minumum digital gain is greater than one
                // and the input gain is valid
                if(gain_dig_min[analogGainIndex][i] > 1.0 && !invalidGain) {
                    // In this case the sensor gain has to be pre-divided with
                    // this so after the decomposition there is still gain
                    // to be applied for digital
                    digitalGain[i] = gain_dig_min[analogGainIndex][i];
                    sensor_gain /= digitalGain[i];
                }

                // Decompose sensor_gain to conversion, analog and digital gain
                if(sensor_gain >= conv_gain[i]) {
                    conversionGain = conv_gain[i];
                    sensor_gain /= conversionGain;
                }
                if(sensor_gain >= analogGain) {
                    sensor_gain /= analogGain;
                    goodDecomposition = 1;
                }
                digitalGain[i] *= sensor_gain;

                DECOMPOSITION_PRINT("   Loop: Good: %d Conversion: %f Analog: %f (Index: %d) Digital: %f\n",
                    goodDecomposition, conversionGain, analogGain, analogGainIndex, digitalGain[i]);

                if(goodDecomposition)
                    break;

                // Next analog gain
                analogGain /= 2.0;
            }

            DECOMPOSITION_PRINT("   Result: Conversion: %f Analog: %f Digital: %f\n", conversionGain, analogGain, digitalGain[i]);

            // Set analog & conversion gain variable
            for (k = 0; k < 4; k++) {
                if (analogGain == again_translate[k].analogGain) {
                    cg_bits = again_translate[k].reg_bits;
                    break;
                }
            }
            switch (i) {
                case NVMEDIA_ISC_EXPOSURE_MODE_LONG:
                    driverHandle->config_info.cg_gain_reg &= 0xBC;
                    driverHandle->config_info.cg_gain_reg |= cg_bits;
                    driverHandle->config_info.cg_gain_reg |= fabs((conversionGain - 1.0) < 0.001) ? 0x00 : 0x40;
                    break;
                case NVMEDIA_ISC_EXPOSURE_MODE_SHORT:
                    driverHandle->config_info.cg_gain_reg &= 0xF3;
                    driverHandle->config_info.cg_gain_reg |= (cg_bits << 2);
                    break;
                case NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT:
                    driverHandle->config_info.cg_gain_reg &= 0x4F;
                    driverHandle->config_info.cg_gain_reg |= (cg_bits << 4);
                    driverHandle->config_info.cg_gain_reg |= fabs((conversionGain - 1.0) < 0.001) ? 0x00 : 0x80;
                    break;
            }
        }
    }
    return status;
}

static NvMediaStatus
SetExposure(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaISCExposureControl *exposureControl)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char reg_data, reg_pair[2];
    unsigned short dgain_reg = 0;
    int i, index;
    float digitalGain[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {0};
    RegUpdateList expRegList, dGainRegList;

    memset(&expRegList, 0, sizeof(RegUpdateList));
    memset(&dGainRegList, 0, sizeof(RegUpdateList));

    reg_data = exposureControl->sensorFrameId;
    EXPOSURE_PRINT("SetExposure: sensorFrameId = %u\n", reg_data);
    status = WriteRegister(handle, transaction, SENSOR_FRAME_ID_ADDRESS, 1, &reg_data);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    // Decompose sensor gain in analog, digital & conversion gain
    DecomposeGains(handle, exposureControl, digitalGain);

    // Digital gain reg value
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if (digitalGain[i]) {
            dgain_reg = (unsigned short)(digitalGain[i] * 256.0 + 0.5);
            REG_UPDATE(dGainRegList, DGAIN_L_H + i * 2, DGAIN_L_H, dgain_reg >> 8);
            REG_UPDATE(dGainRegList, DGAIN_L_H + i * 2 + 1, DGAIN_L_H, dgain_reg & 0xFF);
        }
    }

    index = 0;
    // Exposure time reg value
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        float exposureTime;
        float exposureLines;
        unsigned int exposure_fix_16_5;

        if (!(driverHandle->exposureModeMask & (1 << i)))
            continue;

        // Check valid exposure time
        if(exposureControl->exposureTime[index].valid) {
            exposureTime = exposureControl->exposureTime[index].value;

            // Calculate exposure lines
            exposureLines = exposureTime * driverHandle->config_info.exposureLineRate;

            EXPOSURE_PRINT("SetExposure: Exp: %d Exposure Time: %f Exposure Lines: %f\n", i, exposureTime, exposureLines);

            // Check boundaries
            if (exposureLines > (float)EXPOSURE_LONG) {
                exposureLines = (float)EXPOSURE_LONG;
            }
            else if ((i != NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT) && (exposureLines < 1.0)) {
                exposureLines = 1.0;
            }
            else if (exposureLines < 0.5625) {
                exposureLines = 0.5625;
            }

            // Convert to 16.5 fix point format
            exposure_fix_16_5 = (unsigned int)(exposureLines * 32.0 + 0.5);

            EXPOSURE_PRINT("SetExposure: Exp: %d Exposure reg: 0x%04X\n", i, exposure_fix_16_5 >> 5);

            switch(i) {
                case NVMEDIA_ISC_EXPOSURE_MODE_LONG:
                    reg_pair[0] = (exposure_fix_16_5 >> 13) & 0xFF;
                    reg_pair[1] = (exposure_fix_16_5 >> 5) & 0xFF;
                    REG_UPDATE(expRegList, EXPO_L_H, EXPO_L_H, reg_pair[0]);
                    REG_UPDATE(expRegList, EXPO_L_L, EXPO_L_H, reg_pair[1]);
                    break;
                case NVMEDIA_ISC_EXPOSURE_MODE_SHORT:
                    reg_pair[0] = (exposure_fix_16_5 >> 13) & 0xFF;
                    reg_pair[1] = (exposure_fix_16_5 >> 5) & 0xFF;
                    REG_UPDATE(expRegList, EXPO_S_H, EXPO_L_H, reg_pair[0]);
                    REG_UPDATE(expRegList, EXPO_S_L, EXPO_L_H, reg_pair[1]);
                    break;
                case NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT:
                    reg_pair[0] = exposure_fix_16_5 & 0xFF;
                    REG_UPDATE(expRegList, EXPO_VS, EXPO_L_H, reg_pair[0]);
                    break;
            }
        }
        index++;
    }

    // Common/Analog gain reg value
    REG_UPDATE(expRegList, CG_AGAIN, EXPO_L_H, driverHandle->config_info.cg_gain_reg);

    // Update registers
    status = WriteRegUpdateList(handle, transaction, DGAIN_L_H, DGAIN_REG_BLOCK_SIZE, &dGainRegList, NVMEDIA_TRUE);
    status = WriteRegUpdateList(handle, transaction, EXPO_L_H, EXP_REG_BLOCK_SIZE, &expRegList, NVMEDIA_FALSE);

    return status;
}

// Parse exposure info from embedded data
static void
ParseExposure(
    NvMediaISCEmbeddedData *parsedInformation,
    float lineRate,
    unsigned int exposureModeMask)
{
    unsigned char *data = (unsigned char*)(parsedInformation->top.data);
    unsigned int i, k;
    unsigned char cg_again_reg = data[CG_AGAIN - TOP_LINE_ADDRESS_BASE];
    unsigned int exposure;
    unsigned int dgain_reg;
    unsigned int analogGain;
    float exposureLong, exposureShort, exposureVeryShort, exposureMidpoint = 0.0;
    int numValidExposures = 0;

    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        memset(&parsedInformation->exposure[i], 0,
               sizeof(parsedInformation->exposure[i]));
    }

    k = 0;
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if (exposureModeMask & (1 << i)) {
            dgain_reg = data[(DGAIN_L_H + i * 2) - TOP_LINE_ADDRESS_BASE];
            dgain_reg = (dgain_reg << 8) | data[(DGAIN_L_H + i * 2 + 1) - TOP_LINE_ADDRESS_BASE];
            parsedInformation->exposure[k].digitalGain = (float)(dgain_reg/256.0);

            analogGain = (cg_again_reg & (0x3 << 2*i)) >> (2*i);
            parsedInformation->exposure[k].analogGain = (float)(1 << analogGain);

            switch (i) {
                case NVMEDIA_ISC_EXPOSURE_MODE_LONG:
                    parsedInformation->exposure[k].conversionGain =
                        (((cg_again_reg >> 6) & 0x01) == 0) ? 1.0 : 2.57;
                    break;
                case NVMEDIA_ISC_EXPOSURE_MODE_SHORT:
                    parsedInformation->exposure[k].conversionGain = 1.0;
                    break;
                case NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT:
                    parsedInformation->exposure[k].conversionGain =
                        (((cg_again_reg >> 7) & 0x01) == 0) ? 1.0 : 2.57;
                    break;
            }

            exposure = data[(EXPO_L_H + i * 2) - TOP_LINE_ADDRESS_BASE];
            if (i != NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT)
                exposure = (exposure << 8) | data[(EXPO_L_H + i * 2 + 1) - TOP_LINE_ADDRESS_BASE];
            switch(i) {
                case NVMEDIA_ISC_EXPOSURE_MODE_LONG:
                case NVMEDIA_ISC_EXPOSURE_MODE_SHORT:
                    parsedInformation->exposure[k].exposureTime = exposure/lineRate;
                    break;
                case NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT:
                    exposure &= 0xFF;
                    parsedInformation->exposure[k].exposureTime = exposure/(32.0*lineRate);
                    break;
            }
            EXPOSURE_PRINT("%s: mode %i, analog gain = %f\n", \
                __FUNCTION__, k, parsedInformation->exposure[k].analogGain);

            EXPOSURE_PRINT("%s: mode %i, digital gain = %f\n", \
                __FUNCTION__, k, parsedInformation->exposure[k].digitalGain);

            EXPOSURE_PRINT("%s: mode %i, exposure time = %f\n", \
                __FUNCTION__, k, parsedInformation->exposure[k].exposureTime);
            k++;
        }
    }

    // Get exposure values
    exposureLong = parsedInformation->exposure[NVMEDIA_ISC_EXPOSURE_MODE_LONG].exposureTime;
    exposureShort = parsedInformation->exposure[NVMEDIA_ISC_EXPOSURE_MODE_SHORT].exposureTime;
    exposureVeryShort = parsedInformation->exposure[NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT].exposureTime;

    // Check valid exposures and average them only if they are valid
    if(exposureLong != 0.0) {
        // Bias long with very short since the same diode is used for both
        exposureMidpoint += (0.5 * exposureLong + exposureVeryShort);
        numValidExposures++;
    }
    if(exposureShort != 0.0) {
        exposureMidpoint += 0.5 * exposureShort;
        numValidExposures++;
    }
    if(exposureVeryShort != 0.0) {
        exposureMidpoint += 0.5 * exposureVeryShort;
        numValidExposures++;
    }
    if(numValidExposures) {
        // Average frame middle points
        exposureMidpoint /= (float)numValidExposures;
    }
    parsedInformation->exposureMidpointTime = exposureMidpoint;
}

static NvMediaStatus
SetWBGain(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaISCWBGainControl *wbControl)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    NvMediaStatus status;
    float WBGain[4];
    unsigned short gain;
    int offset;
    int indexR = 1, indexGr = 0, indexGb = 3, indexB = 2;
    int i, j, k;
    RegUpdateList regList;

    memset(&regList, 0, sizeof(regList));

    k = 0;
    // Program wb gains and offsets for all exposures & component
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if (driverHandle->exposureModeMask & (1 << i)) {
            if (wbControl->wbGain[k].valid) {
                WBGain[indexR]  = wbControl->wbGain[k].value[0];
                WBGain[indexGr] = wbControl->wbGain[k].value[1];
                WBGain[indexGb] = wbControl->wbGain[k].value[2];
                WBGain[indexB]  = wbControl->wbGain[k].value[3];

                for (j = 0; j < 4; j++) {
                    gain = (unsigned short) ((WBGain[j] * 256) + 0.5);
                    REG_UPDATE(regList, R_GAIN_L_I_H + (i * 4 * 2)
                               + j * 2, R_GAIN_L_I_H, (gain >> 8) & 0xFF);
                    REG_UPDATE(regList, R_GAIN_L_I_H + (i * 4 * 2)
                               + j * 2 + 1, R_GAIN_L_I_H, gain & 0xFF);

                    offset = (int) ((WBGain[j] - 1)* 128 * 256);
                    REG_UPDATE(regList, R_R_OFFSET_L_I_H + (i * 4 * 3)
                               + j * 3, R_GAIN_L_I_H, (offset & 0xFF0000) >> 16);
                    REG_UPDATE(regList, R_R_OFFSET_L_I_H + (i * 4 * 3)
                               + j * 3 + 1, R_GAIN_L_I_H, (offset & 0xFF00) >> 8);
                    REG_UPDATE(regList, R_R_OFFSET_L_I_H + (i * 4 * 3)
                               + j * 3 + 2, R_GAIN_L_I_H, offset & 0xFF);
                }
            }
            k++;
        }
    }

    status = WriteRegUpdateList(handle, transaction, R_GAIN_L_I_H, AWB_REG_BLOCK_SIZE, &regList, NVMEDIA_FALSE);

    return status;
}

// Parse WB Gain from embedded data
static void
ParseWBGain(
    NvMediaISCEmbeddedData *parsedInformation,
    unsigned int exposureModeMask)
{
    unsigned char *data = (unsigned char*)(parsedInformation->top.data);
    int i, j, k, address;
    unsigned char integer, fraction;
    float value;
    //
    // The sensor register address map is messed up.
    // Need the lookup table to reorder them.
    //
    static int indexMap[4] = {
       1, // R
       0, // GR
       3, // GB
       2  // B
    };
    int index;

    address = R_GAIN_L_I_H;

    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        memset(parsedInformation->wbGain[i].value, 0,
               sizeof(parsedInformation->wbGain[i].value));
    }

    k = 0;
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if (exposureModeMask & (1 << i)) {
            for (j = 0; j < 4; j++) {
                index = indexMap[j];
                integer = data[address - TOP_LINE_ADDRESS_BASE];
                fraction = data[address + 1 - TOP_LINE_ADDRESS_BASE];
                value = (float)integer + fraction/256.0f;
                parsedInformation->wbGain[k].value[index] = value;
                address += 2;
            }
            k++;
        } else {
            // Mode disabled, skip the parsing
            address += 8;
        }
    }
}

static void ParseBlackLevelAndTemperature(
    _DriverHandle *handle,
    NvMediaISCEmbeddedData *parsedInformation)
{
    unsigned char *data = (unsigned char*)(parsedInformation->top.data);
    short int c_blc_reg_base = BLC_TARGET_L_H - TOP_LINE_ADDRESS_BASE;
    short int y_dark_reg_base = DARK_CURRENT_L_H - TOP_LINE_ADDRESS_BASE;
    short int y_black_reg_base = ROW_AVERAG_L_H - TOP_LINE_ADDRESS_BASE;
    unsigned char temp_reg;
    int temperature;
    unsigned int i;
    int diff, dir;

    // Get temperature
    temp_reg = data[TMP_02 - TOP_LINE_ADDRESS_BASE];
    if(temp_reg > 0xc0)
        temperature = (int)temp_reg - 0x100;
    else
        temperature = (int)temp_reg;

    // Difference from last reading
    diff = temperature - handle->temperatureLast;
    // Direction of change since last (1=Up 0=Same -1=Down)
    dir = (diff > 0) - (diff < 0);

    // Check direction change
    if(dir && (dir != handle->temperatureDir)) {
        // Direction changed
        // Store new direction and reset counter
        handle->temperatureDir = dir;
        handle->temperatureDirCount = 0;
    } else {
        // Same direction
        // Increment counter
        handle->temperatureDirCount++;
        // Check direction is not change for 5 readings
        if(handle->temperatureDirCount > 5) {
            // Store as current temperature
            handle->temperature = temperature;
        }
    }
    // Save last temperature
    handle->temperatureLast = temperature;

    // Get dark current related sensor data
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        handle->c_blc[i] = (data[c_blc_reg_base] << 8) | data[c_blc_reg_base + 1];
        handle->y_dark[i] = (data[y_dark_reg_base] << 8) | data[y_dark_reg_base + 1];
        handle->y_black[i] = (data[y_black_reg_base] << 8) | data[y_black_reg_base + 1];
        c_blc_reg_base += 2;
        y_dark_reg_base += 2;
        y_black_reg_base += 2;
    }
#if 0
    DECOMPOSITION_PRINT("Temperature: %d\n", handle->temperature);
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        char *exposures[] = { "Long", "Short", "Very Short" };

        DECOMPOSITION_PRINT("%s exposure: c_blc: %d (0x%X) y_dark: %d (0x%X) y_black: %d (0x%X)\n",
            exposures[i],
            handle->c_blc[i], handle->c_blc[i],
            handle->y_dark[i], handle->y_dark[i],
            handle->y_black[i], handle->y_black[i]);
    }
    DECOMPOSITION_PRINT("\n");
#endif
}

static NvMediaStatus
ParseEmbeddedData(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int lineCount,
    unsigned int *lineLength,
    unsigned char *lineData[],
    NvMediaISCEmbeddedData *parsedInformation)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned int i;
    unsigned int stride;
    unsigned int data, mask, high, shift;
    unsigned char *dst;
    int sensorOutputConfig;
    float lineRate;
    unsigned int frame_counter;

    if(!handle || !transaction ||!lineLength || !lineData || !parsedInformation || !lineCount)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    sensorOutputConfig = driverHandle->config_info.enumeratedDeviceConfig;
    lineRate = driverHandle->config_info.exposureLineRate;

    switch(sensorOutputConfig) {
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x800:
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x1080:
            stride = 4;
            high = 29;
            break;
        default:
            status = NVMEDIA_STATUS_NOT_SUPPORTED;
            goto done;
    }

    shift = high - 7;
    mask = 255 << shift;

    if(parsedInformation->top.bufferSize < lineLength[0]/stride) {
        status = NVMEDIA_STATUS_INSUFFICIENT_BUFFERING;
        goto done;
    }

    dst = (unsigned char*)parsedInformation->top.data;

    for(i = 0; i + stride <= lineLength[0]; i += stride) {
        data = *((unsigned int*)(lineData[0]+i));
        data = (data & mask) >> shift;
        *dst++ = (unsigned char)data;
    }

    parsedInformation->top.baseRegAddress = TOP_LINE_ADDRESS_BASE;
    parsedInformation->top.size = lineLength[0]/stride;

    // Parse gains and the exposure time
    ParseExposure(parsedInformation, lineRate, driverHandle->exposureModeMask);
    ParseWBGain(parsedInformation, driverHandle->exposureModeMask);

    // Parse temparature related registers to calculate proper gains
    ParseBlackLevelAndTemperature((_DriverHandle *)handle, parsedInformation);

    // Parse log module bias
    dst = (unsigned char*)parsedInformation->top.data;
    parsedInformation->mode = dst[LOG_MOD_BIAS - TOP_LINE_ADDRESS_BASE] & 0x1;

    // Parse frame counter
    frame_counter = dst[FCNT_3 - TOP_LINE_ADDRESS_BASE] << 24 |
                    dst[FCNT_2 - TOP_LINE_ADDRESS_BASE] << 16 |
                    dst[FCNT_1 - TOP_LINE_ADDRESS_BASE] << 8  |
                    dst[FCNT_0 - TOP_LINE_ADDRESS_BASE];
    parsedInformation->frameSequenceNumber = frame_counter;

    if(lineCount == 1) goto done;

    if(parsedInformation->bottom.bufferSize < lineLength[1]/stride) {
        status = NVMEDIA_STATUS_INSUFFICIENT_BUFFERING;
        goto done;
    }

    dst = (unsigned char*)parsedInformation->bottom.data;

    for(i = 0; i + stride < lineLength[1]; i += stride) {
        data = *((unsigned int*)(lineData[1]+i));
        data = (data & mask) >> shift;
        *dst++ = (unsigned char)data;
    }

    parsedInformation->bottom.size = lineLength[1]/stride;

    // The content of EMB_START_ADDR1
    parsedInformation->bottom.baseRegAddress = BOTTOM_LINE_ADDRESS_BASE;

done:
    return status;
}

static NvMediaStatus
GetSensorFrameId(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int lineCount,
    unsigned int *lineLength,
    unsigned char *lineData[],
    unsigned int *sensorFrameId)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char *topLine = lineData[0];
    int sensorOutputConfig = ((_DriverHandle *)handle)->config_info.enumeratedDeviceConfig;

    if(lineCount == 0) {
        return NVMEDIA_STATUS_ERROR;
    }

    if(lineLength[0] < SENSOR_FRAME_ID_OFFSET*4+4) {
        return NVMEDIA_STATUS_ERROR;
    }

    switch(sensorOutputConfig) {
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x800:
        case ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x1080:
            *sensorFrameId =
                (*((unsigned short *)topLine + SENSOR_FRAME_ID_OFFSET * 2 + 1) >> 6) & 0xFF;
            break;
        default:
            status = NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    EXPOSURE_PRINT("%s: sensorFrame id %u\n", __FUNCTION__, *sensorFrameId);
    return status;
}

static unsigned int
computeMaxHdrRatio(
    _DriverHandle *driverHandle,
    ConfigResolutionOV10640 resolution)
{
    unsigned int maxHdrRatio = 1;
    int i, firstExp = -1, lastExp = -1;

    for(i = 0; i <= NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT; i++) {
        if(driverHandle->exposureModeMask & (1 << i)) {
            if(firstExp == -1)
                firstExp = i;
            lastExp = i;
        }
    }

    if (firstExp != lastExp) {
        maxHdrRatio =
            (maxET[resolution][firstExp] * driverHandle->minGain[firstExp]) /
            (maxET[resolution][lastExp] * driverHandle->minGain[lastExp]);
    }

    return maxHdrRatio;
}

static NvMediaStatus
GetSensorProperties(
    NvMediaISCDriverHandle *handle,
    NvMediaISCSensorProperties *properties)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    ConfigResolutionOV10640 resolution = driverHandle->config_info.resolution;
    int i, k;

    memset(properties, 0, sizeof(*properties));
    properties->frameRate = 30.0f;

    k = 0;
    for(i = 0; i <= NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT; i++) {
        if(driverHandle->exposureModeMask & (1 << i)) {
            properties->exposure[k].valid = NVMEDIA_TRUE;
            properties->exposure[k].minExposureTime = minET[resolution][i];
            properties->exposure[k].maxExposureTime = maxET[resolution][i];
            properties->exposure[k].minSensorGain = driverHandle->minGain[i];
            properties->exposure[k].maxSensorGain = maxGain[i];
            k++;
        }
    }

    properties->maxHdrRatio =
        computeMaxHdrRatio(driverHandle, resolution);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetSensorAttr(
    NvMediaISCDriverHandle *handle,
    NvMediaISCSensorAttrType type,
    unsigned int size,
    void *attribute)
{
    _DriverHandle *driverHandle;
    ConfigResolutionOV10640 resolution;
    int i, k = 0;
    float val[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {0};

    if(!handle || !attribute) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    driverHandle = (_DriverHandle *)handle;
    resolution = driverHandle->config_info.resolution;

    switch(type) {
        case NVMEDIA_ISC_SENSOR_ATTR_GAIN_MIN:
        {
            if(size != sizeof(driverHandle->minGain)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i <= NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = driverHandle->minGain[i];
                    k++;
                }
            }
            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_GAIN_MAX:
        {
            if(size != sizeof(maxGain)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i <= NVMEDIA_ISC_EXPOSURE_MODE_VERY_SHORT; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = maxGain[i];
                    k++;
                }
            }
            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_MIN:
        {
            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = minET[resolution][i] / 1e9; // convert to seconds
                    k++;
                }
            }

            if(size != sizeof(val)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_MAX:
        {
            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = maxET[resolution][i] / 1e9; // convert to seconds
                    k++;
                }
            }

            if(size != sizeof(val)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_HDR_MAX:
        {
            unsigned int maxHdrRatio;

            maxHdrRatio = computeMaxHdrRatio(driverHandle, resolution);

            if (size != sizeof(unsigned int)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            *((unsigned int *) attribute) = maxHdrRatio;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_FRAME_RATE:
        {
            if (size != sizeof(float)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            *((float *) attribute) = 30.0f;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_NUM_EXPOSURES:
        {
            if (size != sizeof(unsigned int)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    k++;
                }
            }

            *((unsigned int *) attribute) = k;
            return NVMEDIA_STATUS_OK;
        }
        default:
            return NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetModuleConfig(
    NvMediaISCDriverHandle *handle,
    NvMediaISCModuleConfig *cameraModuleConfig)
{
    if((!handle) || (!cameraModuleConfig))
        return NVMEDIA_STATUS_BAD_PARAMETER;

    strncpy(cameraModuleConfig->cameraModuleCfgName,
            ((_DriverHandle *)handle)->moduleCfg.cameraModuleCfgName,
            strlen(((_DriverHandle *)handle)->moduleCfg.cameraModuleCfgName));
    cameraModuleConfig->cameraModuleConfigPass1 = ((_DriverHandle *)handle)->moduleCfg.cameraModuleConfigPass1;
    cameraModuleConfig->cameraModuleConfigPass2 = ((_DriverHandle *)handle)->moduleCfg.cameraModuleConfigPass2;

    return NVMEDIA_STATUS_OK;
}

static NvMediaISCDeviceDriver deviceDriver = {
    .deviceName = "OV10640 Image Sensor",
    .deviceType = NVMEDIA_ISC_DEVICE_IMAGE_SENSOR,
    .regLength = 2,
    .dataLength = 1,
    .DriverCreate = DriverCreate,
    .DriverDestroy = DriverDestroy,
    .CheckPresence = CheckPresence,
    .EnableLink = EnableLink,
    .GetSensorFrameId = GetSensorFrameId,
    .ParseEmbeddedData = ParseEmbeddedData,
    .SetDefaults = SetDefaults,
    .SetDeviceConfig = SetDeviceConfig,
    .GetTemperature = GetTemperature,
    .ReadRegister = ReadRegister,
    .WriteRegister = WriteRegister,
    .SetExposure = SetExposure,
    .SetWBGain = SetWBGain,
    .WriteParameters = WriteParameters,
    .ReadParameters = ReadParameters,
    .DumpRegisters = DumpRegisters,
    .GetSensorProperties = GetSensorProperties,
    .GetSensorAttr = GetSensorAttr,
    .GetModuleConfig = GetModuleConfig
};

NvMediaISCDeviceDriver *
GetOV10640Driver(void)
{
    return &deviceDriver;
}

NvMediaStatus
GetOV10640ConfigSet(
    char *resolution,
    char *inputFormat,
    int *configSet
    )
{
    // set input mode setting for ov10640
    if(inputFormat && !strcasecmp(inputFormat, "raw12")) {
        if(resolution && !strcasecmp(resolution, "1280x1080"))
            *configSet = ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x1080;
        else
            *configSet = ISC_CONFIG_OV10640_DVP_RAW12_COMP_1280x800;
    } else {
        LOG_ERR("%s: Unsupported input format: %s \n", __func__, inputFormat);
        return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}
