/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __CHECK_VERSION_H__
#define __CHECK_VERSION_H__

#include "string.h"
#include "nvmedia.h"
#include "nvmedia_isc.h"
#include "nvmedia_icp.h"
#include "nvmedia_ipp.h"
#include "nvmedia_idp.h"
#include "nvmedia_iep.h"
#include "img_dev.h"

NvMediaStatus
CheckModulesVersion(void);

#endif

