/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _SENSOR_INFO_AR0231_H_
#define _SENSOR_INFO_ARO231_H_

#include "sensor_info.h"

#define AR0231_1928X1208_PCLK      88000000 // MHz

#define AR0231_REG_DATA_BYTES             2

#define AR0231_REG_USER_KNEE_LUT0         0x33C0 // 12 user keen points start add
#define AR0231_REG_KNEE_LUT_CTL           0x33DA
#define AR0231_REG_OP_MODE_CTRL           0x3082
#define AR0231_REG_DIGITAL_CTRL           0x30BA
#define AR0231_REG_LINE_LENGTH_PCK        0x300C // HTS
#define AR0231_REG_FRAME_LENGTH_LINE      0x300A // VTS

#define AR0231_REG_COARSE_INTEG_TIME_T1   0x3012 // Exposure time T1, row
#define AR0231_REG_COARSE_INTEG_TIME_T2   0x3212 // Exposure time T2, row
#define AR0231_REG_FINE_INTEG_TIME_T1     0x3014 // Fine exposure time T1, pclk
#define AR0231_REG_FINE_INTEG_TIME_T2     0x321E // Fine exposure time T2, pclk
#define AR0231_REG_FINE_INTEG_TIME_T3     0x3222 // Fine exposure time T3, pclk
#define AR0231_REG_FINE_INTEG_TIME_T4     0x3226 // Fine exposure time T4, pclk
#define AR0231_REG_CGAIN                  0x3362 // Conversion gain
#define AR0231_REG_AGAIN                  0x3366 // Analog gain
#define AR0231_REG_DGAIN                  0x3308 // Digital gain
#define AR0231_REG_DGAIN_GR               0x3056 // AWB gain Gr
#define AR0231_REG_DLO_CTL                0x318E // DLO control
#define AR0231_REG_DATA_FORMAT            0x31AC // Data format

#define AR0231_REG_EXP_TIME_T1_ROW        0x2020 // Readback exposure time T1, row
#define AR0231_REG_FINE_EXP_TIME_T1_PCLK  0x2038 // Readback fine exposure time T1, pclk

#define AR0231_ONE_DGAIN_VAL              0x200  // = 1.0; format2.9 XX.YYYYYYYYY
#define AR0231_ONE_COLOR_DGAIN_VAL        0x80   // = 1.0; format4.7 XXXX.YYYYYYY

// Exposure time limitations from Vendor
#define MAX_COARSE_INTEG_TIME_T2   62     // Maximum rows can be actually set to T2
#define MAX_COARSE_INTEG_TIME_T3   5      // Maximum rows can be actually set to T3
#define MAX_COARSE_INTEG_TIME_T4   1      // Maximum rows can be actually set to T4

#define AR0231_REG_EMBEDDED_LINES_ENABLE     0x3064
#define AR0231_REG_EMBEDDED_TOP_DARK_ROWS    0x3180
#define AR0231_REG_EMBEDDED_BOT_DARK_ROWS    0x33e0
#define AR0231_REG_EMBEDDED_TEST_ROWS        0x33e4
#define AR0231_MIN_NUM_EMB_LINES             8

#define AR0231_SENSOR_FUSE_ID_SIZE   16

typedef struct {
    NvU16 hts;
    NvU16 vts;
    NvU16 aGain;
    NvU16 cGain;
    NvU16 opCtl;
    NvU16 dCtl;
    NvU16 fineIntTime[4];
} SensorDefaultSet;

SensorInfo *GetSensorInfo_ar0231(void);

#endif /* _SENSOR_INFO_AR0231_H_ */
