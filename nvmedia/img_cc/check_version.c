/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "check_version.h"

NvMediaStatus
CheckModulesVersion(void)
{
    NvMediaVersion version;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    memset(&version, 0, sizeof(NvMediaVersion));

    NVMEDIA_SET_VERSION(version, NVMEDIA_VERSION_MAJOR,
                                 NVMEDIA_VERSION_MINOR);
    status = NvMediaCheckVersion(&version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(version, NVMEDIA_IMAGE_VERSION_MAJOR,
                                 NVMEDIA_IMAGE_VERSION_MINOR);
    status = NvMediaImageCheckVersion(&version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(version, NVMEDIA_ISC_VERSION_MAJOR,
                                 NVMEDIA_ISC_VERSION_MINOR);
    status = NvMediaISCCheckVersion(&version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(version, NVMEDIA_ICP_VERSION_MAJOR,
                                 NVMEDIA_ICP_VERSION_MINOR);
    status = NvMediaICPCheckVersion(&version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(version, NVMEDIA_2D_VERSION_MAJOR,
                                 NVMEDIA_2D_VERSION_MINOR);
    status = NvMedia2DCheckVersion(&version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(version, NVMEDIA_IDP_VERSION_MAJOR,
                                 NVMEDIA_IDP_VERSION_MINOR);
    status = NvMediaIDPCheckVersion(&version);

    return status;
}
