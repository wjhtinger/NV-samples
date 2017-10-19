/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <string.h>
#include "log_utils.h"
#include "dev_list.h"
#include "ref_max9286_9271_ov10635.h"
#include "ref_max9286_9271_ov10640.h"
#include "c_max9286_9271_ov10640.h"
#include "d_max9286_9271_mt9v024.h"
#include "ref_max9286_96705_ar0231.h"
#include "ref_max9288_96705_ov10635.h"
#include "m_max9288_96705_ar0140.h"
#include "ref_max9286_96705_ar0231_rccb.h"
#include "c_max9286_9271_ov10640lsoff.h"
#include "tpg.h"

ImgDevDriver *ImgGetDevice(char *moduleName)
{
    NvS32 i;
    ImgDevDriver *devices[MAX_IMG_DEV_SUPPORTED] = {
        GetDriver_ref_max9286_9271_ov10635(),
        GetDriver_ref_max9286_9271_ov10640(),
        GetDriver_c_max9286_9271_ov10640lsoff(),
        GetDriver_c_max9286_9271_ov10640(),
        GetDriver_d_max9286_9271_mt9v024(),
        GetDriver_ref_max9286_96705_ar0231rccb(),
        GetDriver_ref_max9286_96705_ar0231(),
        GetDriver_ref_max9288_96705_ov10635(),
        GetDriver_m_max9288_96705_ar0140(),
        GetDriver_tpg()
    };

    if(!moduleName) {
        LOG_ERR("%s: Bad parameter for module name%s\n", __func__);
        return NULL;
    }

    for(i = 0; i < MAX_IMG_DEV_SUPPORTED; i++) {
        /* module name can have device name and version number,
         * so module name can be longer than device name.
         * therefore, comparing string needs length of the device name */
        if(!strncmp(moduleName, devices[i]->name, strlen(devices[i]->name))) {
            LOG_DBG("%s: Found the driver for %s\n", __func__, moduleName);
            return devices[i];
        }
    }

    LOG_ERR("%s: Can't find driver for %s\n", __func__, moduleName);

    return NULL;
};
