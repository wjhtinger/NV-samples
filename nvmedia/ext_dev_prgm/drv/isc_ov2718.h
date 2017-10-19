/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_OV2718_H_
#define _ISC_OV2718_H_

#include "nvmedia_isc.h"

typedef enum {
    ISC_CONFIG_EXAMPLE         = 0,
} ConfigSetsExample;

NvMediaISCDeviceDriver *GetOV2718Driver(void);

#endif /* _ISC_EXAMPLE_H_ */
