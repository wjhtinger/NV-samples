/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_OV10635_H_
#define _ISC_OV10635_H_

#include "nvmedia_isc.h"

typedef enum {
    ISC_CONFIG_OV10635_DEFAULT  = 0,
    ISC_CONFIG_OV10635_SYNC,
    ISC_CONFIG_OV10635_ENABLE_STREAMING,
} ConfigSetsOV10635;

NvMediaISCDeviceDriver *GetOV10635Driver(void);

#endif /* _ISC_OV10635_H_ */
