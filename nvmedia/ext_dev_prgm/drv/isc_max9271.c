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

#include "nvmedia_isc.h"
#include "isc_max9271.h"

#define REGISTER_ADDRESS_BYTES  1
#define REG_WRITE_BUFFER        32

#if !defined(__INTEGRITY)
#define MIN(a,b)            (((a) < (b)) ? (a) : (b))
#endif
#define GET_SIZE(x)         sizeof(x)
#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   &x[1]
#define SET_NEXT_BLOCK(x)   x += (x[0] + 1)

typedef struct {
    NvMediaISCSupportFunctions *funcs;
} _DriverHandle;

unsigned char max9271_enable_reverse_channel[] = {
    2, 0x04, 0x43,  // Enable config link
};

unsigned char max9271_defaults[] = {
    // Enable high threshold for reverse channel input buffer.
    // This increase immunity to power supply noise when we use power over the coax.
    2, 0x08, 0x01  // Bug 1660114 : bit[3] should be cleared for the reverse channel stability issue.
};

unsigned char max9271_invert_vs[] = {
    2, 0x08, 0x89  // invert vs
};

unsigned char max9271_config_input_mode[] = {
    2, 0x07, 0x94  // PCLKIN setting DBL=1, DRS=0, BWS=0, ES=1, HVEN=1, EDC=0
};

unsigned char max9271_enableSerialLink[] = {
    2, 0x04, 0x83,   // Enable serial link, disable config link
};

unsigned char max9271_set_translator_a[] = {
    2, 0x09, 0x00,
    2, 0x0a, 0x00,
};

unsigned char max9271_set_translator_b[] = {
    2, 0x0b, 0x00,
    2, 0x0c, 0x00,
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

    return (readBuff == MAX9271_DEVICE_ID) ? NVMEDIA_STATUS_OK :
                                             NVMEDIA_STATUS_ERROR;
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

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    while(arrayByteLength) {
        if(arrayData[0] < 10) {
            status = funcs->Write(
                    transaction,                 // transaction
                    GET_BLOCK_LENGTH(arrayData), // dataLength
                    GET_BLOCK_DATA(arrayData));

            if(status != NVMEDIA_STATUS_OK) {
                printf("MAX9271 : error: wri2c   0x80    0x%.2X    0x%.2X\n",
                    (unsigned int)arrayData[1],
                    (unsigned int)arrayData[2]);
                break;
            }
            /* This SER-DES pair needs 20SCLK clocks or more timing for next I2C command so we set 100 us with margin */
            usleep(100);

            arrayByteLength -= GET_BLOCK_LENGTH(arrayData) + 1;
            SET_NEXT_BLOCK(arrayData);
        } else {
            usleep(arrayData[0] * 10);
            arrayData++;
            arrayByteLength--;
        }
    }

    return NVMEDIA_STATUS_OK;
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
                GET_SIZE(max9271_defaults),
                max9271_defaults);

    return status;
}

static NvMediaStatus
EnableReverseChannel(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    status = funcs->Write(
            transaction,
            max9271_enable_reverse_channel[0],
            &max9271_enable_reverse_channel[1]);

    usleep(3000);

    return status;
}

static NvMediaStatus
EnableSerialLink(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    status = funcs->Write(
            transaction,
            max9271_enableSerialLink[0],
            &max9271_enableSerialLink[1]);

    usleep(350);

    return status;
}

static NvMediaStatus
SetDeviceConfig(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int enumeratedDeviceConfig)
{
    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_MAX9271_DEFAULT:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9271_defaults),
                max9271_defaults);
        case ISC_CONFIG_MAX9271_ENABLE_SERIAL_LINK:
            return EnableSerialLink(handle, transaction);
        case ISC_CONFIG_MAX9271_PCLKIN:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9271_config_input_mode),
                max9271_config_input_mode);
        case ISC_CONFIG_MAX9271_ENABLE_REVERSE_CHANNEL:
            return EnableReverseChannel(handle, transaction);
        case ISC_CONFIG_MAX9271_INVERT_VS:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9271_invert_vs),
                max9271_invert_vs);
    }

    return NVMEDIA_STATUS_OK;
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
    memcpy(&data[1], dataBuff, MIN(REG_WRITE_BUFFER, dataLength));

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
        WriteReadParametersParamMAX9271 *param)
{
    unsigned char *max9271_set_translator;

    if(parameterType == ISC_WRITE_PARAM_CMD_MAX9271_SET_TRANSLATOR_A) {
            max9271_set_translator = &max9271_set_translator_a[0];
    } else {
            max9271_set_translator = &max9271_set_translator_b[0];
    }
    max9271_set_translator[2] = param->Translator.source << 1;
    max9271_set_translator[5] = param->Translator.destination << 1;

    return WriteArray(
        handle,
        transaction,
        GET_SIZE(max9271_set_translator_a),
        max9271_set_translator);
}

static NvMediaStatus
SetDeviceAddress(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned char address)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = 0x00;

    if(address > 0x80)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    data[1] = address << 1;

    return funcs->Write(
        transaction,
        2,
        data);
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
    data[1] = 0x00;

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
        ConfigureInputModeMAX9271 *inputmode)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    max9271_config_input_mode[2] =  (unsigned char)inputmode->byte;

    status = funcs->Write(
        transaction,
        max9271_config_input_mode[0],
        &max9271_config_input_mode[1]);

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
    WriteReadParametersParamMAX9271 *param;

    if(!parameter)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    param = (WriteReadParametersParamMAX9271 *)parameter;

    switch(parameterType) {
        case ISC_WRITE_PARAM_CMD_MAX9271_SET_TRANSLATOR_A:
        case ISC_WRITE_PARAM_CMD_MAX9271_SET_TRANSLATOR_B:
            if(parameterSize != sizeof(param->Translator))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetTranslator(
                handle,
                transaction,
                parameterType,
                param);
        case ISC_WRITE_PARAM_CMD_MAX9271_SET_DEVICE_ADDRESS:
            if(parameterSize != sizeof(param->DeviceAddress))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetDeviceAddress(
                handle,
                transaction,
                param->DeviceAddress.address);
        case ISC_WRITE_PARAM_CMD_MAX9271_CONFIG_INPUT_MODE:
            if(parameterSize != sizeof(param->inputmode))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetInputMode(
                handle,
                transaction,
                param->inputmode);
        case ISC_WRITE_PARAM_CMD_MAX9271_SET_PREEMP:
            if(parameterSize != sizeof(param->preemp))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetPreEmp(
                handle,
                transaction,
                param->preemp);
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
    WriteReadParametersParamMAX9271 *param;

    if(!parameter)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    param = (WriteReadParametersParamMAX9271 *)parameter;

    switch(parameterType) {
        case ISC_READ_PARAM_CMD_MAX9271_GET_DEVICE_ADDRESS:
            if(parameterSize != sizeof(param->DeviceAddress))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return GetDeviceAddress(
                handle,
                transaction,
                (unsigned char*)param);
        default:
            break;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DumpRegisters(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaISCDeviceDriver deviceDriver = {
    .deviceName = "Maxim 9271 Serializer",
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
GetMAX9271Driver(void)
{
    return &deviceDriver;
}

