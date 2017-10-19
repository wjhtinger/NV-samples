/* Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#ifndef _DEV_MAP_H_
#define _DEV_MAP_H_

#include "img_dev.h"

// link i is enabled or not
#define EXTIMGDEV_MAP_LINK_ENABLED(enable, i) ((enable >> 4*i) & 0x1)
// link i out: 0 or 1 or 2 or 3
#define EXTIMGDEV_MAP_LINK_CSIOUT(csiOut, i) ((csiOut >> 4*i) & 0x3)

NvMediaStatus
ExtImgDevCheckValidMap (
    ExtImgDevMapInfo *camMap);

NvMediaBool
ExtImgDevCheckReMap(
    ExtImgDevMapInfo *camMap);

#endif /* _DEV_MAP_H_ */
