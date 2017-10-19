/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "ref_max9288_96705_ov10635.h"
#include "isc_max9288.h"
#include "isc_max96705.h"
#include "isc_ov10635.h"
#include "log_utils.h"
#include "dev_property.h"
#include "dev_map.h"

static void
Deinit(ExtImgDevice *device)
{
    if(!device)
        return;

    if(device->iscBroadcastSensor)
        NvMediaISCDeviceDestroy(device->iscBroadcastSensor);
    if(device->iscBroadcastSerializer)
        NvMediaISCDeviceDestroy(device->iscBroadcastSerializer);
    if(device->iscDeserializer)
        NvMediaISCDeviceDestroy(device->iscDeserializer);
    if(device->iscRoot)
        NvMediaISCRootDeviceDestroy(device->iscRoot);

    free(device);

    return;
}

static
NvMediaStatus
SetupLink(
    ExtImgDevParam *configParam,
    ExtImgDevice *device)
{
    NvMediaStatus status;

    if(!configParam || !device)
        return NVMEDIA_STATUS_ERROR;

    if(device->iscBroadcastSerializer){
        // Set serializer defaults
        LOG_DBG("%s: Set Serializer device defaults\n", __func__);
        status = NvMediaISCSetDefaults(device->iscBroadcastSerializer);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set Serializer defaults\n", __func__);
            return status;
        }
        usleep(2000);  //wait 2ms

        // Enable serializer reverse channel
        LOG_DBG("%s: Enable Serializer reverse channel\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                ISC_CONFIG_MAX96705_ENABLE_REVERSE_CHANNEL);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable Serializer reverse channel\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
        usleep(7000);  //wait 7ms

        // Set serializer to auto config link
        LOG_DBG("%s: Set serializer to auto configure link\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                ISC_CONFIG_MAX96705_SET_AUTO_CONFIG_LINK);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set serializer to auto configure link\n",
                     __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    // Set deserializer defaults
    LOG_DBG("%s: Set deserializer device defaults\n", __func__);
    status = NvMediaISCSetDefaults(device->iscDeserializer);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set deserializer defaults\n", __func__);
        return status;
    }

    if(device->iscBroadcastSerializer){
        // Set serializer to double input mode
        LOG_DBG("%s: Set serializer to double input mode\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                ISC_CONFIG_MAX96705_DOUBLE_INPUT_MODE);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set serializer to double input mode\n",
                     __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        // Configure serializer to COAX
        LOG_DBG("%s: Configure serializer to COAX\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                ISC_CONFIG_MAX96705_CONFIG_SERIALIZER_COAX);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to configure serializer to COAX\n",
                     __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        // Set crossbar settings
        LOG_DBG("%s: Set serializer corssbar settings\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                ISC_CONFIG_MAX96705_SET_XBAR);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set serializer crossbar settings\n",
                     __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    if(device->iscBroadcastSensor) {
        LOG_DBG("%s: Check sensor is present\n", __func__);
        status = NvMediaISCCheckPresence(device->iscBroadcastSensor);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Image sensor device is not present\n", __func__);
            return status;
        }

        LOG_DBG("%s: Set image sensor defaults\n", __func__);
        status = NvMediaISCSetDefaults(device->iscBroadcastSensor);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set image sensor defaults\n", __func__);
            return status;
        }

        // Set for sync mode
        LOG_DBG("%s: Set sensor configuration for enabling sync mode\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                            ISC_CONFIG_OV10635_SYNC);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set sensor configuration for enabling sync mode\n", __func__);
                return status;
        }
    }

    if(device->iscBroadcastSerializer) {
        // Enable serial link
        LOG_DBG("%s: Enable serial link\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                           ISC_CONFIG_MAX96705_ENABLE_SERIAL_LINK);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable serial link\n", __func__);
            return status;
        }
        usleep(10000);  //wait 10ms
    }

    return NVMEDIA_STATUS_OK;
}

static ExtImgDevice *
Init(
    ExtImgDevParam *configParam)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    ExtImgDevice *device = NULL;

    if(!configParam)
        return NULL;

    device = calloc(1, sizeof(ExtImgDevice));
    if(!device) {
        LOG_ERR("%s: out of memory\n", __func__);
        return NULL;
    }

    LOG_INFO("%s: Set image device property\n", __func__);
    status = ImgDevSetProperty(GetDriver_ref_max9288_96705_ov10635(),
                                        configParam,
                                        device);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: doesn't support the given property, check input param and image property\n",
                 __func__);
        goto failed;
    }

    LOG_INFO("%s: Create root device\n", __func__);
    device->iscRoot = NvMediaISCRootDeviceCreate(
                             ISC_ROOT_DEVICE_CFG(device->property.interface,
                                 configParam->enableSimulator?
                                     NVMEDIA_ISC_I2C_SIMULATOR :
                                     configParam->i2cDevice), // port
                             32,                     // queueElementsNumber
                             256,                    // queueElementSize
                             NVMEDIA_FALSE);         // enableExternalTransactions
    if(!device->iscRoot) {
        LOG_ERR("%s: Failed to create NvMedia ISC root device\n", __func__);
        goto failed;
    }

    // Delay for 50 ms in order to let sensor power on
    usleep(50000);

    if(configParam->desAddr) {
        // Create the deserializer device
        LOG_INFO("%s: Create deserializer device on address 0x%x\n", __func__, configParam->desAddr);
        device->iscDeserializer = NvMediaISCDeviceCreate(
                            device->iscRoot,        // rootDevice
                            NULL,                   // parentDevice
                            0,                      // instanceNumber
                            configParam->desAddr,   // deviceAddress
                            GetMAX9288Driver(),     // deviceDriver
                            NULL);                  // advancedConfig
        if(!device->iscDeserializer) {
            LOG_ERR("%s: Failed to create deserializer device\n", __func__);
            goto failed;
        }
    }

    if(configParam->brdcstSerAddr) {
        // Create broadcast serializer device
        LOG_INFO("%s: Create broadcast serializer device on address 0x%x\n", __func__,
                          configParam->brdcstSerAddr);
        device->iscBroadcastSerializer = NvMediaISCDeviceCreate(
                                            device->iscRoot,
                                            device->iscDeserializer,
                                            0,
                                            configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                                                configParam->brdcstSerAddr,
                                            GetMAX96705Driver(),
                                            NULL);
        if(!device->iscBroadcastSerializer) {
            LOG_ERR("%s: Failed to create broadcase serializer device\n", __func__);
            goto failed;
        }
    }

    if(configParam->brdcstSensorAddr) {
        // Create the image sensor device
        LOG_INFO("%s: Create broadcast sensor device on address 0x%x\n", __func__,
                         configParam->brdcstSensorAddr);
        device->iscBroadcastSensor = NvMediaISCDeviceCreate(
                                        device->iscRoot,
                                        device->iscBroadcastSerializer,
                                        0,
                                        configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                                             configParam->brdcstSensorAddr,
                                        GetOV10635Driver(),
                                        NULL);
        if(!device->iscBroadcastSensor) {
            LOG_ERR("%s: Failed to create broadcast sensor device\n", __func__);
            goto failed;
        }
    }

    status = SetupLink(configParam, device);
    if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to setup config link\n", __func__);
            goto failed;
    }

    device->simulator = configParam->enableSimulator;

    return device;

