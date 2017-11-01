/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#ifndef _IMG_DEV_H_
#define _IMG_DEV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "nvcommon.h"
#include "nvmedia.h"
#include "nvmedia_image.h"
#include "nvmedia_icp.h"
#include "nvmedia_isc.h"
#include "dev_error.h"

/* Major Version number */
#define EXTIMGDEV_VERSION_MAJOR   1
/* Minor Version number */
#define EXTIMGDEV_VERSION_MINOR   8

#define MAX_AGGREGATE_IMAGES 4

#define CAM_ENABLE_DEFAULT 0x0001  // only enable cam link 0
#define CAM_MASK_DEFAULT   0x0000  // do not mask any link
#define CSI_OUT_DEFAULT    0x3210  // cam link i -> csiout i

/* Count enabled camera links */
#define EXTIMGDEV_MAP_COUNT_ENABLED_LINKS(enable) \
             ((enable & 0x1) + ((enable >> 4) & 0x1) + \
             ((enable >> 8) & 0x1) + ((enable >> 12) & 0x1))
/* Convert aggegate number to cam_enable */
#define EXTIMGDEV_MAP_N_TO_ENABLE(n) \
             ((((1 << n) - 1) & 0x0001) + ((((1 << n) - 1) << 3) &0x0010) + \
              ((((1 << n) - 1) << 6) & 0x0100) + ((((1 << n) - 1) << 9) & 0x1000))

/* Macro to set the ExtImgDevVersion number */
#define EXTIMGDEV_SET_VERSION(x, _major, _minor) \
    x.major = _major; \
    x.minor = _minor;

typedef void ExtImgDevDriver;

typedef struct {
    /* Major version */
    unsigned char major;
    /* Minor version */
    unsigned char minor;
} ExtImgDevVersion;

typedef struct {
    unsigned int                enable; // camera[3..0] enable, value:0/1. eg 0x1111
    unsigned int                mask;   // camera[3..0] mask,   value:0/1. eg 0x0001
    unsigned int                csiOut; // camera[3..0] csi outmap, value:0/1/2/3. eg. 0x3210
} ExtImgDevMapInfo;

typedef struct {
    char                       *moduleName;
    char                       *resolution;
    char                       *inputFormat;
    char                       *interface;
    NvU32                       i2cDevice;                                            //对应配置文件中i2c_device项，对应一个i2c控制器
    NvU32                       desAddr;
    NvU32                       brdcstSerAddr;
    NvU32                       serAddr[MAX_AGGREGATE_IMAGES];
    NvU32                       brdcstSensorAddr;
    NvU32                       sensorAddr[MAX_AGGREGATE_IMAGES];                     //器件的I2C地址（0x30: OV10635 OV10640，0x10: AR0231），同一个i2c_device下器件地址依次加1（也是为啥同一组camera的器件要一致的原因）。
    NvU32                       sensorsNum;
    NvMediaBool                 enableEmbLines; /* TBD : this flag will be optional for
                                                 * on chip ISP in the sensor
                                                 * such as OV10635,
                                                 * if not, this flag is mandatory */
    char                       *board; /* Optional */
    NvMediaBool                 initialized; /* Optional:
                                              * Already initialized doesn't need to */
    NvMediaBool                 slave;  /* Optional :
                                         * Doesn't need to control sensor/serializer
                                         * through aggregator */
    NvMediaBool                 enableSimulator; /* Optional
                                                  * This flag is not to open actual
                                                  * isc-dev, it is for running isc
                                                  * without actual device. */
    ExtImgDevMapInfo           *camMap;
    NvMediaBool                 enableVirtualChannels;
    NvU32                       reqFrameRate; /* Optional
                                               * default frame rate will be 30fps. */
    NvMediaBool                 selfTestFlag; /* Optional
                                               * sensor self safety test flag
                                               * currently supported only by OV10640 */
    NvMediaBool                 enableExtSync; /* Enable external synchronization */
    float                       dutyRatio; /* FRSYNC duty ratio */
} ExtImgDevParam; //外部设备描述信息，比如串行转换设备，sensor等，信息来自CaptureConfigParams

