/*
 * Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <string.h>

#include "log_utils.h"
#include "nvmedia_icp.h"
#include "dev_map.h"

// check if 4 links has any duplicated out number
#define EXTIMGDEV_MAP_CHECK_DUPLICATED_OUT(csiOut) \
           (((EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 0) == EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 1)) || \
             (EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 0) == EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 2)) || \
             (EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 0) == EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 3)) || \
             (EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 1) == EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 2)) || \
             (EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 1) == EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 3)) || \
             (EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 2) == EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, 3))) ? 1 : 0)

NvMediaStatus
ExtImgDevCheckValidMap(
    ExtImgDevMapInfo *camMap)
{
    unsigned int i, j, n;

    // cam_enable correct?
    if (camMap->enable != (camMap->enable & 0x1111)) {
       LOG_ERR("%s: WRONG command! cam_enable for each cam can only be 0 or 1! 0001 to 1111\n", __func__);
       return NVMEDIA_STATUS_ERROR;
    }

    // cam_mask correct?
    if (camMap->mask != (camMap->mask & 0x1111)) {
       LOG_ERR("%s: WRONG command! cam_mask for each cam can only be 0 or 1! 0000 to 1110\n", __func__);
       return NVMEDIA_STATUS_ERROR;
    }

    // cam_enable correct?
    if (camMap->csiOut != (camMap->csiOut & 0x3333)) {
       LOG_ERR("%s: WRONG command! csi_outmap for each cam can only be 0, 1, 2 or 3! For example: 3210\n", __func__);
       return NVMEDIA_STATUS_ERROR;
    }

    // To same csi out?
    if (EXTIMGDEV_MAP_CHECK_DUPLICATED_OUT(camMap->csiOut)) {
       LOG_ERR("%s: WRONG command! csi_outmap has same out number?\n", __func__);
       return NVMEDIA_STATUS_ERROR;
    }

    // If mask all enabled links?
    if (camMap->mask == camMap->enable) {
       LOG_ERR("%s: WRONG command! can not mask all enabled link(s)!\n", __func__);
       return NVMEDIA_STATUS_ERROR;
    }

    // Check enabled links csi_out, should start from 0, then 1, 2, 3
    n = EXTIMGDEV_MAP_COUNT_ENABLED_LINKS(camMap->enable);
    for(j = 0; j < n; j++) {
        for(i = 0; i < 4; i++) {
           if (EXTIMGDEV_MAP_LINK_ENABLED(camMap->enable, i)) {
               if ((EXTIMGDEV_MAP_LINK_CSIOUT(camMap->csiOut, i)) == j) {
                   break;
               }
           }
        }
        if (i == 4) {
           LOG_ERR("%s: WRONG command! csi_outmap didn't match cam_enable or aggregate!\n", __func__);
           return NVMEDIA_STATUS_ERROR;
        }
    }

   return NVMEDIA_STATUS_OK;
}

NvMediaBool
ExtImgDevCheckReMap(
    ExtImgDevMapInfo *camMap)
{
    if ((camMap != NULL) &&
       ((camMap->mask != CAM_MASK_DEFAULT) ||
        (camMap->csiOut != CSI_OUT_DEFAULT) ||
       ((camMap->enable != 0x0001) && (camMap->enable != 0x0011) &&
        (camMap->enable != 0x0111) && (camMap->enable != 0x1111))))
        return NVMEDIA_TRUE;

    return NVMEDIA_FALSE;
}
