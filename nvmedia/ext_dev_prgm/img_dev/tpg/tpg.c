/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "tpg.h"
#include "log_utils.h"
#include "dev_property.h"
#include "dev_map.h"

static void
Deinit(ExtImgDevice *device)
{
    if(!device)
        return;

    free(device);

    return;
}

static ExtImgDevice *
Init(ExtImgDevParam *configParam)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    ExtImgDevice *device;

    if(!configParam)
        return NULL;

    device = calloc(1, sizeof(ExtImgDevice));
    if(!device) {
        LOG_ERR("%s: out of memory\n", __func__);
        return NULL;
    }

    LOG_INFO("%s: Set image device property\n", __func__);
    status = ImgDevSetProperty(GetDriver_tpg(),
                               configParam,
                               device);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: doesn't support the given property, check input param and image property\n",
                 __func__);
        goto failed;
    }

    return device;

failed:
    Deinit(device);

    return NULL;
}

static NvMediaStatus
RegisterCallback(
    ExtImgDevice *device,
    NvU32 sigNum,
    void (*cb)(void *),
    void *context
)
{
    if(!device)
        return NVMEDIA_STATUS_ERROR;

    return NVMEDIA_STATUS_OK;
}

static ImgProperty properties[] = {
                          /* resolution, oscMHz, fps, embTop, embBottom, inputFormat, pixelOrder */
    IMG_PROPERTY_ENTRY_NO_PCLK(1280x800,     24,  30,      0,         0,        rgb,       rggb),
    IMG_PROPERTY_ENTRY_NO_PCLK(1280x1080,    24,  30,      0,         0,        rgb,       rggb),
    IMG_PROPERTY_ENTRY_NO_PCLK(1920x1280,    24,  30,      0,         0,        rgb,       rggb),
};


static ImgDevDriver device = {
    .name = "tpg",
    .Init = Init,
    .Deinit = Deinit,
    .Start = NULL,
    .RegisterCallback = RegisterCallback,
    .GetError = NULL,
    .properties = properties,
    .numProperties = sizeof(properties) / sizeof(properties[0]),
};

ImgDevDriver *
GetDriver_tpg(void)
{
    return &device;
}