typedef struct {
    unsigned short              width;
    unsigned short              height;
    unsigned int                embLinesTop;
    unsigned int                embLinesBottom;
    NvMediaICPInputFormatType   inputFormatType;
    NvMediaBitsPerPixel         bitsPerPixel;
    NvMediaRawPixelOrder        pixelOrder;
    NvMediaICPInterfaceType     interface;
    NvMediaBool                 doubledPixel; /* for raw11x2 or raw16 */
    unsigned int                vcId[NVMEDIA_ICP_MAX_VIRTUAL_CHANNELS];
    NvU32                       frameRate;
    unsigned int                pixelFrequency; /* either calculated by (VTS * HTS * frame rate) * sensorsNum,
                                                   or actual PCLK * sensorsNum, unit is Hz */
    NvMediaBool                 enableExtSync; /* Enable external synchronization */
    float                       dutyRatio; /* FRSYNC duty ratio */
} ExtImgDevProperty;

typedef struct {
    ExtImgDevDriver            *driver;
    ExtImgDevProperty           property;
    // ISC
    NvMediaISCRootDevice       *iscRoot;
    NvMediaISCDevice           *iscDeserializer;
    NvMediaISCDevice           *iscSerializer[MAX_AGGREGATE_IMAGES];
    NvMediaISCDevice           *iscSensor[MAX_AGGREGATE_IMAGES];
    NvMediaISCDevice           *iscSensor2[MAX_AGGREGATE_IMAGES];
    NvMediaISCDevice           *iscBroadcastSerializer;
    NvMediaISCDevice           *iscBroadcastSensor;
    NvMediaISCDevice           *iscBroadcastSensor2;
    NvU32                       sensorsNum;
    NvU32                       remapIdx[MAX_AGGREGATE_IMAGES];
    NvMediaBool                 simulator;
} ExtImgDevice;

ExtImgDevice *
ExtImgDevInit(ExtImgDevParam *configParam);

void
ExtImgDevDeinit(ExtImgDevice *device);

NvMediaStatus
ExtImgDevStart(ExtImgDevice *device);

void
ExtImgDevStop(ExtImgDevice *device);

NvMediaStatus
ExtImgDevGetError(
    ExtImgDevice *device,
    NvU32 *link,
    ExtImgDevFailureType *errorType);

NvMediaStatus
ExtImgDevRegisterCallback(
    ExtImgDevice *device,
    NvU32 sigNum,
    void (*cb)(void *),
    void *context);

/*
 * Possible values are:
 *      NVMEDIA_STATUS_OK
 *      NVMEDIA_STATUS_BAD_PARAMETER if the pointer is invalid.
 *      NVMEDIA_STATUS_INCOMPATIBLE_VERSION if the client version does
 *                                          not match with the core version
 */
NvMediaStatus
ExtImgDevCheckVersion(
    ExtImgDevVersion *version
);

/**
 * Provides change history for the ExtImgDev API.
 *
 * <b> Version 1.0 </b> April 11, 2016
 * - Initial release
 *
 * <b> Version 1.1 </b> June 16, 2016
 * - Added \ref ExtImgDevCheckVersion API
 *
 * <b> Version 1.2 </b> June 21, 2016
 * - Added \ref enableVirtualChannels and vcId variables
 *
 * <b> Version 1.3 </b> June 28, 2016
 * - Added \ref ExtImgDevCheckVersion API
 *
 * <b> Version 1.4 </b> Sep 16, 2016
 * - Added reqFrameRate in \ref ExtImgDevParam
 * - Added frameRate in \ref ExtImgDevProperty
 *
 * <b> Version 1.5 </b> Oct 11, 2016
 * - Added pixelFrequency in \ref ExtImgDevProperty
 *
 * <b> Version 1.6 </b> Oct 25, 2016
 * - Changed \ref ExtImgDevStart function return type from void to NvMediaStatus
 * - Added remapIdx & simulator variables in \ref ExtImgDevice
 *
 * <b> Version 1.7 </b> Feb 24, 2017
 * - Added selfTestFlag in \ref ExtImgDevParam
 *
 * <b> Version 1.8 </b> March 19, 2017
 * - Added enableExtSync in \ref ExtImgDevParam and ExtImgDevProperty
 * - Added dutyRatio in \ref ExtImgDevParam and ExtImgDevProperty
 *
 */

#ifdef __cplusplus
};      /* extern "C" */
#endif

#endif /* _IMG_DEV_H_ */
