/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_XC7027_H_
#define _ISC_XC7027_H_

#include "nvmedia_isc.h"
#include "log_utils.h"

typedef enum {
    ISC_CONFIG_XC7027         = 0,
	ISC_CONFIG_XC7027_SYNC,
	ISC_CONFIG_XC7027_ENABLE_STREAMING
} ConfigSetsXC7027;



NvMediaISCDeviceDriver *GetXC7027Driver(void);

#endif /* _ISC_EXAMPLE_H_ */
