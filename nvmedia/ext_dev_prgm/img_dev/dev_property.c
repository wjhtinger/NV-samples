/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <string.h>
#include "log_utils.h"
#include "dev_property.h"
#include "nvmedia_icp.h"
#include "camera_modules_config.h"
#include "dev_map.h"

// camera mapping csiout IDs
#define EXTIMGDEV_CAMMAP_CSIOUTID(csiOut,i) \
           (unsigned int)((((csiOut & 0xF) == i)*0 + \
                          (((csiOut >> 4) & 0xF) == i)*1 + \
                          (((csiOut >> 8) & 0xF) == i)*2 + \
                          (((csiOut >> 12) & 0xF) == i)*3))

static NvMediaStatus
SetCSIInterface(
    char *csiPortName,
    NvMediaICPInterfaceType *interface
)
{
    if(!csiPortName)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(!strcasecmp(csiPortName, "csi-a"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_A;
    else if(!strcasecmp(csiPortName, "csi-b"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_B;
    else if(!strcasecmp(csiPortName, "csi-c"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_C;
    else if(!strcasecmp(csiPortName, "csi-d"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_D;
    else if(!strcasecmp(csiPortName, "csi-e"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_E;
    else if(!strcasecmp(csiPortName, "csi-f"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_F;
    else if(!strcasecmp(csiPortName, "csi-ab"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB;
    else if(!strcasecmp(csiPortName, "csi-cd"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_CD;
    else if(!strcasecmp(csiPortName, "csi-ef"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_EF;
    else if(!strcasecmp(csiPortName, "tpg0"))
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_TPG0;
    else {
        LOG_ERR("%s: Bad interface-type specified: %s.Using csi-ab as default\n",
                __func__,
                csiPortName);
        *interface = NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB;
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    return NVMEDIA_STATUS_OK;
};

static NvMediaStatus
SetInputFormatProperty(
    char *inputFormat,
    ExtImgDevProperty *property
)
{
    if(!inputFormat)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    property->doubledPixel = NVMEDIA_FALSE;

    if(!strcasecmp(inputFormat, "422p")) {
        property->inputFormatType = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_YUV422;
    } else if(!strcasecmp(inputFormat, "rgb")) {
        property->inputFormatType = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_RGB888;
    } else if(!strcasecmp(inputFormat, "raw8")) {
        property->inputFormatType = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_RAW;
        property->bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_8;
    } else if(!strcasecmp(inputFormat, "raw10")) {
        property->inputFormatType = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_RAW;
        property->bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_10;
    } else if(!strcasecmp(inputFormat, "raw12")) {
        property->inputFormatType = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_RAW;
        property->bitsPerPixel = NVMEDIA_BITS_PER_PIXEL_12;
    } else {
        LOG_ERR("%s: Bad input format specified: %s. Using rgba.\n",
                __func__, inputFormat);
        property->inputFormatType = NVMEDIA_IMAGE_CAPTURE_INPUT_FORMAT_TYPE_RGB888;
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetPixelOrderProperty(
    char *pixelOrder,
    ExtImgDevProperty *property
)
{
    if(!pixelOrder)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(!strcasecmp(pixelOrder, "bggr")) {
        property->pixelOrder = NVMEDIA_RAW_PIXEL_ORDER_BGGR;
    } else if(!strcasecmp(pixelOrder, "rggb")) {
            property->pixelOrder = NVMEDIA_RAW_PIXEL_ORDER_RGGB;
    } else if(!strcasecmp(pixelOrder, "grbg")) {
            property->pixelOrder = NVMEDIA_RAW_PIXEL_ORDER_GRBG;
    } else if(!strcasecmp(pixelOrder, "gbrg")) {
            property->pixelOrder = NVMEDIA_RAW_PIXEL_ORDER_GBRG;
    } else if(!strcasecmp(pixelOrder, "rgba")) {
            // Do nothing
    } else if(strcasecmp(pixelOrder, "yuv")) {
        LOG_ERR("%s: Bad input format specified: %s. Using rgba.\n",
                __func__, pixelOrder);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    return NVMEDIA_STATUS_OK;
}

static ImgProperty *
GetPropertyEntry(
    ImgDevDriver *device,
    ExtImgDevParam *configParam
)
{
    NvU32 i;
    NvMediaBool found = NVMEDIA_FALSE;
    ImgProperty *imgProperty;

    for(i = 0; i < device->numProperties; i++) {
        imgProperty = &device->properties[i];

        if(strcmp(configParam->resolution, imgProperty->resolution))
            continue;

        if(strcmp(configParam->inputFormat, imgProperty->inputFormat))
            continue;

        if(configParam->enableEmbLines &&
           !(imgProperty->embLinesTop || imgProperty->embLinesBottom))
            continue;

        /* when requested frameRate is non-zero, check whether frameRate is supported by module */
        if(configParam->reqFrameRate  &&
           (configParam->reqFrameRate != imgProperty->frameRate)) {
            continue;
        }

        found = NVMEDIA_TRUE;
        break;
    }

    if(!found)
        return NULL;

    return imgProperty;
}

static NvMediaStatus
SetCamMapIdProperty(
    NvMediaBool virtualChannelsEnabled,
    ExtImgDevMapInfo *camMap,
    ExtImgDevProperty *property
)
{
    NvU32 i = 0;
    unsigned int *vcId = property->vcId;

    for (i = 0; i < NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS; i++) {
        if (virtualChannelsEnabled) {
            vcId[i] = (camMap)? EXTIMGDEV_CAMMAP_CSIOUTID(camMap->csiOut, i) : i;
        } else {
            vcId[i] = 0;
        }
    }

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ImgDevSetProperty(
    ImgDevDriver *imgDevice,
    ExtImgDevParam *configParam,
    ExtImgDevice   *extImgDevice
)
{
    ExtImgDevProperty *deviceProperty;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    ImgProperty *imgProperty;
    NvU32 numEnabled = 0, i;

    if(!imgDevice)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!configParam)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!extImgDevice)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    imgProperty = GetPropertyEntry(imgDevice, configParam);
    if(!imgProperty)
        return NVMEDIA_STATUS_NOT_SUPPORTED;

    deviceProperty = &extImgDevice->property;

    /* set interface information */
    status = SetCSIInterface(configParam->interface, &deviceProperty->interface);
    if(status != NVMEDIA_STATUS_OK)
        return NVMEDIA_STATUS_ERROR;

    /* set input format */
    status = SetInputFormatProperty(imgProperty->inputFormat, deviceProperty);
    if(status != NVMEDIA_STATUS_OK)
        return NVMEDIA_STATUS_ERROR;

    /* set number of sensors in the interface */
    extImgDevice->sensorsNum = configParam->sensorsNum;

    /* check number of sensor and number of links enabled */
    if(configParam->camMap) {
        for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
            // get remapped index of link i if CSI remapping bitmask is given
            numEnabled += EXTIMGDEV_MAP_LINK_ENABLED(configParam->camMap->enable, i) ? 1 : 0;
        }
        if(numEnabled < configParam->sensorsNum) {
            LOG_ERR("%s: configured sensorNum is greater than number of enabled links\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    /* set resolution */
    if(sscanf(imgProperty->resolution,
              "%hux%hu",
              &deviceProperty->width,
              &deviceProperty->height) != 2) {
        LOG_ERR("%s: Bad resolution: %s\n", __func__, imgProperty->resolution);
        return NVMEDIA_STATUS_ERROR;
    }

    /* set pixel order */
    status = SetPixelOrderProperty(imgProperty->pixelOrder, deviceProperty);
    if(status != NVMEDIA_STATUS_OK)
        return NVMEDIA_STATUS_ERROR;

    /* set number of emb lines */
    deviceProperty->embLinesTop = imgProperty->embLinesTop;
    deviceProperty->embLinesBottom = imgProperty->embLinesBottom;
    if (imgProperty->pixelFrequency) {
        /* It assumes that each sensor has same pixel frequency */
        deviceProperty->pixelFrequency = imgProperty->pixelFrequency * configParam->sensorsNum;
    } else {
        LOG_WARN("%s: pixel frequency of imgProperty is zero, please set proper pixel frequency for the stability of capture\n",
                 __func__);
    }

    /* set csiout IDs */
    status = SetCamMapIdProperty(configParam->enableVirtualChannels,
                                 configParam->camMap,
                                 deviceProperty);

    /* set frame rate */
    deviceProperty->frameRate = imgProperty->frameRate;

    /* set external sync mode */
    deviceProperty->enableExtSync = configParam->enableExtSync;
    deviceProperty->dutyRatio =
        ((configParam->dutyRatio <= 0.0) || (configParam->dutyRatio >= 1.0)) ?
            0.25 : configParam->dutyRatio;

    /* set driver for the image device */
    extImgDevice->driver = imgDevice;

    return NVMEDIA_STATUS_OK;
}

NvMediaStatus
ImgDevGetModuleConfig(
    NvMediaISCModuleConfig *cameraModuleCfg,
    char *captureModuleName
)
{
    char *strPtr = NULL;
    char *strPtr2 = NULL;
    NvU32 i, size;
    char cameraModuleCfgName[128];

    if((!cameraModuleCfg) || (!captureModuleName))
        return NVMEDIA_STATUS_BAD_PARAMETER;

    // Check length of captureModuleName
    size = strlen(captureModuleName) + 1;
    if(size > sizeof(cameraModuleCfgName))
        return NVMEDIA_STATUS_OUT_OF_MEMORY;

    // Copy captureModuleName to local
    strncpy(cameraModuleCfgName, captureModuleName, size);

    // Get cameraModuleCfgName
    strPtr = strtok(cameraModuleCfgName, "_"); //Get 1st part before 1st "_"
    if(!strPtr)
        return NVMEDIA_STATUS_ERROR;

    strncpy(cameraModuleCfg->cameraModuleCfgName, strPtr, strlen(strPtr));
    strcat(cameraModuleCfg->cameraModuleCfgName, "_");
    cameraModuleCfg->cameraModuleCfgName[strlen(strPtr) + 1] = '\0';

    while (strPtr != NULL) {
      strPtr2 = strPtr; //Get the last part after last "_"

      strPtr = strtok(NULL, "_");
    }

    strcat(cameraModuleCfg->cameraModuleCfgName, strPtr2);
    LOG_INFO("%s: moduleCfgName: %s\n", __func__, cameraModuleCfg->cameraModuleCfgName);

    // Get cameraModuleConfig
    cameraModuleCfg->cameraModuleConfigPass1 = NULL;
    cameraModuleCfg->cameraModuleConfigPass2 = NULL;
    size = sizeof(cameraConfigString)/sizeof(cameraConfigString[0]);

    strncpy(cameraModuleCfgName,
            cameraModuleCfg->cameraModuleCfgName,
            strlen(cameraModuleCfg->cameraModuleCfgName) + 1);

    strPtr = strcat(cameraModuleCfgName, "_pass1");
    for(i =0; i < size ; i++) {
        if(!strncmp(cameraModuleCfgName, cameraConfigString[i], strlen(cameraConfigString[i]))) {
            cameraModuleCfg->cameraModuleConfigPass1 = cameraConfigTable[i];
            break;
        }
    }

    strcpy(cameraModuleCfgName, cameraModuleCfg->cameraModuleCfgName);
    strPtr = strcat(cameraModuleCfgName, "_pass2");
    for(i =0; i < size ; i++) {
        if(!strncmp(cameraModuleCfgName, cameraConfigString[i], strlen(cameraConfigString[i]))) {
            cameraModuleCfg->cameraModuleConfigPass2 = cameraConfigTable[i];
            break;
        }
    }

    return NVMEDIA_STATUS_OK;
}
