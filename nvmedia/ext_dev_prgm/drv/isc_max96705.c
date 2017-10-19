/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log_utils.h"
#include "nvmedia_isc.h"
#include "isc_max96705.h"

#define REGISTER_ADDRESS_BYTES  1
#define REG_WRITE_BUFFER        32

#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   &x[1]
#define SET_NEXT_BLOCK(x)   x += (x[0] + 1)

typedef struct {
    NvMediaISCSupportFunctions *funcs;
} _DriverHandle;

unsigned char max96705_enable_reverse_channel[] = {
    2, 0x04, 0x43,  // Enable config link, wait 5ms
};

unsigned char max96705_enable_serial_link[] = {
    2, 0x04, 0x83,   // Enable serial link, disable config link, wait 5ms
};

unsigned char max96705_defaults[] = {
    2, 0x08, 0x01,
    2, 0x97, 0x5F, //enable bit only on 96705
};

unsigned char max96705_config_input_mode[] = {
    2, 0x07, 0xC4,  // PCLKIN setting DBL=1, HIBW=1, BWS=0, ES=0, HVEN=1
};

unsigned char max96705_set_translator_a[] = {
    2, 0x09, 0x00,
    2, 0x0a, 0x00,
};

unsigned char max96705_set_translator_b[] = {
    2, 0x0b, 0x00,
    2, 0x0c, 0x00,
};

unsigned char max96705_regen_vsync[] = {
    2, 0x44, 0x00,   //vsync delay pclk
    2, 0x45, 0x9C,
    2, 0x46, 0x80,
    2, 0x47, 0x00,   //vsync high pclk
    2, 0x48, 0xb0,
    2, 0x49, 0x00,
    2, 0x43, 0x21,   //eanble vsync re-gen
    2, 0x67, 0xc4,   //align at HS rising edge
};

unsigned char max96705_set_xbar[] = {
    2, 0x20, 0x04,
    2, 0x21, 0x03,
    2, 0x22, 0x02,
    2, 0x23, 0x01,
    2, 0x24, 0x00,
    2, 0x25, 0x40,
    2, 0x26, 0x40,
    2, 0x27, 0x0E,
    2, 0x28, 0x2F,
    2, 0x29, 0x0E,
    2, 0x2A, 0x40,
    2, 0x2B, 0x40,
    2, 0x2C, 0x40,
    2, 0x2D, 0x40,
    2, 0x2E, 0x40,
    2, 0x2F, 0x40,
    2, 0x30, 0x17,
    2, 0x31, 0x16,
    2, 0x32, 0x15,
    2, 0x33, 0x14,
    2, 0x34, 0x13,
    2, 0x35, 0x12,
    2, 0x36, 0x11,
    2, 0x37, 0x10,
    2, 0x38, 0x07,
    2, 0x39, 0x06,
    2, 0x3A, 0x05,
    2, 0x3B, 0x40,
    2, 0x3C, 0x40,
    2, 0x3D, 0x40,
    2, 0x3E, 0x40,
    2, 0x3F, 0x0E,
    2, 0x40, 0x2F,
    2, 0x41, 0x0E,
};

unsigned char max96705_auto_config_link[] = {
    2, 0x67, 0xE4,
};

unsigned char max96705_double_input_mode[] = {
    2, 0x07, 0x80,  // PCLKIN setting DBL=1
};

unsigned char max96705_config_serializer_coax[] = {
    2, 0x4D, 0x40,
};

unsigned char max96705_i2c_remote_master_timeout[] = {
    2, 0x99, 0x0e, // set max(8.192ms) remote master timeout (Bug 1802338)
};

