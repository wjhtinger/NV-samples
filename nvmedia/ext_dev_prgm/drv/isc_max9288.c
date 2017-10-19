/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "nvmedia_isc.h"
#include "isc_max9288.h"

#define REGISTER_ADDRESS_BYTES  1
#define REG_WRITE_BUFFER        32

#if !defined(__INTEGRITY)
#define MIN(a,b)            (((a) < (b)) ? (a) : (b))
#endif
#define GET_SIZE(x)         sizeof(x)
#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   &x[1]
#define SET_NEXT_BLOCK(x)   x += (x[0] + 1)

unsigned char max9288_defaults[] = {
    2, 0x09, 0x40,  // Enable automatic pixel count
    2, 0x60, 0x33,  // Set data type to 8-bit YUV 422 normal mode
};

unsigned char max9288_enable_data_lanes_0123[] = {
    2, 0x65, 0x37,  // Enable data lanes 0, 1, 2 and 3
};

unsigned char max9288_set_data_type_rgb_oldi[] = {
    2, 0x60, 0x10,  // Set data type to RGB888 using oLDI format
};

unsigned char max9288_set_bws_high[] = {
    2, 0x17, 0x1D,  // Set deserializer BWS to 1
};

unsigned char max9288_set_bws_open[] = {
    2, 0x17, 0x1E,  // Set deserializer to high bandwidth mode
};

typedef struct {
    NvMediaISCSupportFunctions *funcs;
    ContextMAX9288 ctx;
} _DriverHandle;

static NvMediaStatus
InvertHsyncVsync(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaBool invertHsyncVsync)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[2];
    NvMediaStatus status;

    if(!handle || !transaction)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = MAX9288_REG_HSYNC_VSYNC; // Register address

    status = funcs->Read(
        transaction,            // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        data,                   // regData
        1,                      // dataLength
        &data[1]);              // data
    if(status != NVMEDIA_STATUS_OK)
        return status;

    if(invertHsyncVsync)
        // Set first 2 bits
        data[1] |= 0xC0;
    else
        // Clear first 2 bits
        data[1] &= ~0xC0;

    status = funcs->Write(
        transaction, // transaction
        2,           // dataLength
        data);       // data
    if(status != NVMEDIA_STATUS_OK)
        return status;

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
        status = funcs->Write(
            transaction,                 // transaction
            GET_BLOCK_LENGTH(arrayData), // dataLength
            GET_BLOCK_DATA(arrayData));  // data
        if(status != NVMEDIA_STATUS_OK) {
            printf("MAX9288: error: wri2c   0x%.2X    0x%.2X\n",
                (unsigned int)arrayData[1],
                (unsigned int)arrayData[2]);
        }

{
        unsigned char data[1] = {0};

        // TODO: Read is used for register programming stability.
        // This delay is needed by certain platform
        // Remove once possible.
        status = funcs->Read(
            transaction,                // transaction
            REGISTER_ADDRESS_BYTES,     // regLength
            GET_BLOCK_DATA(arrayData),  // regData
            1,                          // dataLength
            data);                      // dataBuff

}
        arrayByteLength -= GET_BLOCK_LENGTH(arrayData) + 1;
        SET_NEXT_BLOCK(arrayData);
    }

    return NVMEDIA_STATUS_OK;
}

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

    if(clientContext) {
        memcpy(&driverHandle->ctx, clientContext, sizeof(ContextMAX9288));
    }

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
SetDefaults(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaISCSupportFunctions *funcs;
    unsigned char offset[1];
    unsigned char dataBuff[1];

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    offset[0] = 0x00;

    status = funcs->Read(
        transaction,    // transaction
        REGISTER_ADDRESS_BYTES, // regLength
        offset,   // regData
        1,     // dataLength
        dataBuff);      // data

    if(status != NVMEDIA_STATUS_OK)
        return status;

    status = WriteArray(
                handle,
                transaction,
                GET_SIZE(max9288_defaults),
                max9288_defaults);

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
        case ISC_CONFIG_MAX9288_DEFAULT:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9288_defaults),
                max9288_defaults);
        case ISC_CONFIG_MAX9288_ENABLE_LANES_0123:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9288_enable_data_lanes_0123),
                max9288_enable_data_lanes_0123);
        case ISC_CONFIG_MAX9288_SET_DATA_TYPE_RGB_OLDI:
            return WriteArray(
                handle,
                transaction,
                GET_SIZE(max9288_set_data_type_rgb_oldi),
                max9288_set_data_type_rgb_oldi);
        case ISC_CONFIG_MAX9288_RGB_1280x720:
            status = WriteArray(
                        handle,
                        transaction,
                        GET_SIZE(max9288_set_bws_open),
                        max9288_set_bws_open);
            if(status != NVMEDIA_STATUS_OK)
                return status;

            return InvertHsyncVsync(
                handle,
                transaction,
                NVMEDIA_FALSE);
        case ISC_CONFIG_MAX9288_RGB_960x540:
            status = WriteArray(
                handle,
                transaction,
                GET_SIZE(max9288_set_bws_high),
                max9288_set_bws_high);
            if(status != NVMEDIA_STATUS_OK)
                return status;

            return InvertHsyncVsync(
                handle,
                transaction,
                NVMEDIA_TRUE);
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
WriteParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        unsigned int parameterSize,
        void *parameter)
{
    WriteParametersParamMAX9288 *param = parameter;

    if (!param)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    switch(parameterType) {
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
    .deviceName = "Maxim 9288 Deserializer",
    .deviceType = NVMEDIA_ISC_DEVICE_DESERIALIZER,
    .regLength = 1,
    .dataLength = 1,
    .DriverCreate = DriverCreate,
    .DriverDestroy = DriverDestroy,
    .SetDefaults = SetDefaults,
    .SetDeviceConfig = SetDeviceConfig,
    .ReadRegister = ReadRegister,
    .WriteRegister = WriteRegister,
    .WriteParameters = WriteParameters,
    .DumpRegisters = DumpRegisters
};

NvMediaISCDeviceDriver *
GetMAX9288Driver(void)
{
    return &deviceDriver;
}

NvMediaStatus
GetMAX9288ConfigSet(
        char *resolution,
        int *configSet
        )
{
    if(!resolution || !configSet)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(!strcasecmp(resolution, "1280x720")) {
        *configSet = ISC_CONFIG_MAX9288_RGB_1280x720;
    } else if(!strcasecmp(resolution, "960x540")) {
        *configSet = ISC_CONFIG_MAX9288_RGB_960x540;
    } else {
        printf("%s: Resolution %s is not supported\n", __func__, resolution);
        return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}