failed:
    Deinit(device);

    return NULL;
}

static NvMediaStatus
Start(ExtImgDevice *device)
{
    NvMediaStatus status;

    if(!device)
        return NVMEDIA_STATUS_NOT_SUPPORTED;

    LOG_DBG("%s: Enable sensor streaming\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                       ISC_CONFIG_OV10635_ENABLE_STREAMING);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enable sensor streaming\n", __func__);
        return status;
    }

    // Enable data lanes d0-d3
    LOG_DBG("%s: Enable csi out\n", __func__);
    return NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                     ISC_CONFIG_MAX9288_ENABLE_LANES_0123);
}

static NvMediaStatus
RegisterCallback(
    ExtImgDevice *device,
    NvU32 sigNum,
    void (*cb)(void *),
    void *context)
{
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetError(
    ExtImgDevice *device,
    NvU32 *link,
    ExtImgDevFailureType *errorType)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static ImgProperty properties[] = {
                   /* resolution, oscMHz, fps,   pclk,  embTop, embBottom, inputFormat, pixelOrder */
    IMG_PROPERTY_ENTRY(1280x800,     24,  30, 48006000,      0,         0,        422p,        yuv),
};

static ImgDevDriver device = {
    .name = "ref_max9288_96705_ov10635",
    .Init = Init,
    .Deinit = Deinit,
    .Start = Start,
    .RegisterCallback = RegisterCallback,
    .GetError = GetError,
    .properties = properties,
    .numProperties = sizeof(properties) / sizeof(properties[0]),
};

ImgDevDriver *
GetDriver_ref_max9288_96705_ov10635(void)
{
    return &device;
}
