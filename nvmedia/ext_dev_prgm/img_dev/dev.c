/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include "stdio.h"
#include "dev_priv.h"
#include "dev_list.h"
#include "log_utils.h"

ExtImgDevice *
ExtImgDevInit(ExtImgDevParam *configParam)
{
    ImgDevDriver *imgDev = NULL;

    if(!configParam)
        return NULL;

    imgDev = ImgGetDevice(configParam->moduleName);
    if(!imgDev)
        return NULL;

    if(!imgDev->Init)
        return NULL;

    return imgDev->Init(configParam);
}

void
ExtImgDevDeinit(ExtImgDevice *device)
{
    ImgDevDriver *imgDev = NULL;

    if(!device)
        return;

    imgDev = (ImgDevDriver *)device->driver;
    if(!imgDev)
        return;

    if(imgDev->Deinit)
        imgDev->Deinit(device);
}

NvMediaStatus
ExtImgDevStart(ExtImgDevice *device)
{
    ImgDevDriver *imgDev = NULL;

    if(!device)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    imgDev = (ImgDevDriver *)device->driver;
    if(!imgDev)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(imgDev->Start)
        return imgDev->Start(device);

    return NVMEDIA_STATUS_OK;
}

void
ExtImgDevStop(ExtImgDevice *device)
{
    /* do nothing; stop will be called from de-init */
    return;
}

NvMediaStatus
ExtImgDevGetError(
    ExtImgDevice *device,
    NvU32 *link,
    ExtImgDevFailureType *errorType
)
{
    ImgDevDriver *imgDev = NULL;

    if(!device)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!link)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    imgDev = (ImgDevDriver *)device->driver;
    if(!imgDev)
        return NVMEDIA_STATUS_ERROR;

    if(!imgDev->GetError)
        return NVMEDIA_STATUS_NOT_SUPPORTED;

    return imgDev->GetError(device, link, errorType);
}

NvMediaStatus
ExtImgDevRegisterCallback(
    ExtImgDevice *device,
    NvU32 sigNum,
    void (*cb)(void *),
    void *context
)
{
    ImgDevDriver *imgDev = NULL;

    if(!device)
        return NVMEDIA_STATUS_BAD_PARAMETER;
    if(!cb)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    imgDev = (ImgDevDriver *)device->driver;
    if(!imgDev)
        return NVMEDIA_STATUS_ERROR;

    if(!imgDev->RegisterCallback)
        return NVMEDIA_STATUS_NOT_SUPPORTED;

    return imgDev->RegisterCallback(device, sigNum, cb, context);
}

NvMediaStatus
ExtImgDevCheckVersion(
    ExtImgDevVersion *version
)
{
    if(!version)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if((version->major != EXTIMGDEV_VERSION_MAJOR) ||
       (version->minor != EXTIMGDEV_VERSION_MINOR)) {
        LOG_ERR("%s: Incompatible version found \n", __func__);
        LOG_ERR("%s: Core version: %d.%d\n", __func__,
            EXTIMGDEV_VERSION_MAJOR, EXTIMGDEV_VERSION_MINOR);
        LOG_ERR("%s: Client version: %d.%d\n", __func__,
            version->major, version->minor);
        return NVMEDIA_STATUS_INCOMPATIBLE_VERSION;
    }
    return NVMEDIA_STATUS_OK;
}
