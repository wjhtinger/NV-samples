/*
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#ifndef _ISC_MAX9286_H_
#define _ISC_MAX9286_H_

#include "nvmedia_isc.h"

#define MAX9286_REG_LINK_EN                0
#define MAX9286_REG_CONTROL_CHANNEL        10
#define MAX9286_REG_CONFIG_LINK_DETECT     73
#define MAX9286_REG_SET_SYNC_MARGIN        3
#define MAX9286_REG_DATA_OK                72
#define MAX9286_REG_VIDEO_LINK             73
#define MAX9286_REG_VSYNC_DET              39
#define MAX9286_REG_LINE_BUF_ERR           34
#define MAX9286_REG_FSYNC_LOCK             49
#define MAX9286_REG_LINK_OUT_ORDER         11
#define MAX9286_REG_MASK_LINK              105
#define MAX9286_REG_FSYNC_PERIOD           6
#define MAX9286_REG_CHIP_REVISION          31

#define MAX9286_CHIP_REVISION_8            0x8 // For the new aggregator
#define MAX9286_CHIP_REVISION_MASK         0xF

#define MAX9286_FSYNC_LOSS_OF_LOCK (1 << 7)
#define MAX9286_GMSL_LINK(link)    (1 << link)
#define MAX9286_REVERSE_CHANNEL_TRANSMITTER (1 << 4)

typedef enum {
    ISC_CONFIG_MAX9286_DEFAULT           = 0,
    ISC_CONFIG_MAX9286_DISABLE_AUTO_ACK,
    ISC_CONFIG_MAX9286_ENABLE_AUTO_ACK,
    ISC_CONFIG_MAX9286_ENABLE_CSI_OUT,
    ISC_CONFIG_MAX9286_ENABLE_LINK_0,
    ISC_CONFIG_MAX9286_ENABLE_LINKS_01,
    ISC_CONFIG_MAX9286_ENABLE_LINKS_012,
    ISC_CONFIG_MAX9286_ENABLE_LINKS_0123,
    ISC_CONFIG_MAX9286_REVERSE_CHANNEL_SETTING,
    ISC_CONFIG_MAX9286_POWER_UP_REVERSE_CHANNEL_TRANSMITTER,
    ISC_CONFIG_MAX9286_ENABLE_REVERSE_CHANNEL_0123,
    ISC_CONFIG_MAX9286_DISABLE_REVERSE_CHANNEL_0123,
    ISC_CONFIG_MAX9286_DISABLE_ALL_CONTROL_CHANNEL,
    ISC_CONFIG_MAX9286_REVERSE_CHANNEL_RESET,
    ISC_CONFIG_MAX9286_SWAP_DATA_LANES,
    ISC_CONFIG_MAX9286_CAMERA_MAPPING,
    ISC_CONFIG_MAX9286_SWAP_12BIT_ORDER,
    ISC_CONFIG_MAX9286_REVERSE_CHANNEL_ENABLE,
    ISC_CONFIG_MAX9286_REVERSE_CHANNEL_AMPL_L,
    ISC_CONFIG_MAX9286_REVERSE_CHANNEL_AMPL_H,
    ISC_CONFIG_MAX9286_DISABLE_CSI_OUT,
    ISC_CONFIG_MAX9286_DISABLE_OVLP_WINDOW,
    ISC_CONFIG_MAX9286_ENABLE_HIBW,
    ISC_CONFIG_MAX9286_ENABLE_VC,
} ConfigSetsMAX9286;

typedef enum {
    ISC_WRITE_PARAM_CMD_MAX9286_ENABLE_REVERSE_CHANNEL  = 0,
    ISC_WRITE_PARAM_CMD_MAX9286_DISABLE_REVERSE_CHANNEL,
    ISC_WRITE_PARAM_CMD_MAX9286_SET_PIXEL_INFO,
    ISC_WRITE_PARAM_CMD_MAX9286_SET_SYNC_MODE,
} WriteParametersCmdMAX9286;

typedef enum {
    ISC_DATA_TYPE_MAX9286_RGB888 = 0,
    ISC_DATA_TYPE_MAX9286_RGB565,
    ISC_DATA_TYPE_MAX9286_RGB666,
    ISC_DATA_TYPE_MAX9286_YUV422_8BIT,
    ISC_DATA_TYPE_MAX9286_YUV422_10BIT,
    ISC_DATA_TYPE_MAX9286_RAW8_RAW16,
    ISC_DATA_TYPE_MAX9286_RAW10_RAW20,
    ISC_DATA_TYPE_MAX9286_RAW11_RAW12,
    ISC_DATA_TYPE_MAX9286_RAW14,
    ISC_DATA_TYPE_MAX9286_USER_DEFINED_24BIT,
    ISC_DATA_TYPE_MAX9286_USER_DEFINED_YUV422,
    ISC_DATA_TYPE_MAX9286_USER_DEFINED_8BIT,
} DataTypeMAX9286;

typedef enum {
    ISC_WORD_MODE_MAX9286_SINGLE = 0,
    ISC_WORD_MODE_MAX9286_DOUBLE,
} WordModeMAX9286;

typedef enum {
    ISC_CSI_MODE_MAX9286_SINGLE = 0,
    ISC_CSI_MODE_MAX9286_DOUBLE,
} CSIModeMAX9286;

typedef enum {
    ISC_SET_FSYNC_MAX9286_INTERNAL_PIN_HIGH_Z = 0,
    ISC_SET_FSYNC_MAX9286_INTERNAL_PIN_OUT,
    ISC_SET_FSYNC_MAX9286_EXTERNAL_FROM_MAX9286,
    ISC_SET_FSYNC_MAX9286_EXTERNAL_FROM_ECU,
    ISC_SET_FSYNC_MAX9286_DISABLE_SYNC,
    ISC_SET_FSYNC_MAX9286_FSYNC_MANUAL,
    ISC_SET_FSYNC_MAX9286_FSYNC_AUTO,
    ISC_SET_FSYNC_MAX9286_FSYNC_SEMI_AUTO,
} SetFsyncModeMAX9286;

typedef enum {
    ISC_MAX9286_REG_DATA_OK = 0,
    ISC_MAX9286_REG_VIDEO_LINK,
    ISC_MAX9286_REG_VSYNC_DET,
    ISC_MAX9286_REG_LINE_BUF_ERR,
    ISC_MAX9286_REG_FSYNC_LOCK,
    ISC_MAX9286_NUM_ERR_REG,
} ErrorRegisterMAX9286;

typedef enum {
    ISC_MAX9286_NO_ERROR = 0,
    ISC_MAX9286_NO_DATA_ACTIVITY,
    ISC_MAX9286_VIDEO_LINK_ERROR,
    ISC_MAX9286_VSYNC_DETECT_FAILURE,
    ISC_MAX9286_LINE_LENGTH_ERROR,
    ISC_MAX9286_LINE_BUFFER_OVERFLOW,
    ISC_MAX9286_FSYNC_LOSS_OF_LOCK,
    ISC_MAX9286_NUM_FAILURE_TYPES
} FailureTypeMAX9286;

typedef union {
    struct {
        unsigned type : 4;
        unsigned dbl : 1;
        unsigned csi_dbl : 1;
        unsigned lane_cnt : 2;
    } bits;
    unsigned char byte;
} ConfigurePixelInfoMAX9286;

typedef struct {
    union {
        struct {
            unsigned int id;
        } EnableReverseChannel;
        struct {
            unsigned int id;
        } DisableReverseChannel;
        struct {
            SetFsyncModeMAX9286 syncMode;
            int k_val; /* frame sync margin : us OR pclk value of FSYNC*/
        } SetFsyncMode;
        struct {
            ConfigurePixelInfoMAX9286 *pixelInfo;
        } SetPixelInfo;
    };
} WriteParametersParamMAX9286;

