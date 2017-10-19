/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#ifndef _DEV_LIST_H_
#define _DEV_LIST_H_

#include "dev_priv.h"

typedef enum {
    REF_MAX9286_9271_OV10635,
    REF_MAX9286_9271_OV10640,
    C_MAX9286_9271_OV10640LSOFF,
    C_MAX9286_9271_OV10640,
    D_MAX9286_9271_MT9V024,
    REF_MAX9286_96705_AR0231RCCB,
    REF_MAX9286_96705_AR0231,
    REF_MAX9288_96705_OV10635,
    M_MAX9288_96705_AR0140,
    TPG,
    MAX_IMG_DEV_SUPPORTED,
} ImgDevicesList;

ImgDevDriver *ImgGetDevice(char *moduleName);

#endif /* _DEV_LIST_H_ */