static NvMediaStatus
DriverCreate(
    NvMediaISCDriverHandle **handle,
    NvMediaISCSupportFunctions *supportFunctions,
    void *clientContext)
{
    _DriverHandle *driverHandle;

    if(!handle || !supportFunctions)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    driverHandle = calloc(1, sizeof(_DriverHandle));
    if(!driverHandle)
        return NVMEDIA_STATUS_OUT_OF_MEMORY;

    driverHandle->funcs = supportFunctions;
    *handle = (NvMediaISCDriverHandle *)driverHandle;

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
CheckPresence(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaISCSupportFunctions *funcs;
    unsigned char reg = 0x1e;
    unsigned char readBuff = 0;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    status = funcs->Read(
                transaction,                // transaction
                REGISTER_ADDRESS_BYTES,     // regLength
                &reg,                       // regData
                1,                          // dataLength
                &readBuff);                 // dataBuff

    if(status != NVMEDIA_STATUS_OK)
        return status;

    return (CHECK_96705ID(readBuff) ? NVMEDIA_STATUS_OK
                                    : NVMEDIA_STATUS_ERROR);
}

static NvMediaStatus
EnableLink(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int instanceNumber,
    NvMediaBool enable)
{
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetPreEmp(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned char preemp)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaISCSupportFunctions *funcs;
    unsigned char reg[2] = {0x06, 0xA0};

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    reg[1] |= (preemp & 0xF);

    status = funcs->Write(
        transaction,
        sizeof(reg),
        reg);

    if(status != NVMEDIA_STATUS_OK)
        return status;

    usleep(5000); /* Delay to wait since I2C unavailable while GMSL locks from programming guide */

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
WriteArray(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int arrayByteLength,
    unsigned char *arrayData)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status;

    if((!handle) || (!transaction) || (!arrayData))
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    while(arrayByteLength) {
        if(arrayData[0] < 10) {
            status = funcs->Write(
                    transaction,                 // transaction
                    GET_BLOCK_LENGTH(arrayData), // dataLength
                    GET_BLOCK_DATA(arrayData));

            if(status != NVMEDIA_STATUS_OK) {
                LOG_DBG("%s, max96705 : error: wri2c   0x80    0x%.2X    0x%.2X\n",
                    __func__, (unsigned int)arrayData[1], (unsigned int)arrayData[2]);
                break;
            }

            arrayByteLength -= GET_BLOCK_LENGTH(arrayData) + 1;
            SET_NEXT_BLOCK(arrayData);
        } else {
            usleep(arrayData[0] * 10);
            arrayData++;
            arrayByteLength--;
        }
    }

    return status;
}

static NvMediaStatus
SetDefaults(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status;

    status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_defaults),
                max96705_defaults);
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
        case ISC_CONFIG_MAX96705_DEFAULT:
            status = SetDefaults(
                handle,
                transaction);
           break;
        case ISC_CONFIG_MAX96705_ENABLE_SERIAL_LINK:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_enable_serial_link),
                max96705_enable_serial_link);
            break;
        case ISC_CONFIG_MAX96705_PCLKIN:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_config_input_mode),
                max96705_config_input_mode);
            break;
        case ISC_CONFIG_MAX96705_REGEN_VSYNC:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_regen_vsync),
                max96705_regen_vsync);
            break;
        case ISC_CONFIG_MAX96705_ENABLE_REVERSE_CHANNEL:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_enable_reverse_channel),
                max96705_enable_reverse_channel);
            break;
        case ISC_CONFIG_MAX96705_SET_AUTO_CONFIG_LINK:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_auto_config_link),
                max96705_auto_config_link);
            break;
        case ISC_CONFIG_MAX96705_DOUBLE_INPUT_MODE:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_double_input_mode),
                max96705_double_input_mode);
            break;
        case ISC_CONFIG_MAX96705_CONFIG_SERIALIZER_COAX:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_config_serializer_coax),
                max96705_config_serializer_coax);
            break;
        case ISC_CONFIG_MAX96705_SET_XBAR:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_set_xbar),
                max96705_set_xbar);
            break;
         case ISC_CONFIG_MAX96705_SET_MAX_REMOTE_I2C_MASTER_TIMEOUT:
            status = WriteArray(
                handle,
                transaction,
                sizeof(max96705_i2c_remote_master_timeout),
                max96705_i2c_remote_master_timeout);
            break;
       default:
            status =  NVMEDIA_STATUS_NOT_SUPPORTED;
            break;
    }

    return status;
}

static NvMediaStatus
GetTemperature(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    float *temperature)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
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

    registerData[0] = registerNum & 0xFF;
    status = funcs->Read(
        transaction,    // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        registerData,   // regData
        dataLength,     // dataLength
        dataBuff);      // data

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

    data[0] = registerNum & 0xFF;
    memcpy(&data[1], dataBuff, (REG_WRITE_BUFFER < dataLength ? REG_WRITE_BUFFER: dataLength));

    status = funcs->Write(
        transaction,                         // transaction
        dataLength + REGISTER_ADDRESS_BYTES, // dataLength
        data);                               // data

    return status;
}

