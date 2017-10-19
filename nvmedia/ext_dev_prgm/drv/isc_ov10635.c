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
#include "isc_ov10635.h"
#include "isc_ov10635_setting.h"

#define REGISTER_ADDRESS_BYTES  2
#define REG_WRITE_BUFFER        32

#if !defined(__INTEGRITY)
#define MIN(a,b)            (((a) < (b)) ? (a) : (b))
#define MAX(a,b)            (((a) > (b)) ? (a) : (b))
#endif
#define GET_SIZE(x)         sizeof(x)
#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   &x[1]
#define SET_NEXT_BLOCK(x)   x += (x[0] + 1)

typedef struct {
    NvMediaISCSupportFunctions *funcs;
    const unsigned char **sensor_settings;
} _DriverHandle;

static unsigned char ov10635_sync[] = {
    3, 0x38, 0x32, 0x01,
    3, 0x38, 0x33, 0x08,
    3, 0x38, 0x34, 0x03,
    3, 0x38, 0x35, 0x60,
    3, 0x30, 0x2E, 0x01
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
    driverHandle->sensor_settings = ov10635_sensor_settings;

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
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    while(arrayByteLength) {
        status = funcs->Write(
            transaction,                 // transaction
            GET_BLOCK_LENGTH(arrayData), // dataLength
            (unsigned char*) GET_BLOCK_DATA(arrayData));  // data
        if(status != NVMEDIA_STATUS_OK) {
            break;
        }

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
    NvMediaStatus status;

    if(!((_DriverHandle *)handle)->sensor_settings[ISC_CONFIG_OV10635_422P_8BIT_1280x800])
        return NVMEDIA_STATUS_ERROR;

    status = WriteArrayWithCommand(
                handle,
                transaction,
                ((_DriverHandle *)handle)->sensor_settings[ISC_CONFIG_OV10635_422P_8BIT_1280x800]);

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
        case ISC_CONFIG_OV10635_DEFAULT:
            status = WriteArrayWithCommand(
                    handle,
                    transaction,
                    ((_DriverHandle *)handle)->sensor_settings[ISC_CONFIG_OV10635_422P_8BIT_1280x800]);
            return status;
        case ISC_CONFIG_OV10635_SYNC:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(ov10635_sync),
                ov10635_sync);
        case ISC_CONFIG_OV10635_ENABLE_STREAMING:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(ov10635_enable_streaming),
                ov10635_enable_streaming);
        default:
            break;
    }

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

    registerData[0] = registerNum >> 8;
    registerData[1] = registerNum & 0xFF;
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

    data[0] = registerNum >> 8;
    data[1] = registerNum & 0xFF;
    memcpy(&data[2], dataBuff, MIN(REG_WRITE_BUFFER, dataLength));

    status = funcs->Write(
        transaction,                         // transaction
        dataLength + REGISTER_ADDRESS_BYTES, // dataLength
        data);                               // data

    return status;
}

static NvMediaStatus
GetTemperature(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    float *temperature)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned int reg = 0x3827;
    unsigned char buff = 0x00;

    status = ReadRegister(handle,
                          transaction,
                          reg,
                          1,
                          &buff);
    if(status != NVMEDIA_STATUS_OK)
        return status;

    if(buff >= 0xc0)
        *temperature = -1 * (256 - buff);
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
    unsigned int reg = 0x300a;
    unsigned char readBuff[2] = {0};

    status = ReadRegister(handle,
                          transaction,
                          reg,
                          2,
                          readBuff);

    if(status != NVMEDIA_STATUS_OK)
        return status;

    if((readBuff[0] != ((ISC_OV10635_CHIP_ID >> 8) & 0xff)) ||
        (readBuff[1] != (ISC_OV10635_CHIP_ID & 0xff))) {
        return NVMEDIA_STATUS_ERROR;
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
    .deviceName = "OV10635 Image Sensor",
    .deviceType = NVMEDIA_ISC_DEVICE_IMAGE_SENSOR,
    .regLength = 2,
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
    .DumpRegisters = DumpRegisters
};

NvMediaISCDeviceDriver *
GetOV10635Driver(void)
{
    return &deviceDriver;
}

