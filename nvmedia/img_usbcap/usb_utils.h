/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __USB_UTILS_H__
#define __USB_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"

#include "nvmedia_image.h"
#include "linux/videodev2.h"

#define USB_MMAP_MAX_BUFFERS    6

#define USB_OPEN_MAX_DEVICES    10
#define USB_OPEN_MAX_TIMEOUT   (10 * 1000) /* ms */
#define USB_OPEN_MAX_RETRIES    10

#define USB_INVALID_FD          -1

typedef struct _UsbMmapBuffer
{
    void     *start;
    size_t   length;
} V4l2UsbMmapBuffer;

typedef struct _UtilUSBSensorConfig
{
    unsigned int          height;
    unsigned int          width;
    unsigned int          fmt;
    char*                 devPath;
} UtilUsbSensorConfig;

typedef struct _UtilUSBSensor
{
    UtilUsbSensorConfig   *config;
    int                   fdUsbCameraDevice; /* USB file descriptor */
    V4l2UsbMmapBuffer     mmapBufs[USB_MMAP_MAX_BUFFERS];
    unsigned int          numBufs;
    bool                  isMmapBufsInit;
} UtilUsbSensor;

void UtilUsbSensorFindCameras(void);
int UtilUsbSensorGetFirstAvailableCamera(char *camName);
UtilUsbSensor* UtilUsbSensorInit(UtilUsbSensorConfig *);
int UtilUsbSensorDeinit(UtilUsbSensor *);
int UtilUsbSensorStartCapture(UtilUsbSensor *);
int UtilUsbSensorStopCapture(UtilUsbSensor *);
int UtilUsbSensorGetFrame(UtilUsbSensor *,NvMediaImage *,unsigned int);

#endif /* USB_UTILS_H_ */
