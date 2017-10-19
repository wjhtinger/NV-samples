/* Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __ANDROIDA4A_H__
#define __ANDROIDA4A_H__

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <dirent.h>

#include "usb.h"
#include "result.h"

#define USB_ACCESSORY_VENDOR_ID                 0x18D1
#define USB_ACCESSORY_PRODUCT_ID                0x2D00
#define USB_ACCESSORY_ADB_PRODUCT_ID            0x2D01
#define USB_AUDIO_PRODUCT_ID                    0x2D02
#define USB_AUDIO_ADB_PRODUCT_ID                0x2D03
#define USB_ACCESSORY_AUDIO_PRODUCT_ID          0x2D04
#define USB_ACCESSORY_AUDIO_ADB_PRODUCT_ID      0x2D05

#define ACCESSORY_STRING_MANUFACTURER   0
#define ACCESSORY_STRING_MODEL          1
#define ACCESSORY_STRING_DESCRIPTION    2
#define ACCESSORY_STRING_VERSION        3
#define ACCESSORY_STRING_URI            4
#define ACCESSORY_STRING_SERIAL         5

#define ACCESSORY_GET_PROTOCOL          51
#define ACCESSORY_SEND_STRING           52
#define ACCESSORY_START                 53
#define AUDIO_START                     58

#define DEFAULT_TIMEOUT 1000

#ifdef __cplusplus
extern "C" {
#endif

/* Function prototypes */
GstNvmResult androida4a (void);
int androidhid (usb_dev_handle *device_handle);

#ifdef __cplusplus
}
#endif

#endif

