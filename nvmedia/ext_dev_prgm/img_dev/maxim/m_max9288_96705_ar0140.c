/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "m_max9288_96705_ar0140.h"
#include "isc_max9288.h"
#include "isc_max96705.h"
#include "log_utils.h"
#include "dev_property.h"
#include "dev_map.h"

static void
Deinit(ExtImgDevice *device)
{
    if(!device)
        return;

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
    NvS32 config;

    if(!configParam || !device)
        return NVMEDIA_STATUS_ERROR;

    // Set deserializer defaults
    LOG_DBG("%s: Set deserializer device defaults\n", __func__);
    status = NvMediaISCSetDefaults(device->iscDeserializer);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set deserializer defaults\n", __func__);
        return status;
    }

    // Get desrializer config set
    status = GetMAX9288ConfigSet(configParam->resolution,
                                 &config);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to get config set\n", __func__);
        return status;
    }

    // Set deserializer configuration
    LOG_DBG("%s: Set deserializer configuration\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                       config);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set deserializer configuration\n", __func__);
        return status;
    }

    // Set data type to RGB using oLDI format
    LOG_DBG("%s: Set deserializer data type to RGB oLDI format\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                       ISC_CONFIG_MAX9288_SET_DATA_TYPE_RGB_OLDI);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set deserializer data type\n", __func__);
        return status;
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
    status = ImgDevSetProperty(GetDriver_m_max9288_96705_ar0140(),
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

    status = SetupLink(configParam, device);
    if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to setup config link\n", __func__);
            goto failed;
    }

    return device;

failed:
    Deinit(device);

    return NULL;
}

static NvMediaStatus
Start(ExtImgDevice *device)
{
    if(!device)
        return NVMEDIA_STATUS_NOT_SUPPORTED;

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
                  /* resolution,        oscMHz, fps, embTop, embBottom, inputFormat, pixelOrder */
    IMG_PROPERTY_ENTRY_NO_PCLK(1280x720,     24,  30,    0,       0,       rgb,        rgba),
    IMG_PROPERTY_ENTRY_NO_PCLK(960x540,      24,  30,    0,       0,       rgb,        rgba),
};

static ImgDevDriver device = {
    .name = "m_max9288_96705_ar0140",
    .Init = Init,
    .Deinit = Deinit,
    .Start = Start,
    .RegisterCallback = RegisterCallback,
    .GetError = GetError,
    .properties = properties,
    .numProperties = sizeof(properties) / sizeof(properties[0]),
};

ImgDevDriver *
GetDriver_m_max9288_96705_ar0140(void)
{
    return &device;
}
