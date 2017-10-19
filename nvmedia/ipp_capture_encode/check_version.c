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
    NvMediaVersion nvm_version;
    ExtImgDevVersion imgdev_version;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    memset(&nvm_version, 0, sizeof(NvMediaVersion));
    memset(&imgdev_version, 0, sizeof(ExtImgDevVersion));

    EXTIMGDEV_SET_VERSION(imgdev_version, EXTIMGDEV_VERSION_MAJOR,
                                          EXTIMGDEV_VERSION_MINOR);
    status = ExtImgDevCheckVersion(&imgdev_version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(nvm_version, NVMEDIA_VERSION_MAJOR,
                                     NVMEDIA_VERSION_MINOR);
    status = NvMediaCheckVersion(&nvm_version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(nvm_version, NVMEDIA_IMAGE_VERSION_MAJOR,
                                     NVMEDIA_IMAGE_VERSION_MINOR);
    status = NvMediaImageCheckVersion(&nvm_version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(nvm_version, NVMEDIA_ISC_VERSION_MAJOR,
                                     NVMEDIA_ISC_VERSION_MINOR);
    status = NvMediaISCCheckVersion(&nvm_version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(nvm_version, NVMEDIA_ICP_VERSION_MAJOR,
                                     NVMEDIA_ICP_VERSION_MINOR);
    status = NvMediaICPCheckVersion(&nvm_version);
    if (status != NVMEDIA_STATUS_OK)
        return status;

//    NVMEDIA_SET_VERSION(nvm_version, NVMEDIA_IPP_VERSION_MAJOR,
//                                     NVMEDIA_IPP_VERSION_MINOR);
//    status = NvMediaIPPCheckVersion(&nvm_version);
//    if (status != NVMEDIA_STATUS_OK)
//        return status;

    NVMEDIA_SET_VERSION(nvm_version, NVMEDIA_IEP_VERSION_MAJOR,
                                     NVMEDIA_IEP_VERSION_MINOR);
    status = NvMediaIEPCheckVersion(&nvm_version);
    if (status != NVMEDIA_STATUS_OK)
            return status;

    NVMEDIA_SET_VERSION(nvm_version, NVMEDIA_IDP_VERSION_MAJOR,
                                     NVMEDIA_IDP_VERSION_MINOR);
    status = NvMediaIDPCheckVersion(&nvm_version);

    return status;
}