typedef struct {
    FailureTypeMAX9286          failureType;
    unsigned char               link;
} ErrorStatusMAX9286;

#define MAX9286_MIN_REVERSE_CHANNEL_AMP 30
#define MAX9286_MAX_REVERSE_CHANNEL_AMP 200
#define MAX9286_REVERSE_CHANNEL_BOOT_AMP 100
#define MAX9286_REVERSE_CHANNEL_STEP_SIZE 10
#define MAX9286_REVERSE_CHANNEL_AMP_REG_SHIFT 1
#define MAX9286_REVERSE_CHANNEL_AMP_BOOST_REG_SHIFT 0

typedef struct {
    unsigned int     enable; // camera[3..0] enable, value:0/1. eg 0x1111
    unsigned int     mask;   // camera[3..0] mask,   value:0/1. eg 0x0001
    unsigned int     csiOut; // camera[3..0] csi outmap, value:0/1/2/3. eg. 0x3210
} CamMap;

typedef struct {
    unsigned int reverseChannelAmp; /* 30 to 160, step size is 10, unit is mV */
    unsigned int reverseChannelTrf; /* transimitter transition time: 100 to 400, step size is 100, unit is ns */
    unsigned char gmslLinks;        /* current status of gmsl link*/
    CamMap camMap;
    NvMediaBool enSwapBitOrder;
    unsigned char revision;         /* chip revision information */
    NvMediaBool disableBackChannelCtrl; /* flag to indicate MAX9286 won't interact with the serializer and the sensor */
} ContextMAX9286;

NvMediaISCDeviceDriver *GetMAX9286Driver(void);

#endif /* _ISC_MAX9286_H_ */
