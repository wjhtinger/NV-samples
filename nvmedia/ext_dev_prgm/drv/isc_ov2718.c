/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved. All
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
#include "isc_ov2718.h"

typedef struct {
    NvMediaISCSupportFunctions *funcs;
} _DriverHandle;

#define REGISTER_ADDRESS_BYTES  2
#define REG_WRITE_BUFFER        32
#define MIN(a,b)            (((a) < (b)) ? (a) : (b))

#define GET_SIZE(x)         sizeof(x)
#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   &x[1]
#define SET_NEXT_BLOCK(x)   x += (x[0] + 1)

unsigned char ov2718_data[] = {
    4, 0x07, 0x72, 0x00, 0x02
};

static NvMediaStatus
ReadRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int registerNum,
    unsigned int dataLength,
    unsigned char *dataBuff);


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
            printf("error: wri2c   0x%.2X    0x%.2X\n",
                (unsigned int)arrayData[1],
                (unsigned int)arrayData[2]);
        }

        arrayByteLength -= GET_BLOCK_LENGTH(arrayData) + 1;
        SET_NEXT_BLOCK(arrayData);
    }

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
SetDefaults(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetDeviceConfig(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int enumeratedDeviceConfig)
{
	unsigned char buf[2] = {0, 0};
    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_OV2718:
			return NVMEDIA_STATUS_OK;
			break;
		
		case ISC_CONFIG_OV2718_ENABLE_STREAMING:
			SetI2cFunOv(((_DriverHandle *)handle)->funcs, transaction);
			//XC7027MIPIOpen();
			OV2718MIPIOpen();
			return NVMEDIA_STATUS_OK;
			break;
		
        default:
             break;
    }

    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaStatus
WriteParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        unsigned int parameterSize,
        void *parameter)
{
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
	const NvMediaISCSupportFunctions *funcs;
	unsigned char registerData[REGISTER_ADDRESS_BYTES];
	NvMediaStatus status;
	
	if((handle == NULL) || (transaction == NULL) || (dataBuff == NULL)) {
		return NVMEDIA_STATUS_BAD_PARAMETER;
	}
	
	funcs = ((_DriverHandle *)handle)->funcs;
	
	registerData[0] = registerNum >> 8;
	registerData[1] = (registerNum & (unsigned int)0xFF);
	status = funcs->Read(
		transaction,
		REGISTER_ADDRESS_BYTES, // regLength
		registerData,	   // regData
		dataLength, 	   // dataLength
		dataBuff);		   // data
	
	if(status != NVMEDIA_STATUS_OK) {
		LOG_ERR("%s: sensor read failed: 0x%x, length %d\n", __func__, registerNum, dataLength);
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
	const NvMediaISCSupportFunctions *funcs;
	unsigned char data[REGISTER_ADDRESS_BYTES + REG_WRITE_BUFFER];
	NvMediaStatus status;

	if((handle == NULL) || (transaction  == NULL) || (dataBuff == NULL)) {
		return NVMEDIA_STATUS_BAD_PARAMETER;
	}

	if(dataLength > REG_WRITE_BUFFER){
		LOG_ERR("$s: dataLength two big[%d] \n", __func__, dataLength);
		return NVMEDIA_STATUS_BAD_PARAMETER;
	}

	funcs = ((_DriverHandle *)handle)->funcs;

	data[0] = registerNum >> 8;
	data[1] = registerNum & (unsigned int)0xFF;
	(void)memcpy(&data[2], dataBuff, dataLength);

	status = funcs->Write(
		transaction,
		dataLength + (unsigned int)REGISTER_ADDRESS_BYTES,	  // dataLength
		data);							   // data

	if(status != NVMEDIA_STATUS_OK) {
		LOG_ERR("%s: sensor write failed: 0x%x, length %d\n", __func__, registerNum, dataLength);
	}

    return status;
}

static NvMediaStatus
GetTemperature(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    float *temperature)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char cmd[1];
    unsigned char data[8];
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    cmd[0] = 0x0a;

    status = funcs->Read(
        transaction,    // transaction
        1,              // regLength
        cmd,            // regData
        8,              // dataLength
        data);          // data

    return status;
}

static NvMediaStatus
CheckPresence(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char cmd[1];
    unsigned char data[8];
    NvMediaStatus status;

    if(!handle)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    cmd[0] = 0x01;

    status = funcs->Read(
        transaction,    // transaction
        1,              // regLength
        cmd,            // regData
        4,              // dataLength
        data);          // data

    if(status != NVMEDIA_STATUS_OK)
        return status;

    if(!(data[0] & (1 << 0)))
        return NVMEDIA_STATUS_ERROR;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DumpRegisters(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaStatus
SetExposure(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaISCExposureControl *exposureControl)
{
     return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetWBGain(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaISCWBGainControl *wbControl)
{
    return NVMEDIA_STATUS_OK;
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
    return NVMEDIA_STATUS_OK;
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
     return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetSensorProperties(
    NvMediaISCDriverHandle *handle,
    NvMediaISCSensorProperties *properties)
{
    memset(properties, 0, sizeof(*properties));
    properties->frameRate = 20.0f;
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetSensorAttr(
    NvMediaISCDriverHandle *handle,
    NvMediaISCSensorAttrType type,
    unsigned int size,
    void *attribute)
{
    if(!handle || !attribute) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    switch(type) {
        case NVMEDIA_ISC_SENSOR_ATTR_FRAME_RATE:
            if (size != sizeof(float)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }
            *((float *) attribute) = 30.0f;
            return NVMEDIA_STATUS_OK;
        default:
            return NVMEDIA_STATUS_NOT_SUPPORTED;
    }
}

static NvMediaISCDeviceDriver deviceDriver = {
    .deviceName             = "Example ISC Device",
    .deviceType             = NVMEDIA_ISC_DEVICE_IMAGE_SENSOR,
    .regLength              = REGISTER_ADDRESS_BYTES,
    .dataLength             = 2,
    .DriverCreate           = DriverCreate,
    .DriverDestroy          = DriverDestroy,
    .CheckPresence          = CheckPresence,
    .EnableLink             = EnableLink,
    .GetSensorFrameId       = GetSensorFrameId,
    .ParseEmbeddedData      = ParseEmbeddedData,
    .SetDefaults            = SetDefaults,
    .SetDeviceConfig        = SetDeviceConfig,
    .GetTemperature         = GetTemperature,
    .ReadRegister           = ReadRegister,
    .WriteRegister          = WriteRegister,
    .SetExposure            = SetExposure,
    .SetWBGain              = SetWBGain,
    .WriteParameters        = WriteParameters,
    .ReadParameters         = ReadParameters,
    .DumpRegisters          = DumpRegisters,
    .GetSensorProperties    = GetSensorProperties,
    .GetSensorAttr          = GetSensorAttr
};

NvMediaISCDeviceDriver *
GetOV2718Driver(void)
{
    return &deviceDriver;
}
