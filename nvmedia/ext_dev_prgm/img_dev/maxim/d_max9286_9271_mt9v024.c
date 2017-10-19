/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include "d_max9286_9271_mt9v024.h"
#include "isc_max9271.h"
#include "isc_max9286.h"
#include "log_utils.h"
#include "error_max9286.h"
#include "dev_property.h"

static void
Deinit(ExtImgDevice *device)
{
    return;
}

static ExtImgDevice *
Init(ExtImgDevParam *configParam)
{
    return NULL;
}

static NvMediaStatus
RegisterCallback(
    ExtImgDevice * device,
    NvU32 sigNum,
    void (*cb)(void *),
    void *context
)
{
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetError(
    ExtImgDevice * device,
    NvU32 *link,
    ExtImgDevFailureType *errorType
)
{
    /* TBD : Check status of module */
    return _GetError_max9286(device->iscDeserializer, link, errorType);
}

static ImgProperty properties[] = {
                   /* resolution, oscMHz, fps,  embTop, embBottom, inputFormat, pixelOrder */
    IMG_PROPERTY_ENTRY_NO_PCLK(752x480,     27,    60,     0,         0,       raw12,       bggr), /* TBD : this module is mono but set as bggr */
};

static ImgDevDriver device = {
    .name = "d_max9286_9271_mt9v024",
    .Init = Init,
    .Deinit = Deinit,
    .RegisterCallback = RegisterCallback,
    .GetError = GetError,
    .properties = properties,
    .numProperties = sizeof(properties) / sizeof(properties[0]),
};

ImgDevDriver *
GetDriver_d_max9286_9271_mt9v024(void)
{
    return &device;
}