static NvMediaStatus
SetTranslator(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        WriteReadParametersParamMAX96705 *param)
{
    NvMediaStatus status;
    unsigned char *max96705_set_translator;

    if(parameterType == ISC_WRITE_PARAM_CMD_MAX96705_SET_TRANSLATOR_A) {
            max96705_set_translator = &max96705_set_translator_a[0];
    } else {
            max96705_set_translator = &max96705_set_translator_b[0];
    }
    max96705_set_translator[2] = param->Translator.source << 1;
    max96705_set_translator[5] = param->Translator.destination << 1;

    status = WriteArray(
        handle,
        transaction,
        sizeof(max96705_set_translator_a),
        max96705_set_translator);

    return status;
}

static NvMediaStatus
SetDeviceAddress(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned char address)
{
    NvMediaStatus status;
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = 0x00;

    if(address > 0x80)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    data[1] = address << 1;

    status = funcs->Write(
        transaction,
        2,
        data);

    return status;
}

static NvMediaStatus
GetDeviceAddress(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned char *address)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = 0x00;

    status = funcs->Read(
        transaction,
        1,
        &data[0],
        1,
        &data[1]);

    if (status == NVMEDIA_STATUS_OK)
        *address = data[1];

    return status;
}

static NvMediaStatus
SetInputMode(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        ConfigureInputModeMAX96705 *inputmode)
{
    NvMediaStatus status;
    NvMediaISCSupportFunctions *funcs;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    max96705_config_input_mode[2] =  (unsigned char)inputmode->byte;

    status = funcs->Write(
        transaction,
        max96705_config_input_mode[0],
        &max96705_config_input_mode[1]);

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
    NvMediaStatus status;
    WriteReadParametersParamMAX96705 *param;

    if((!parameter) || (!handle) || (!transaction))
        return NVMEDIA_STATUS_BAD_PARAMETER;

    param = (WriteReadParametersParamMAX96705 *)parameter;

    switch(parameterType) {
        case ISC_WRITE_PARAM_CMD_MAX96705_SET_TRANSLATOR_A:
        case ISC_WRITE_PARAM_CMD_MAX96705_SET_TRANSLATOR_B:
            if(parameterSize != sizeof(param->Translator))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            status = SetTranslator(
                handle,
                transaction,
                parameterType,
                param);
           break;
        case ISC_WRITE_PARAM_CMD_MAX96705_SET_DEVICE_ADDRESS:
            if(parameterSize != sizeof(param->DeviceAddress))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            status = SetDeviceAddress(
                handle,
                transaction,
                param->DeviceAddress.address);
           break;
        case ISC_WRITE_PARAM_CMD_MAX96705_CONFIG_INPUT_MODE:
            if(parameterSize != sizeof(param->inputmode))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            status = SetInputMode(
                handle,
                transaction,
                param->inputmode);
           break;
        case ISC_WRITE_PARAM_CMD_MAX96705_SET_PREEMP:
            if(parameterSize != sizeof(param->preemp))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            status = SetPreEmp(
                handle,
                transaction,
                param->preemp);
            break;
        default:
            status = NVMEDIA_STATUS_NOT_SUPPORTED;
            break;
    }

    return status;
}

static NvMediaStatus
ReadParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        unsigned int parameterSize,
        void *parameter)
{
    NvMediaStatus status;
    WriteReadParametersParamMAX96705 *param;

    if(!parameter)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    param = (WriteReadParametersParamMAX96705 *)parameter;

    switch(parameterType) {
        case ISC_READ_PARAM_CMD_MAX96705_GET_DEVICE_ADDRESS:
            if(parameterSize != sizeof(param->DeviceAddress))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            status = GetDeviceAddress(
                handle,
                transaction,
                (unsigned char*)param);
            break;
        default:
            status = NVMEDIA_STATUS_NOT_SUPPORTED;
            break;
    }

    return status;
}

static NvMediaStatus
DumpRegisters(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaISCDeviceDriver deviceDriver = {
    .deviceName = "Maxim 96705 Serializer",
    .deviceType = NVMEDIA_ISC_DEVICE_SERIALIZER,
    .regLength = 1,
    .dataLength = 1,
    .DriverCreate = DriverCreate,
    .DriverDestroy = DriverDestroy,
    .CheckPresence = CheckPresence,
    .EnableLink = EnableLink,
    .SetDefaults = SetDefaults,
    .SetDeviceConfig = SetDeviceConfig,
    .GetTemperature = GetTemperature,
    .ReadRegister = ReadRegister,
    .WriteRegister = WriteRegister,
    .WriteParameters = WriteParameters,
    .ReadParameters = ReadParameters,
    .DumpRegisters = DumpRegisters
};

NvMediaISCDeviceDriver *
GetMAX96705Driver(void)
{
    return &deviceDriver;
}

