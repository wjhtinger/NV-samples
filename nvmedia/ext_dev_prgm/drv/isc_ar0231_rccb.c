/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "log_utils.h"
#include "nvmedia_isc.h"
#include "isc_ar0231_rccb.h"
#include "isc_ar0231_rccb_setting.h"

#define NUM_COMPANDING_KNEE_POINTS  12

#define REG_ADDRESS_BYTES     2
#define REG_DATA_BYTES        2
#define REG_WRITE_BUFFER      256

#define AR0231_CHIP_ID        0x0354

#define REG_CHIP_ID           0x3000
#define REG_CHIP_VER          0x31FE
#define REG_GROUP_HOLD        0x3022
#define REG_INTEG_TIME_T1     0x3012
#define REG_INTEG_TIME_T2     0x3212
#define REG_INTEG_TIME_T3     0x3216
#define REG_CGAIN             0x3362
#define REG_AGAIN             0x3366
#define REG_DGAIN             0x3308
#define REG_DGAIN_GR          0x3056
#define REG_STAT_FRAME_ID     0x31D2
#define REG_USER_KNEE_LUT0    0x33C0
#define REG_KNEE_LUT_CTL      0x33DA
#define REG_TEMPSENS0_DATA    0x20B0
#define REG_TEMPSENS1_DATA    0x20B2
#define REG_TEMPSENS0_CTL     0x30B4
#define REG_TEMPSENS1_CTL     0x30B8
#define REG_TEMPSENS0_CALI    0x30C6
#define REG_DLO_CTL           0x318E
#define REG_DATA_FORMAT       0x31AC
#define REG_EMB_CTL           0x3064
#define REG_CTX_CTL           0x3034
#define REG_CTX_WR_DATA       0x3066
#define REG_DIGITAL_TEST      0x30B0
#define REG_INTEG_TIME_T1_CB  0x3016
#define REG_INTEG_TIME_T2_CB  0x3214
#define REG_AGAIN_CB          0x3368
#define REG_DGAIN_CB          0x3312
#define REG_FIT1              0x3014
#define REG_FIT1_CB           0x3018
#define REG_FIT2              0x321E
#define REG_FUSE_ID           0x34C0

// top emb data offset
#define EMB_OFFSET_FRAME_CNT  3
#define EMB_OFFSET_EXP_T1     31
#define EMB_OFFSET_CHIP_VER   704 // offset of register 0x31ff in top emb, v4
                                  // v4: 139; v6: +4; v7: +10
#define EMB_OFFSET_TEMPSENS0(ver) (139 + ((ver <= 4) ? 0 : ((ver == 7) ? 10 : 4)))
#define EMB_OFFSET_DGAIN_GR(ver)  (433 + ((ver <= 4) ? 0 : ((ver == 7) ? 12 : 4)))
#define EMB_OFFSET_FRAME_ID(ver)  (671 + ((ver <= 4) ? 0 : ((ver == 7) ? 20 : 4)))
#define EMB_OFFSET_DGAIN(ver)     (AR0231_ACTIVE_H / 2 + ((ver == 7) ?  19 : 75))
#define EMB_OFFSET_CGAIN(ver)     (AR0231_ACTIVE_H / 2 + ((ver == 7) ? 110 : 166))
#define EMB_OFFSET_AGAIN(ver)     (AR0231_ACTIVE_H / 2 + ((ver == 7) ? 113 : 169))
// 4 bottom emb lines, stats on lines 2 and 3
#define EMB_OFFSET_BOTTOM_ROW1 (AR0231_ACTIVE_H * 2 * 2)
#define EMB_OFFSET_BOTTOM_ROW2 (AR0231_ACTIVE_H * 3 * 2)
// bottom emb stats offset
#define R1_OFFSET_ROI1_PIXELS 8
#define R1_OFFSET_DUMMY       990
#define R2_OFFSET_ROI3_PIXELS 4
#define R2_OFFSET_DBLC        526
#define R2_OFFSET_ROI1_STATS  1038
#define R2_OFFSET_STATS_REG   1074

#define EMB_EXTRA_OFFSET_V6   4
#define EMB_EXTRA_OFFSET_V7   16

#define HIGH_CGAIN            3.0     // cgain bit 0->1.0, 1->3.0
#define MIN_AGAIN_LCG         0.8     // sensor vendor recommanded @ lcg
#define MIN_AGAIN_HCG         1.0     // sensor vendor recommanded @ hcg
#define MIN_DGAIN             1.0
#define MAX_DGAIN_VAL         0x7FF   // =3 + 511 / 512
#define ONE_DGAIN_VAL         0x200   // =1.0; format2.9 XX.YYYYYYYYY
#define ONE_COLOR_DGAIN_VAL   0x80    // =1.0; format4.7 XXXX.YYYYYYY
// exposure time limitation from sensor vendor
#define MAX_AE_EXP_LINES_T2   62
#define MAX_AE_EXP_LINES_T3   5
#define MAX_GAIN              (HIGH_CGAIN * 8.0 * (3 + 511 / 512.0))

#define EXPOSURE_PRINT(args...)
//#define EXPOSURE_PRINT(args...) printf(args)

static const float aGainTbl[] = {
    0.12500, // 0 - 1/8x
    0.25000, // 1 - 2/8x
    0.28571, // 2 - 2/7x
    0.42857, // 3 - 3/7x
    0.50000, // 4 - 3/6x
    0.66667, // 5 - 4/6x
    0.80000, // 6 - 4/5x - min aGain in lcg
    1.00000, // 7 - 5/5x - min aGain in hcg
    1.25000, // 8 - 5/4x
    1.50000, // 9 - 6/4x
    2.00000, //10 - 6/3x
    2.33333, //11 - 7/3x
    3.50000, //12 - 7/2x
    4.00000, //13 - 8/2x
    8.00000  //14 - 8/1x
};

#define NUM_A_GAIN (sizeof(aGainTbl) / sizeof(aGainTbl[0]))

#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   (&x[1])
#define SET_NEXT_BLOCK(x)   (x += (x[0] + 1))

static int colorIndex[4] = {
    1, // Gr - index 1; index order: R, Gr, Gb, B
    3, // B  - index 3
    0, // R  - index 0
    2  // Gb - index 2
};

typedef enum {
    CONTEXT_0 = 0,
    CONTEXT_1,
    CONTEXT_2,
    CONTEXT_3,
    CONTEXT_A,
    CONTEXT_B,
} ContextIndex;

 typedef struct { // sensor 0 & 1
    float slope[2]; // slope
    float t0[2];    // -T0
    unsigned short int tempratureData[2];
} TempSensCali;

typedef struct {
    NvMediaISCSupportFunctions *funcs;
    const unsigned char *default_setting;
    const TimingSettingAR0231 *timingSettings;
    ConfigInfoAR0231 config_info;
    unsigned int oscMhz;
    float maxGain;
    NvMediaISCModuleConfig moduleCfg;
    NvMediaBool grgbOneFlag;     // set flag to TRUE if GR and Gb gains = 1.0
    unsigned char loadedContext; // 0~3:ram based context, 4:context A, 5:context B
    unsigned char cGainCa;
    unsigned int exposureModeMask;
    unsigned char fuseId[SIZE_FUSE_ID];
    unsigned int configSetIdx;   // 0 - internal sync, 1 - external sync
    TempSensCali tempSensCali;
} _DriverHandle;

static NvMediaStatus
WriteRegister(NvMediaISCDriverHandle *handle, NvMediaISCTransactionHandle *transaction,
        unsigned int registerNum, unsigned int dataLength, unsigned char *dataBuff);

static NvMediaStatus
ReadRegister(NvMediaISCDriverHandle *handle, NvMediaISCTransactionHandle *transaction,
        unsigned int registerNum, unsigned int dataLength, unsigned char *dataBuff);

static NvMediaStatus
DriverCreate(
    NvMediaISCDriverHandle **handle,
    NvMediaISCSupportFunctions *supportFunctions,
    void *clientContext)
{
    _DriverHandle *driverHandle;
    ContextAR0231 *ctx = NULL;
    unsigned int lenCfgName;
    ConfigFormatAR0231 fmtSel = ISC_CONFIG_AR0231_1920X1208_30FPS;

    if(!handle || !supportFunctions) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    driverHandle = calloc(1, sizeof(_DriverHandle));
    if(!driverHandle) {
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    driverHandle->funcs = supportFunctions;
    driverHandle->default_setting = ar0231_raw12_default_v7;
    driverHandle->tempSensCali.tempratureData[0] = 0;
    driverHandle->tempSensCali.tempratureData[1] = 0;

    driverHandle->oscMhz = 27;
    driverHandle->maxGain = MAX_GAIN;
    driverHandle->grgbOneFlag = NVMEDIA_TRUE;
    driverHandle->loadedContext = CONTEXT_A;
    driverHandle->config_info.frameRate = 30;
    driverHandle->config_info.sensorVersion = 0;

    (void)memset(driverHandle->fuseId, 0, SIZE_FUSE_ID);

    *handle = (NvMediaISCDriverHandle *)driverHandle;

    if(clientContext) {  // ok to be NULL, then use default values
        ctx = clientContext;
        driverHandle->oscMhz = ctx->oscMHz;
        driverHandle->maxGain = ctx->maxGain;
        driverHandle->configSetIdx = ctx->configSetIdx;

        lenCfgName = strlen(ctx->moduleConfig.cameraModuleCfgName);
        if(sizeof(driverHandle->moduleCfg.cameraModuleCfgName) > lenCfgName) {
            strncpy(driverHandle->moduleCfg.cameraModuleCfgName,
                    ctx->moduleConfig.cameraModuleCfgName,
                    lenCfgName);
        } else {
            return NVMEDIA_STATUS_OUT_OF_MEMORY;
        }

        driverHandle->moduleCfg.cameraModuleConfigPass1 = ctx->moduleConfig.cameraModuleConfigPass1;
        driverHandle->moduleCfg.cameraModuleConfigPass2 = ctx->moduleConfig.cameraModuleConfigPass2;

        if(ctx->frameRate) {
            driverHandle->config_info.frameRate = ctx->frameRate;

            switch(ctx->frameRate) {
                case 20: /* 20fps */
                    fmtSel = ISC_CONFIG_AR0231_1920X1208_20FPS;
                    break;
                case 36: /* 36fps */
                    fmtSel = ISC_CONFIG_AR0231_1920X1008_36FPS;
                    break;
                case 30: /* default fps is 30fps */
                default:
                    fmtSel = ISC_CONFIG_AR0231_1920X1208_30FPS;
                    break;
            }
        }
    }

    driverHandle->config_info.format = fmtSel;
    driverHandle->timingSettings = &ar0231_timing[ctx->configSetIdx][fmtSel];
    driverHandle->exposureModeMask = ISC_AR0231_ALL_EXPOSURE_MASK;
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DriverDestroy(
    NvMediaISCDriverHandle *handle)
{
    if(!handle) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    free(handle);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetFormat(
    unsigned int enumeratedDeviceConfig,
    ConfigFormatAR0231 *format)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208:
            *format = ISC_CONFIG_AR0231_1920X1208_30FPS;
            break;
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208_20FPS:
            *format = ISC_CONFIG_AR0231_1920X1208_20FPS;
            break;
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1008:
            *format = ISC_CONFIG_AR0231_1920X1008_36FPS;
            break;
        default:
            status = NVMEDIA_STATUS_BAD_PARAMETER;
            break;
    }

    return status;
}

static NvMediaStatus
CalculateExposureLineRate(
    float *lineRate,
    unsigned int hts)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if(!lineRate) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    *lineRate = (float)(AR0231_PCLK) / hts;

    LOG_DBG("%s: lineRate %f\n", __func__, *lineRate);

    return status;
}

static NvMediaStatus
EnableLink(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int instanceNumber,
    NvMediaBool enable)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaStatus
WriteArrayWithCommand(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    const unsigned char *arrayData)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if(!handle || !transaction || !arrayData) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    funcs = ((_DriverHandle *)handle)->funcs;
    while(arrayData[0] != 'e') {
        switch (arrayData[0]) {
            case 'w':
                status = funcs->Write(
                    transaction,
                    arrayData[1],
                    (unsigned char*)&arrayData[2]);
                arrayData += (arrayData[1] + 2);
                break;
            case 'd':
                usleep((arrayData[1] << 8) + arrayData[2]);
                arrayData += 3;
                break;
            default:
                break;
        }

        if(status != NVMEDIA_STATUS_OK) {
            break;
        }
    }

    return status;
}

static NvMediaStatus
SetDefaults(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    if(!handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    return WriteArrayWithCommand(
                handle,
                transaction,
                ((_DriverHandle *)handle)->default_setting);
}

static NvMediaStatus
WriteArray(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int arrayByteLength,
    const unsigned char *arrayData)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status;

    if(!handle || !transaction || !arrayData)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    while(arrayByteLength) {
        status = funcs->Write(
            transaction,                 // transaction
            GET_BLOCK_LENGTH(arrayData), // dataLength
            (unsigned char*) GET_BLOCK_DATA(arrayData));  // data
        if(status != NVMEDIA_STATUS_OK) {
            break;
        }

        arrayByteLength -= GET_BLOCK_LENGTH(arrayData) + 1;
        SET_NEXT_BLOCK(arrayData);
    }
    return NVMEDIA_STATUS_OK;
}

static void
GetFITbySensorVer(
    NvMediaISCDriverHandle *handle)
{
    unsigned int hts;

    hts = ((_DriverHandle *)handle)->timingSettings->hts;

    // FIT - fine integration time is depend on sensor version and HTS
    if(((_DriverHandle *)handle)->config_info.sensorVersion == AR0231_VER_7) {
        ((_DriverHandle *)handle)->config_info.fineIntegTime[0] = AR0231_FINE_INTEG_TIME_V7(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[1] = AR0231_FINE_INTEG_TIME_V7_T2;
        ((_DriverHandle *)handle)->config_info.fineIntegTime[2] = AR0231_FINE_INTEG_TIME_V7(hts);
    } else if(((_DriverHandle *)handle)->config_info.sensorVersion == AR0231_VER_6) {
        ((_DriverHandle *)handle)->config_info.fineIntegTime[0] = AR0231_FINE_INTEG_TIME_V6(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[1] = AR0231_FINE_INTEG_TIME_V6(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[2] = AR0231_FINE_INTEG_TIME_V6(hts);
    } else { // v4
        ((_DriverHandle *)handle)->config_info.fineIntegTime[0] = AR0231_FINE_INTEG_TIME_V4(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[1] = AR0231_FINE_INTEG_TIME_V4(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[2] = AR0231_FINE_INTEG_TIME_V4(hts);
    }
}

static NvMediaStatus
SetFIT(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    unsigned char fit[8]; // only write FIT for T1, T2, T3

    if((!handle) || (!transaction)){
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    GetFITbySensorVer(handle);

    // FIT T1
    fit[0] = (((_DriverHandle *)handle)->config_info.fineIntegTime[0] >> 8) & 0xFF;
    fit[1] = ((_DriverHandle *)handle)->config_info.fineIntegTime[0] & 0xFF;
    ret = WriteRegister(handle, transaction, REG_FIT1, 2, fit);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    ret = WriteRegister(handle, transaction, REG_FIT1_CB, 2, fit);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }

    // FIT T2/T3, CA and CB
    fit[0] = (((_DriverHandle *)handle)->config_info.fineIntegTime[1] >> 8) & 0xFF;
    fit[1] = ((_DriverHandle *)handle)->config_info.fineIntegTime[1] & 0xFF;
    fit[2] = fit[0]; // T2 _CB
    fit[3] = fit[1];
    fit[4] = (((_DriverHandle *)handle)->config_info.fineIntegTime[2] >> 8) & 0xFF;
    fit[5] = ((_DriverHandle *)handle)->config_info.fineIntegTime[2] & 0xFF;
    fit[6] = fit[4]; // T3 _CB
    fit[7] = fit[5];

    ret = WriteRegister(handle, transaction, REG_FIT2, 8, fit);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }

    return status;
}

static NvMediaStatus
SetDeviceConfig(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int enumeratedDeviceConfig)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char ctlData = 0;
    unsigned char regData[8] = {0};
    ConfigFormatAR0231 format = ISC_CONFIG_AR0231_1920X1208_30FPS;

    if(!handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208:
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208_20FPS:
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1008:
            status = GetFormat(enumeratedDeviceConfig, &format);
            if(status != NVMEDIA_STATUS_OK) {
                return status;
            }

            ((_DriverHandle *)handle)->config_info.enumeratedDeviceConfig =
                     enumeratedDeviceConfig;
            ((_DriverHandle *)handle)->config_info.format = format;
            ((_DriverHandle *)handle)->timingSettings = &ar0231_timing[((_DriverHandle *)handle)->configSetIdx][format];

            status = CalculateExposureLineRate(
                         &((_DriverHandle *)handle)->config_info.exposureLineRate,
                         ((_DriverHandle *)handle)->timingSettings->hts);
            if(status != NVMEDIA_STATUS_OK) {
                return status;
            }

            // set fine integration time based on sensor version and HTS
            status = SetFIT(handle, transaction);
            if(status != NVMEDIA_STATUS_OK) {
                return status;
            }
            status = WriteArrayWithCommand(handle,
                                           transaction,
                                           ((_DriverHandle *)handle)->timingSettings->settings);
            break;
        case ISC_CONFIG_AR0231_RESET_FRAME_ID:
            status = WriteRegister(
                handle,
                transaction,
                REG_STAT_FRAME_ID,
                1,
                &ctlData);
             break;
        case ISC_CONFIG_AR0231_ENABLE_USER_KNEE_LUT:
            // bit0: 0 - disable legacy companding
            status = WriteRegister(
                handle,
                transaction,
                REG_KNEE_LUT_CTL + 1,  //low byte
                1,
                &ctlData);
            break;
        case ISC_CONFIG_AR0231_ENABLE_TEMP_SENSOR:
            // only enable on Rev7
            if(((_DriverHandle *)handle)->config_info.sensorVersion == AR0231_VER_7) {
                ctlData = 0x07; // bit[2:0]: enable temp sensor
                status = WriteRegister(
                    handle,
                    transaction,
                    REG_TEMPSENS0_CTL + 1,  //low byte
                    1,
                    &ctlData);
                if(status != NVMEDIA_STATUS_OK) {
                    return status;
                }
                status = WriteRegister(
                    handle,
                    transaction,
                    REG_TEMPSENS1_CTL + 1,  //low byte
                    1,
                    &ctlData);
                if(status != NVMEDIA_STATUS_OK) {
                    return status;
                }
                status = ReadRegister(
                    handle,
                    transaction,
                    REG_TEMPSENS0_CALI,
                    8,
                    regData);
                if(status != NVMEDIA_STATUS_OK) {
                    return status;
                }

                ((_DriverHandle *)handle)->tempSensCali.slope[0] =
                    70.0 / (((regData[0] << 8) + regData[1]) - ((regData[2] << 8) + regData[3]));
                ((_DriverHandle *)handle)->tempSensCali.slope[1] =
                    70.0 / (((regData[4] << 8) + regData[5]) - ((regData[6] << 8) + regData[7]));
                ((_DriverHandle *)handle)->tempSensCali.t0[0] =
                    ((regData[2] << 8) + regData[3]) * ((_DriverHandle *)handle)->tempSensCali.slope[0] - 55.0;
                ((_DriverHandle *)handle)->tempSensCali.t0[1] =
                    ((regData[6] << 8) + regData[7]) * ((_DriverHandle *)handle)->tempSensCali.slope[1] - 55.0;
            }
            else {
                LOG_WARN("AR0231 Rev%d temperature sensors not working!\n");
            }
            break;
        case ISC_CONFIG_AR0231_ENABLE_BOTTOM_EMB:
            ctlData = 0x82; // bit 7: enable bottom emb stats
            status = WriteRegister(
                handle,
                transaction,
                REG_EMB_CTL + 1,  //low byte
                1,
                &ctlData);
            break;
        case ISC_CONFIG_AR0231_ENABLE_STREAMING:
            status = WriteArray(
                handle,
                transaction,
                sizeof(ar0231_enable_streaming),
                ar0231_enable_streaming);
            break;
        default:
            status = NVMEDIA_STATUS_NOT_SUPPORTED;
            break;
    }

    return status;
}

static NvMediaStatus
SetConfigInfo(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        ConfigInfoAR0231 *configInfo)
{
    if(!handle || !transaction || !configInfo) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    /* copy configInfo to current handle */
    (void)memcpy(&((_DriverHandle *)handle)->config_info, configInfo, sizeof(ConfigInfoAR0231));

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetConfigInfo(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        ConfigInfoAR0231 *configInfo)
{
    if(!handle || !transaction || !configInfo) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    /* copy config info of current handle to input param */
    (void)memcpy(configInfo, &((_DriverHandle *)handle)->config_info, sizeof(ConfigInfoAR0231));

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetKneeLut(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned char *lut)
{
    NvMediaStatus status;

    if(!handle || !transaction || !lut) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    status = WriteRegister(
                 handle,
                 transaction,
                 REG_USER_KNEE_LUT0,
                 24,
                 lut);

    return status;
}

static NvMediaStatus
setNonHdr(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned char whichExp
)
{
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    unsigned char regVal;

    // bypass only one channel
    regVal = 0x12 + (whichExp << 2) + ((whichExp == 2) ? 4 : 0);
    ret = WriteRegister(handle, transaction, REG_DLO_CTL, 1, &regVal);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    // 12bit raw
    regVal = 0x0C;
    ret = WriteRegister(handle, transaction, REG_DATA_FORMAT, 1, &regVal);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }

   return status;
}

static NvMediaStatus
setHdr(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char regVal;

    regVal = 0x02;
    status = WriteRegister(handle, transaction, REG_DLO_CTL, 1, &regVal);
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    regVal = 0x14;
    status = WriteRegister(handle, transaction, REG_DATA_FORMAT, 1, &regVal);
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    return status;
}

/* Set exposure mode based on the mask, only support single exposure
   and full exposure mode, otherwise, return error
 */
static NvMediaStatus
SetExposureMode(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int exposureModeMask)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    unsigned char expIdx = 0;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if (!handle || !transaction || !exposureModeMask)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if (exposureModeMask == driverHandle->exposureModeMask) {
        // No change, just return
        return NVMEDIA_STATUS_OK;
    }

    if (exposureModeMask == ISC_AR0231_ALL_EXPOSURE_MASK) {
        // full exposure mode
        status = setHdr(handle, transaction);
    } else {
        switch (exposureModeMask) {
            case ISC_AR0231_T1_EXPOSURE_MASK:
                expIdx = 0;
                break;
            case ISC_AR0231_T2_EXPOSURE_MASK:
                expIdx = 1;
                break;
            case ISC_AR0231_T3_EXPOSURE_MASK:
                expIdx = 2;
                break;
            default:
                return NVMEDIA_STATUS_BAD_PARAMETER;
        }

        status = setNonHdr(handle, transaction, expIdx);
    }

    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }

    driverHandle->exposureModeMask = exposureModeMask;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
WriteParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        unsigned int parameterSize,
        void *parameter)
{
    NvMediaStatus status;
    WriteReadParametersParamAR0231 *param;

    if(!parameter || !handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    param = (WriteReadParametersParamAR0231 *)parameter;

    switch(parameterType) {
        case ISC_WRITE_PARAM_CMD_AR0231_CONFIG_INFO:
            if(parameterSize != sizeof(param->configInfo)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }
            status = SetConfigInfo(
                handle,
                transaction,
                param->configInfo);
            break;
        case ISC_WRITE_PARAM_CMD_AR0231_SET_KNEE_LUT:
            if(parameterSize != sizeof(param->KneeLutReg)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }
            status = SetKneeLut(
                handle,
                transaction,
                param->KneeLutReg);
            break;
        case ISC_WRITE_PARAM_CMD_AR0231_EXPO_MODE:
            if(parameterSize != sizeof(param->exposureModeMask))
                return NVMEDIA_STATUS_BAD_PARAMETER;
            return SetExposureMode(
                handle,
                transaction,
                param->exposureModeMask);
        default:
            status = NVMEDIA_STATUS_BAD_PARAMETER;
            break;
    }

    return status;
}

static NvMediaStatus
ReadParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        unsigned int parameterType,
        unsigned int parameterSize,
        void *parameter)
{
    WriteReadParametersParamAR0231 *param;
    NvMediaStatus status;

    if(!parameter || !handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    param = (WriteReadParametersParamAR0231 *)parameter;
    if(parameterSize != sizeof(param->configInfo)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    switch(parameterType) {
        case ISC_READ_PARAM_CMD_AR0231_CONFIG_INFO:
            status = GetConfigInfo(
                         handle,
                         transaction,
                         param->configInfo);
            break;
        case ISC_READ_PARAM_CMD_AR0231_EXP_LINE_RATE:
            status = CalculateExposureLineRate(
                         &param->configInfo->exposureLineRate,
                         ((_DriverHandle *)handle)->timingSettings->hts);
            break;
        case ISC_READ_PARAM_CMD_AR0231_FUSE_ID:
            status = ReadRegister(
                          handle,
                          transaction,
                          REG_FUSE_ID,
                          SIZE_FUSE_ID,
                          ((_DriverHandle *)handle)->fuseId);
            break;
        default:
            status = NVMEDIA_STATUS_NOT_SUPPORTED;
            break;
    }

    return status;
}

static NvMediaStatus
ReadRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int registerNum,
    unsigned int dataLength,
    unsigned char *dataBuff)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char registerData[REG_ADDRESS_BYTES];
    NvMediaStatus status;

    if(!handle || !transaction || !dataBuff) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    funcs = ((_DriverHandle *)handle)->funcs;

    registerData[0] = registerNum >> 8;
    registerData[1] = (registerNum & 0xFF);
    status = funcs->Read(
        transaction,
        REG_ADDRESS_BYTES, // regLength
        registerData,      // regData
        dataLength,        // dataLength
        dataBuff);         // data

    if(status != NVMEDIA_STATUS_OK) {
        LOG_DBG("%s: sensor read failed: 0x%x, length %d\n", __func__, registerNum, dataLength);
    }
    return status;
}

static NvMediaStatus
WriteRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int registerNum,
    unsigned int dataLength,
    unsigned char *dataBuff)
{
    NvMediaISCSupportFunctions *funcs;
    unsigned char data[REG_ADDRESS_BYTES + REG_WRITE_BUFFER];
    NvMediaStatus status;

    if(!handle || !transaction || !dataBuff) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = registerNum >> 8;
    data[1] = registerNum & 0xFF;
    memcpy(&data[2], dataBuff, dataLength);

    status = funcs->Write(
        transaction,
        dataLength + REG_ADDRESS_BYTES,    // dataLength
        data);                             // data

    if(status != NVMEDIA_STATUS_OK) {
        LOG_DBG("%s: sensor write failed: 0x%x, length %d\n", __func__, registerNum, dataLength);
    }
    return status;
}

static NvMediaStatus
GetTemperature(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    float *temperature)
{
    _DriverHandle *driverHandle;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char buff[REG_DATA_BYTES] = {0};
    unsigned int i, regAdd;
    float tempValue[2];

    if(!handle || !transaction || !temperature) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    driverHandle = (_DriverHandle *)handle;

    if(driverHandle->config_info.sensorVersion != AR0231_VER_7) {
        LOG_WARN("AR0231 Rev%d temperature sensors not functional!\n");
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    regAdd = REG_TEMPSENS0_DATA;

    for(i = 0; i < 2; i++) { // 2 temperature aensors
        if(driverHandle->tempSensCali.tempratureData[i] == 0) {// in case parseEmbeddedData() hasn't been called yet
            status = ReadRegister(handle,
                              transaction,
                              regAdd + (i * 2),
                              REG_DATA_BYTES,
                              buff);
            if(status != NVMEDIA_STATUS_OK) {
                return status;
            }

            driverHandle->tempSensCali.tempratureData[i] = (buff[0] << 8) + buff[1];
        }
    }

    tempValue[0] = driverHandle->tempSensCali.slope[0] * (float)driverHandle->tempSensCali.tempratureData[0]
                    - driverHandle->tempSensCali.t0[0];
    tempValue[1] = driverHandle->tempSensCali.slope[1] * (float)driverHandle->tempSensCali.tempratureData[1]
                    - driverHandle->tempSensCali.t0[1];
    // Return the highest value from 2 temperature Senors
    *temperature = (tempValue[0] > tempValue[1]) ? tempValue[0] : tempValue[1];

    LOG_DBG("%s: sensor0 temp: %.3f, sensor1 temp: %.3f\n", __func__, tempValue[0], tempValue[1]);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
CheckPresence(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status;
    unsigned char readBuff[2] = {0};

    if(!handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    status = ReadRegister(handle,
                          transaction,
                          REG_CHIP_ID,
                          REG_DATA_BYTES,
                          readBuff);
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    if((readBuff[0] != ((AR0231_CHIP_ID >> 8) & 0xff)) || (readBuff[1] != (AR0231_CHIP_ID & 0xff))) {
        return NVMEDIA_STATUS_ERROR;
    }

    status = ReadRegister(handle,
                          transaction,
                          REG_CHIP_VER + 1,  //low byte
                          1,
                          readBuff);
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    if((readBuff[0] & 0x0Fu) == 7u) { // sensor version, [3:0]
        ((_DriverHandle *)handle)->config_info.sensorVersion = AR0231_VER_7;
        if(((readBuff[0] >> 4) & 0x0Fu) != 3u) { // 3 - RCCB
            LOG_WARN("This is AR0231 Rev7, but not RCCB sensor!\n");
        } else {
            printf("Sensor AR0231 RCCB Rev7 detected!\n"); // To be fixed, LOG
        }
    } else if(((readBuff[0] & 0x0Fu) == 5u) || ((readBuff[0] & 0x0Fu) == 6u)){
        ((_DriverHandle *)handle)->config_info.sensorVersion = AR0231_VER_6;
        ((_DriverHandle *)handle)->default_setting = ar0231_raw12_default_v6;
        LOG_INFO("Sensor AR0231 Rev6 detected!\n");
    } else if((readBuff[0] & 0x0Fu) == 4u) {
        ((_DriverHandle *)handle)->config_info.sensorVersion = AR0231_VER_4;
        ((_DriverHandle *)handle)->default_setting = ar0231_raw12_default_v4;
        LOG_INFO("Sensor AR0231 Rev4 detected!\n");
    } else {
        LOG_ERR("This sensor is v3 or older, rccb mode is not supported\n");
        status = NVMEDIA_STATUS_ERROR;
    }
    return status;
}

static NvMediaStatus
DumpRegisters(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char data[REG_DATA_BYTES] = {0, 0};
    unsigned char *arrayData;

   if(!handle || !transaction) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
   }

    funcs = ((_DriverHandle *)handle)->funcs;
    arrayData = (unsigned char*)((_DriverHandle *)handle)->default_setting;

    while(arrayData[0] != 'e') {
        if(arrayData[0] == 'w') {
            status = funcs->Read(
                transaction,
                REG_ADDRESS_BYTES,     // regLength
                &arrayData[2],         // regData
                REG_DATA_BYTES,        // dataLength
                data);                 // dataBuff

            LOG_DBG("20 %.2X%.2X %.2x %.2x\n", (unsigned int)arrayData[2],
                (unsigned int)arrayData[3], data[0], data[1]);
            arrayData += (arrayData[1] + 2);
        } else {
            arrayData += 3;  // for 'd'
        }
    }

    return status;
}

static unsigned int
GetAgainValT1(
    NvMediaISCExposureControl *exposureControl,
    unsigned int cgain)
{
    unsigned int i, minAgVal;
    float tmp;

    minAgVal = ((cgain & 0x1) == 1) ? 7 : 6;

    tmp = exposureControl->sensorGain[0].value / ((cgain & 0x1) ? HIGH_CGAIN : 1);
    if(tmp < MIN_AGAIN_LCG) {
        tmp = (cgain & 0x1) ? MIN_AGAIN_HCG : MIN_AGAIN_LCG;
    }

    for(i = NUM_A_GAIN - 1; i >= minAgVal; i--) {
        if(tmp >= aGainTbl[i]) {
            break;
        }
    }

    return i;
}

static unsigned int
GetAgainValNonT1(
    NvMediaISCExposureControl *exposureControl,
    unsigned int cgain,
    unsigned int dGain)
{
    unsigned int i, j, k = 0, minAgVal;
    float tmp;

    for(i = 1; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        minAgVal = (((cgain >> i) & 0x1) == 1) ? 7 : 6;

        tmp = exposureControl->sensorGain[i].value / (((cgain >> i) & 0x1) ?
              HIGH_CGAIN : 1) / dGain * ONE_DGAIN_VAL;
        if(tmp < MIN_AGAIN_LCG) {
            tmp = (cgain & 0x1) ? MIN_AGAIN_HCG : MIN_AGAIN_LCG;
        }

        for(j = NUM_A_GAIN - 1; j >= minAgVal; j--) {
            if(tmp >= aGainTbl[j]) {
                break;
            }
        }

        if((j < NUM_A_GAIN - 1) && ((aGainTbl[j + 1] - tmp) < (tmp - aGainTbl[j]))) {
            j++;
        }

        k += j << (i * 4);
    }

    return k;
}

static unsigned int
GetDgainVal(
    NvMediaISCExposureControl *exposureControl,
    unsigned int cgain,
    unsigned int again)
{
    unsigned int i;

    //accrate for T1, T2/T3 use the same digital gain
    i = (unsigned int)(exposureControl->sensorGain[0].value / ((cgain & 0x1) ?
        HIGH_CGAIN : 1) / aGainTbl[again & 0xf] * ONE_DGAIN_VAL + 0.5);

    if(i > MAX_DGAIN_VAL) {
        return MAX_DGAIN_VAL;
    }

    if(i < ONE_DGAIN_VAL) {
        return ONE_DGAIN_VAL;
    }

    return i;
}

static unsigned int
GetCgainVal(
    NvMediaISCExposureControl *exposureControl,
    NvMediaBool *flag3x)
{
    unsigned char i, cGain = 0;
    float mGain = exposureControl->sensorGain[0].value;

    *flag3x = NVMEDIA_FALSE;

    if((mGain == exposureControl->sensorGain[1].value) &&
        (mGain == exposureControl->sensorGain[2].value)) {
        if(mGain >= (HIGH_CGAIN * MIN_AGAIN_HCG * MIN_DGAIN)) {
            cGain = 0xf;
        }
        return cGain;
    }

    for(i = 1; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if(mGain > exposureControl->sensorGain[i].value) {
            mGain = exposureControl->sensorGain[i].value;
        }
    }

    for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if(exposureControl->sensorGain[i].value != mGain) {
            if((unsigned int)(exposureControl->sensorGain[i].value * 10 + 0.5) ==
                (unsigned int)(mGain * 10 * HIGH_CGAIN + 0.5)) {
                cGain |= 1 << i;
            } else {
                break;
            }
        }
    }

    if(i != NVMEDIA_ISC_EXPOSURE_MODE_MAX) {
        *flag3x = NVMEDIA_TRUE;
        cGain = 0;
        for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
             if(exposureControl->sensorGain[i].value >= (HIGH_CGAIN * MIN_AGAIN_HCG * MIN_DGAIN)) {
                 cGain |= 1 << i;
             }
        }
    }

    return cGain;
}

static NvMediaStatus
ReCalculate3ExpTime(
    NvMediaISCDriverHandle *handle,
    NvMediaISCExposureControl *exposureControl,
    unsigned int *exposureLines)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned int hts;
    const unsigned short int *fineIntegTime;

    if(!handle) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    hts = ((_DriverHandle *)handle)->timingSettings->hts;
    fineIntegTime = ((_DriverHandle *)handle)->config_info.fineIntegTime;

    // re-calculate expTime for 3-exp
    // e3 = (cit3 - 1) * llpck + fit3
    // e2 = (cit2 + cit3 - 2) * llpck + fit2 + fit3
    // e1 = (cit1 + cit2 + cit3 - 3) * llpck + fit1 + fit2 + fit3
    exposureLines[2] = (unsigned int)(exposureControl->exposureTime[2].value *
                       ((_DriverHandle *)handle)->config_info.exposureLineRate + 1 -
                       (float)fineIntegTime[2] / hts + 0.5);
    if(exposureLines[2] < 1) {
         exposureLines[2] = 1;
    } else if (exposureLines[2] > MAX_AE_EXP_LINES_T3) {
         exposureLines[2] = MAX_AE_EXP_LINES_T3;
    }

    exposureLines[1] = (unsigned int)(exposureControl->exposureTime[1].value *
                       ((_DriverHandle *)handle)->config_info.exposureLineRate -
                       exposureLines[2] + 2 - (float)(fineIntegTime[1] +
                       fineIntegTime[2]) / hts + 0.5);
    if(exposureLines[1] < 1) {
         exposureLines[1] = 1;
    } else if (exposureLines[1] > MAX_AE_EXP_LINES_T2) {
         exposureLines[1] = MAX_AE_EXP_LINES_T2;
    }

    exposureLines[0] = (unsigned int)(exposureControl->exposureTime[0].value *
                       ((_DriverHandle *)handle)->config_info.exposureLineRate -
                       exposureLines[1] - exposureLines[2] + 3 - (float)(fineIntegTime[0] +
                       fineIntegTime[1] + fineIntegTime[2]) / hts + 0.5);
    if(exposureLines[0] < 1) {
        exposureLines[0] = 1;
    }

    EXPOSURE_PRINT("%s: ExpT1: %f Lines: %d; ExpT2: %f Lines: %d; ExpT3: %f Lines: %d\n",
                   __func__, exposureControl->exposureTime[0].value,
                   (unsigned int)(exposureLines[0] + exposureLines[1] + exposureLines[2] - 3 +
                   (float)(fineIntegTime[0] + fineIntegTime[1] + fineIntegTime[2]) /
                   hts), exposureControl->exposureTime[1].value,
                   (unsigned int)(exposureLines[1] + exposureLines[2] - 2 +
                   (float)(fineIntegTime[1] + fineIntegTime[2]) / hts),
                   exposureControl->exposureTime[2].value,
                   (unsigned int)(exposureLines[2] - 1 + (float)fineIntegTime[2] /
                   hts));
    return status;
}

static NvMediaStatus
WriteExpRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int exposureLines,
    unsigned int whichExp)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char regVal[2];
    unsigned int regAdd;
    unsigned int dataLength;

    if(whichExp == 0) { // T1 exp has 2 bytes
        regAdd = REG_INTEG_TIME_T1;
        regVal[0] = (exposureLines >> 8) & 0xFF;
        regVal[1] = exposureLines & 0xFF;
        dataLength = 2;
    } else { // T2, T3 exp has 1 byte
        regAdd = (REG_INTEG_TIME_T2 + (whichExp - 1) * 4) + 1;
        regVal[0] = exposureLines & 0xFF;
        dataLength = 1;
    }

    status = WriteRegister(handle, transaction, regAdd, dataLength, regVal);

    return status;
}

static NvMediaStatus
SetExposureWork(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaISCExposureControl *exposureControl)
{
    _DriverHandle *driverHandle;
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    unsigned char regVal[2];
    unsigned int aGainVal = 0, dGainVal = 0, cGainVal = 0;
    unsigned int i, exposureLines[3] = {0,0,0};
    NvMediaBool flag3x;
    unsigned int hts;
    const unsigned short int *fineIntegTime;

    if(!handle || !transaction || !exposureControl) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    driverHandle = (_DriverHandle *)handle;

    hts = driverHandle->timingSettings->hts;
    fineIntegTime = driverHandle->config_info.fineIntegTime;
    regVal[0] = exposureControl->sensorFrameId;

    ret = WriteRegister(handle, transaction, REG_STAT_FRAME_ID, 1, regVal);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    EXPOSURE_PRINT("%s: set sensor FrameId = %x\n", __func__, regVal[0]);

    if((exposureControl->sensorGain[0].valid) || (exposureControl->sensorGain[1].valid)
        || (exposureControl->sensorGain[2].valid)) {
        // group hold
        regVal[0] = 0x01;
        ret = WriteRegister(handle, transaction, REG_GROUP_HOLD, 1, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }

        // conversion gain
        cGainVal = GetCgainVal(exposureControl, &flag3x);
        driverHandle->cGainCa = cGainVal;

        // analog gain for T1
        aGainVal = GetAgainValT1(exposureControl, cGainVal);
        if(!flag3x) { // aGain is the same for all exp channels
            aGainVal |= (aGainVal << 4) | (aGainVal << 8) | (aGainVal << 12);
        }

        // globle digital gain is accurate for T1
        dGainVal = GetDgainVal(exposureControl, cGainVal, aGainVal);

        // analog gain for T2...or more
        if(flag3x) {
            aGainVal += GetAgainValNonT1(exposureControl, cGainVal, dGainVal);
        }

        EXPOSURE_PRINT("%s: gainT1 %f, gainT2 %f, gainT3 %f, cG %x, aG %x, dG %x\n", __func__, \
                 exposureControl->sensorGain[0].value, exposureControl->sensorGain[1].value,\
                 exposureControl->sensorGain[2].value, cGainVal, aGainVal, dGainVal);

        regVal[0] = cGainVal & 0x0F;
        ret = WriteRegister(handle, transaction, REG_CGAIN + 1, 1, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
        regVal[0] = (aGainVal >> 8) & 0xFF;
        regVal[1] = aGainVal & 0xFF;
        ret = WriteRegister(handle, transaction, REG_AGAIN, 2, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
        regVal[0] = (dGainVal >> 8) & 0xFF;
        regVal[1] = dGainVal & 0xFF;
        ret = WriteRegister(handle, transaction, REG_DGAIN, 2, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }

    if(driverHandle->exposureModeMask == ISC_AR0231_ALL_EXPOSURE_MASK) { // 3-exp
        ret = ReCalculate3ExpTime(handle, exposureControl, exposureLines);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }

        for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            ret = WriteExpRegister(handle, transaction, exposureLines[i], i);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
        }

    } else { // non-HDR 1 exp only
        for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            if(exposureControl->exposureTime[i].valid) { // 1st valid form T1, T2 or T3
                exposureLines[i] = (unsigned int)(exposureControl->exposureTime[i].value *
                                    driverHandle->config_info.exposureLineRate + 1 -
                                    (float)fineIntegTime[i] / hts + 0.5);

                ret = WriteExpRegister(handle, transaction, exposureLines[i], i);
                if(ret != NVMEDIA_STATUS_OK) {
                    status = NVMEDIA_STATUS_ERROR;
                }

                EXPOSURE_PRINT("%s: ExpT%d: %f Lines: %d\n",
                                __func__, i+1, exposureControl->exposureTime[i].value,
                                exposureLines[i] - 1);
                break;
            }
        }
    }

    if((exposureControl->sensorGain[0].valid) || (exposureControl->sensorGain[1].valid)
        || (exposureControl->sensorGain[2].valid)) {
        // group hold clear
        regVal[0] = 0x00;
        ret = WriteRegister(handle, transaction, REG_GROUP_HOLD, 1, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }

    return status;
}

static NvMediaStatus
SetExposure(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaISCExposureControl *exposureControl)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    NvMediaISCExposureControl exposureControlTmp;
    unsigned int i, k;

    if (!handle || !transaction || !exposureControl)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    /* For all exposure mode and long exposure mode */
    if (driverHandle->exposureModeMask == ISC_AR0231_ALL_EXPOSURE_MASK) {
        return SetExposureWork(handle, transaction, exposureControl);
    }

    /* Re-map the settings for other modes combinations */
    memset(&exposureControlTmp, 0, sizeof(exposureControlTmp));
    exposureControlTmp.sensorFrameId = exposureControl->sensorFrameId;
    exposureControlTmp.hdrRatio = exposureControl->hdrRatio;
    exposureControlTmp.sensorMode = exposureControl->sensorMode;

    k = 0;
    for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if (driverHandle->exposureModeMask & (1 << i)) {
            exposureControlTmp.exposureTime[i].valid = exposureControl->exposureTime[k].valid;
            exposureControlTmp.exposureTime[i].value = exposureControl->exposureTime[k].value;
            exposureControlTmp.sensorGain[i].valid = exposureControl->sensorGain[k].valid;
            exposureControlTmp.sensorGain[i].value = exposureControl->sensorGain[k].value;
            k++;
        }
    }

    return SetExposureWork(handle, transaction, &exposureControlTmp);
}

static NvMediaStatus
SetExposureCb(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaISCExposureControl *exposureControl)
{
    _DriverHandle *driverHandle;
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    unsigned char regVal[2];
    unsigned int aGainVal = 0, dGainVal = 0, cGainVal = 0;
    unsigned int i, exposureLines[3] = {0, 0, 0};
    NvMediaBool flag3x;
    unsigned int regAdd;

    if((!handle) || (!transaction) ||(!exposureControl)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    driverHandle = (_DriverHandle *)handle;

    if(exposureControl->sensorGain[0].valid) {
        // conversion gain
        cGainVal = GetCgainVal(exposureControl, &flag3x);

        //analog gain for T1
        aGainVal = GetAgainValT1(exposureControl, cGainVal);
        if(!flag3x) { // aGain is the same for all exp channels
            aGainVal |= (aGainVal << 4) | (aGainVal << 8) | (aGainVal << 12);
        }

        //globle digital gain is accurate for T1
        dGainVal = GetDgainVal(exposureControl, cGainVal, aGainVal);

        //analog gain for T2...or more
        if(flag3x) {
            aGainVal += GetAgainValNonT1(exposureControl, cGainVal, dGainVal);
        }

        EXPOSURE_PRINT("%s: gainT1 %f, gainT2 %f, gainT3 %f, cG %x, aG %x, dG %x\n", __func__, \
                 exposureControl->sensorGain[0].value, exposureControl->sensorGain[1].value,\
                 exposureControl->sensorGain[2].value, cGainVal, aGainVal, dGainVal);

        //cGain for ContextA&B
        regVal[0] = (cGainVal << 4) + driverHandle->cGainCa;
        ret = WriteRegister(handle, transaction, REG_CGAIN + 1, 1, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }

        regVal[0] = (aGainVal >> 8) & 0xFF;
        regVal[1] = aGainVal & 0xFF;
        ret = WriteRegister(handle, transaction, REG_AGAIN_CB, 2, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }

        regVal[0] = (dGainVal >> 8) & 0xFF;
        regVal[1] = dGainVal & 0xFF;
        ret = WriteRegister(handle, transaction, REG_DGAIN_CB, 2, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }

    //Get integration time for 3-exp
    ret = ReCalculate3ExpTime(handle, exposureControl, exposureLines);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }

    // T1
    if(exposureControl->exposureTime[0].valid) {
        regAdd = REG_INTEG_TIME_T1_CB;
        regVal[0] = exposureLines[0] >> 8 & 0xFF;
        regVal[1] = exposureLines[0] & 0xFF;
        ret = WriteRegister(handle, transaction, regAdd, 2, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }

    // T2, T3
    for(i = 1; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if(exposureControl->exposureTime[i].valid) {
            regAdd = REG_INTEG_TIME_T2_CB + (i - 1) * 4 + 1;
            regVal[0] = exposureLines[i] & 0xFF;
            ret = WriteRegister(handle, transaction, regAdd, 1, regVal);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
        }
    }

    return status;
}

static NvMediaStatus
getSenVerFromEmb(
    _DriverHandle *driverHandle,
    const unsigned char *data,
    EmbeddedDataType embType)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char emb[5] = {0};

    if((driverHandle == NULL) || (data == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(embType == EMB_PARSED) {
        emb[0] = data[EMB_OFFSET_CHIP_VER - EMB_EXTRA_OFFSET_V6];
        emb[1] = data[EMB_OFFSET_CHIP_VER];
        emb[2] = data[EMB_OFFSET_CHIP_VER + EMB_EXTRA_OFFSET_V6];
        emb[3] = data[EMB_OFFSET_CHIP_VER + EMB_EXTRA_OFFSET_V7];
        emb[4] = data[EMB_OFFSET_CHIP_VER + EMB_EXTRA_OFFSET_V7 + 2];
    } else if(embType == EMB_UNPARSED) {
        emb[0] = (unsigned char)(*((unsigned int *)(data + (EMB_OFFSET_CHIP_VER - EMB_EXTRA_OFFSET_V6) * 4u)) >> 6u);
        emb[1] = (unsigned char)(*((unsigned int *)(data + EMB_OFFSET_CHIP_VER * 4u )) >> 6u);
        emb[2] = (unsigned char)(*((unsigned int *)(data + (EMB_OFFSET_CHIP_VER + EMB_EXTRA_OFFSET_V6) * 4u)) >> 6u);
        emb[3] = (unsigned char)(*((unsigned int *)(data + (EMB_OFFSET_CHIP_VER + EMB_EXTRA_OFFSET_V7) * 4u)) >> 6u);
        emb[4] = (unsigned char)(*((unsigned int *)(data + (EMB_OFFSET_CHIP_VER + EMB_EXTRA_OFFSET_V7 + 2) * 4u)) >> 6u);
    }

    if((emb[3] == 0xFEu) && (( emb[4] & 0x0Fu) == 7u)) {
        driverHandle->config_info.sensorVersion = AR0231_VER_7;
    } else if((emb[1] == 0xFCu) && ((( emb[2] & 0x0Fu) == 5u) || (( emb[2] & 0x0Fu) == 6u))) {
        driverHandle->config_info.sensorVersion = AR0231_VER_6;
    } else if((emb[0] == 0xFC) && ((emb[1] & 0x0F) == 4u)) {
        driverHandle->config_info.sensorVersion = AR0231_VER_4;
    } else {
        status = NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    return status;
}

static NvMediaStatus
ParseExposure(
    _DriverHandle *driverHandle,
    NvMediaISCEmbeddedData *parsedInformation,
    float lineRate)
{
    unsigned char *data;
    unsigned int i, k, agVal, numValidExposures = 0;
    unsigned char cgVal, regPair[2];
    float dgain, exposureMidpoint = 0.0;
    float expT[3];
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned int hts;
    const unsigned short int *fineIntegTime;
    unsigned char sensorVer;

    if(!driverHandle || !parsedInformation || (lineRate == 0.0f)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    hts = driverHandle->timingSettings->hts;
    fineIntegTime = driverHandle->config_info.fineIntegTime;
    sensorVer = driverHandle->config_info.sensorVersion;
    data = (unsigned char*)(parsedInformation->top.data);

    // for re-process
    if(sensorVer == 0u) {
        status = getSenVerFromEmb(driverHandle, data, EMB_PARSED);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_WARN("AR0231 sensor version is unknown!\n");
        }

        GetFITbySensorVer(driverHandle);
    }

    regPair[0] = data[EMB_OFFSET_DGAIN(sensorVer)];
    regPair[1] = data[EMB_OFFSET_DGAIN(sensorVer) + 1];
    dgain = (float)((regPair[0] << 8u) + regPair[1]) / (float)ONE_DGAIN_VAL;

    regPair[0] = data[EMB_OFFSET_AGAIN(sensorVer)];
    regPair[1] = data[EMB_OFFSET_AGAIN(sensorVer) + 1];
    agVal = (regPair[0] << 8) + regPair[1];

    cgVal = data[EMB_OFFSET_CGAIN(sensorVer)];

    for(i = 0; i < (float)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        memset(&parsedInformation->exposure[i], 0,
               sizeof(parsedInformation->exposure[i]));
    }

    k = 0;
    for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if(driverHandle->exposureModeMask & (1 << i)) {
            parsedInformation->exposure[k].digitalGain = dgain;

            parsedInformation->exposure[k].analogGain = aGainTbl[(agVal >> (4 * i)) & 0xF];
            parsedInformation->exposure[k].conversionGain = (((cgVal >> i) & 0x01) == 0) ? 1.0 : HIGH_CGAIN;

            regPair[0] = data[EMB_OFFSET_EXP_T1 + (i * 2)];
            regPair[1] = data[EMB_OFFSET_EXP_T1 + (i * 2) + 1];
            expT[i] = (((regPair[0] << 8) + regPair[1]) - 1 +
                (float)fineIntegTime[i] / hts) / lineRate;

            parsedInformation->exposure[k].exposureTime = expT[i];
            k++;
        }
    }

    if(driverHandle->exposureModeMask == ISC_AR0231_ALL_EXPOSURE_MASK) {
        // HDR
        // e2 += e3
        // e1 += e2 + e3
        parsedInformation->exposure[1].exposureTime += expT[2];
        parsedInformation->exposure[0].exposureTime += (expT[1] + expT[2]);
    }

    for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        EXPOSURE_PRINT("%s: T%i, cGain = %f, aGain = %f, expTime = %f\n", __func__, i+1, \
            parsedInformation->exposure[i].conversionGain, parsedInformation->exposure[i].analogGain, \
            parsedInformation->exposure[i].exposureTime);

        if(parsedInformation->exposure[i].exposureTime != 0.0) {
            exposureMidpoint += 0.5 * parsedInformation->exposure[i].exposureTime;
            numValidExposures++;
        }
    }

    EXPOSURE_PRINT("%s: global dGain = %f\n", __func__, parsedInformation->exposure[0].digitalGain);

    if(numValidExposures) {
        // average frame middle points
        exposureMidpoint /= (float)numValidExposures;
    }
    parsedInformation->exposureMidpointTime = exposureMidpoint;

    return status;
}

static NvMediaStatus
SetWBGain(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvMediaISCWBGainControl *wbControl)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned short gain;
    unsigned char regVal[8];
    unsigned char *dataAdd;
    unsigned int i, index, regAdd, dataLength;
    unsigned int firstValid = 0xff; // overwrite with i when 1st wbGain[i].valid is TRUE

    if(!handle || !transaction || !wbControl) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    // check 1st valid
    for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if(wbControl->wbGain[i].valid) {
            firstValid = i;
            break;
        }
    }

    // global WB gains
    if(firstValid != 0xff) {
        for(i = 0; i < 4; i++) {  //Gr, B, R, Gb
            index = colorIndex[i]; //R, Gr, Gb, B
            gain = (unsigned short) ((wbControl->wbGain[firstValid].value[index] *
                    ONE_COLOR_DGAIN_VAL) + 0.5);
            if(gain) {
                regVal[i * 2] = (gain >> 8) & 0xFF;
                regVal[i * 2 + 1] = gain & 0xFF;
                LOG_DBG("%s: SetWBGain %d: %x\n", __func__, i, gain);
            }
        }
        // in most cases, Gr gain and Gb gain are 1.0
        if((((_DriverHandle *)handle)->grgbOneFlag == NVMEDIA_TRUE) &&
            (regVal[0] == 0x00) && (regVal[1] == ONE_COLOR_DGAIN_VAL) &&
            (regVal[6] == 0x00) && (regVal[7] == ONE_COLOR_DGAIN_VAL)) {
            regAdd = REG_DGAIN_GR + 2; // start from B gain
            dataLength = 4; // write only B and R gain
            dataAdd = &regVal[2];
        } else {
            regAdd = REG_DGAIN_GR;
            dataLength = 8;
            dataAdd = &regVal[0];

            if((regVal[0] == 0x00) && (regVal[1] == ONE_COLOR_DGAIN_VAL) &&
            (regVal[6] == 0x00) && (regVal[7] == ONE_COLOR_DGAIN_VAL)) {
                ((_DriverHandle *)handle)->grgbOneFlag = NVMEDIA_TRUE;
            } else {
                ((_DriverHandle *)handle)->grgbOneFlag = NVMEDIA_FALSE;
            }
        }
        status = WriteRegister(handle, transaction, regAdd, dataLength, dataAdd);
    } else { // all wbGain[i].valid are FALSE
        LOG_WARN("No valid flag set for WB gains.\n");
    }

    return status;
}

static NvMediaStatus
ParseWBGain(
    _DriverHandle *driverHandle,
    NvMediaISCEmbeddedData *parsedInformation)
{
    unsigned char *data = (unsigned char*)(parsedInformation->top.data);
    int i, j, k, index, address;
    unsigned char regPair[2];
    float value;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if(!parsedInformation) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(driverHandle->config_info.sensorVersion == 0u) {
        status = getSenVerFromEmb(driverHandle, data, EMB_PARSED);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_WARN("AR0231 sensor version is unknown!\n");
        }
    }

    address = EMB_OFFSET_DGAIN_GR(driverHandle->config_info.sensorVersion);

    for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        memset(parsedInformation->wbGain[i].value, 0,
               sizeof(parsedInformation->wbGain[i].value));
    }

    for(i = 0; i < 4; i++) {  // Gr, B, R, Gb
        index = colorIndex[i]; // R, Gr, Gb, B
        regPair[0] = data[address];
        regPair[1] = data[address + 1];
        value = (float)((regPair[0] << 8) + regPair[1]) / ONE_COLOR_DGAIN_VAL;

        // all channels (if enabled) have same wb gains
        // if channel is not enabled, wb gain values are zero
        k = 0;
        for(j = 0; j < NVMEDIA_ISC_EXPOSURE_MODE_MAX; j++) {
            if(driverHandle->exposureModeMask & (1 << j)) {
                parsedInformation->wbGain[k].value[index] = value;
                k++;
            }
        }
        address += 2;
        LOG_DBG("%s: %d: %f\n", __func__, i, value);
    }

    return status;
}

static NvMediaStatus
ParseEmbeddedData(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int lineCount,
    unsigned int *lineLength,
    unsigned char *lineData[],
    NvMediaISCEmbeddedData *parsedInformation)
{
    _DriverHandle *driverHandle;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned int i;
    unsigned int stride = 4;
    unsigned int shift;
    unsigned char *dst;
    unsigned char *embBotBuffer;
    float lineRate;
    unsigned int frame_counter;
    unsigned char sensorVer;

    if(!handle || !transaction || !lineLength || !lineData || !parsedInformation) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    driverHandle = (_DriverHandle *)handle;
    lineRate = driverHandle->config_info.exposureLineRate;

    shift = 6; // top: 8 bit data in bit[13:6] on each 16-bit

    if(parsedInformation->top.bufferSize < lineLength[0] / stride) {
        status = NVMEDIA_STATUS_INSUFFICIENT_BUFFERING;
        goto done;
    }

    dst = (unsigned char*)parsedInformation->top.data;
    sensorVer = driverHandle->config_info.sensorVersion;

    // total buffer size: lineLength[0] = ACTIVE_H * 32; only extract used data bytes
    for(i = 0; i < (float)(EMB_OFFSET_AGAIN(sensorVer) * 4 + 20); i += stride) {
        *dst++  = (*((unsigned short int*)(lineData[0] + i))) >> shift; // skip tag byte
    }

    // top.baseRegAddress can not be used by app
    parsedInformation->top.baseRegAddress = 0x0;
    parsedInformation->top.size = lineLength[0] / stride;

    // parse gains and the exposure time
    status = ParseExposure(driverHandle, parsedInformation, lineRate);
    if(status != NVMEDIA_STATUS_OK) {
        goto done;
    }

    // parse WB gains
    status = ParseWBGain(driverHandle, parsedInformation);
    if(status != NVMEDIA_STATUS_OK) {
        goto done;
    }

    // parse sensor temprature data
    driverHandle->tempSensCali.tempratureData[0] =
            (parsedInformation->top.data[EMB_OFFSET_TEMPSENS0(sensorVer)] << 8) +
             parsedInformation->top.data[EMB_OFFSET_TEMPSENS0(sensorVer) + 1];
    driverHandle->tempSensCali.tempratureData[1] =
            (parsedInformation->top.data[EMB_OFFSET_TEMPSENS0(sensorVer) + 2] << 8) +
             parsedInformation->top.data[EMB_OFFSET_TEMPSENS0(sensorVer) + 3];

    parsedInformation->mode = 1;  // 12bit: 1;

    // parse frame counter
    frame_counter = (parsedInformation->top.data[EMB_OFFSET_FRAME_CNT] << 24) |
                    (parsedInformation->top.data[EMB_OFFSET_FRAME_CNT + 1] << 16) |
                    (parsedInformation->top.data[EMB_OFFSET_FRAME_CNT + 2] << 8) |
                    parsedInformation->top.data[EMB_OFFSET_FRAME_CNT + 3];
    parsedInformation->frameSequenceNumber = frame_counter;

    if((lineCount == 1) || (lineLength[1] == 0)) {
        goto done;
    }

    if(parsedInformation->bottom.bufferSize < lineLength[1] / stride) {
        status = NVMEDIA_STATUS_INSUFFICIENT_BUFFERING;
        goto done;
    }

    shift = 4;
    // bottom emb
    dst = (unsigned char*)parsedInformation->bottom.data;
    memset(dst, 0, lineLength[1]);

    // row1:
    embBotBuffer = lineData[1] + EMB_OFFSET_BOTTOM_ROW1;
    // 0: 8bit(--xx,xxxx,xx--,----); line start code
    *dst++ = (*(unsigned short int*)embBotBuffer) >> 6;
    dst++;
    // 1: 10bit(--xx,xxxx,xxxx,----)
    *dst++ = (*(unsigned short int*)(embBotBuffer + 2)) >> shift;
    *dst++ = (*(embBotBuffer + 3) >> shift) & 0x03;
    // 2~5: 2 bytes register (----,xxxx,xxxx,----)(----,xxxx,xxxx,----)
    for(i = 2 * 2; i < (R1_OFFSET_ROI1_PIXELS - 2) * 2; i += stride) {
        *dst++ = (*(unsigned short int*)(embBotBuffer + i + 2)) >> shift;
        *dst++ = (*(unsigned short int*)(embBotBuffer + i)) >> shift;
        dst += 2;
    }
    // 6~7: skip 2 bytes 00
    dst += 4;
    // 8~989: 20bit(--xx,xxxx,xxxx,----, --xx,xxxx,xxxx,----)
    for(i = R1_OFFSET_ROI1_PIXELS * 2; i < R1_OFFSET_DUMMY * 2; i += stride) {
        *dst++ = (*(unsigned short int*)(embBotBuffer + i + 2)) >> shift;
        *dst++ = (((*(unsigned short int*)(embBotBuffer + i)) >> 2) & 0xFC) +
                 ((*(embBotBuffer + i + 3) >> shift) & 0x03);
        *dst++ = ((*(embBotBuffer + i + 1)) >> 2) & 0x0F;
        dst++;
    }
    // 990~row1 end: skip dummy bytes
    dst += AR0231_ACTIVE_H * 2 - R1_OFFSET_DUMMY * 2;

    // row2:
    embBotBuffer = lineData[1] + EMB_OFFSET_BOTTOM_ROW2;
    // 0: 8bit(--xx,xxxx,xx--,----); line start code
    *dst++ = (*(unsigned short int*)embBotBuffer) >> 6;
    dst++;
    // 1: 10bit(--xx,xxxx,xxxx,----)
    *dst++ = (*(unsigned short int*)(embBotBuffer + 2)) >> shift;
    *dst++ = (*(embBotBuffer + 3) >> shift) & 0x03;
    // 2~3: skip 2 bytes 00
    dst += 4;
    // 4~525: 20bit(--xx,xxxx,xxxx,----, --xx,xxxx,xxxx,----)
    for(i = R2_OFFSET_ROI3_PIXELS * 2; i < R2_OFFSET_DBLC * 2; i += stride) {
        *dst++ = (*(unsigned short int*)(embBotBuffer + i + 2)) >> shift;
        *dst++ = (((*(unsigned short int*)(embBotBuffer + i)) >> 2) & 0xFC) +
                 ((*(embBotBuffer + i + 3) >> shift) & 0x03);
        *dst++ = ((*(embBotBuffer + i + 1)) >> 2) & 0x0F;
        dst++;
    }
    // 526~1037: 2 bytes register (----,xxxx,xxxx,----)(----,xxxx,xxxx,----)
    for(i = R2_OFFSET_DBLC * 2; i < R2_OFFSET_ROI1_STATS * 2; i += stride) {
        *dst++ = (*(unsigned short int*)(embBotBuffer + i + 2)) >> shift;
        *dst++ = (*(unsigned short int*)(embBotBuffer + i)) >> shift;
        dst += 2;
    }
    // 1038~1073: 20bit(--xx,xxxx,xxxx,----, --xx,xxxx,xxxx,----)
    for(i = R2_OFFSET_ROI1_STATS * 2; i < R2_OFFSET_STATS_REG * 2; i += stride) {
        *dst++ = (*(unsigned short int*)(embBotBuffer + i + 2)) >> shift;
        *dst++ = (((*(unsigned short int*)(embBotBuffer + i)) >> 2) & 0xFC) +
                 ((*(embBotBuffer + i + 3) >> shift) & 0x03);
        *dst++ = ((*(embBotBuffer + i + 1)) >> 2) & 0x0F;
        dst++;
    }
    // 1074~1075: 2 bytes registers (----,xxxx,xxxx,----,----,xxxx,xxxx,----)
    *dst++ = (*(unsigned short int*)(embBotBuffer + R2_OFFSET_STATS_REG * 2 + 2)) >> shift;
    *dst++ = (*(unsigned short int*)(embBotBuffer + R2_OFFSET_STATS_REG * 2 )) >> shift;
    // 1076~row2 end: skip dummy bytes

    parsedInformation->bottom.size = lineLength[1] / stride;

    // bottom.baseRegAddress can not be used by app
    parsedInformation->bottom.baseRegAddress = 0x0;

done:
    return status;
}

static NvMediaStatus
GetSensorFrameId(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int lineCount,
    unsigned int *lineLength,
    unsigned char *lineData[],
    unsigned int *sensorFrameId)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    unsigned char *offset;
    unsigned char sensorVer;

    if((handle == NULL)|| (transaction == NULL) || (lineLength == NULL) ||
        (lineData == NULL) || (sensorFrameId == NULL) || (lineCount < 1u)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    sensorVer = ((_DriverHandle *)handle)->config_info.sensorVersion;

    if(((_DriverHandle *)handle)->config_info.sensorVersion == 0u) { // for reprocess
        status = getSenVerFromEmb((_DriverHandle *)handle, lineData[0], EMB_UNPARSED);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_WARN("AR0231 sensor version is unknown!\n");
        }
    }

    offset = lineData[0] + EMB_OFFSET_FRAME_ID(sensorVer) * 4;

    *sensorFrameId = ((*((unsigned int *)offset)) >> 6) & 0xff;

    LOG_DBG("%s: FrameId %x\n", __func__, *sensorFrameId);

    return status;
}

static NvMediaStatus
GetSensorProperties(
    NvMediaISCDriverHandle *handle,
    NvMediaISCSensorProperties *properties)
{
    _DriverHandle *driverHandle = (_DriverHandle *)handle;
    ConfigFormatAR0231 format;
    int i, k;
    float pixelRate, maxExp[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    unsigned int hts = ((_DriverHandle *)handle)->timingSettings->hts;
    unsigned int vts = ((_DriverHandle *)handle)->timingSettings->vts;
    const unsigned short int *fineIntegTime = ((_DriverHandle *)handle)->config_info.fineIntegTime;

    if(!handle || !properties) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    memset(properties, 0, sizeof(*properties));
    format = driverHandle->config_info.format;
    pixelRate = 1e9 / AR0231_PCLK;

    if(format < ISC_CONFIG_AR0231_NUM_FORMAT) {
        maxExp[2] = pixelRate * (hts * (MAX_AE_EXP_LINES_T3 - 1) +
                    fineIntegTime[2]);
        maxExp[1] = pixelRate * (hts * (MAX_AE_EXP_LINES_T2 - 1) +
                    fineIntegTime[1]) + maxExp[2];
        // maxExp[0] < VTS - 4; give couple of extra rows
        maxExp[0] = pixelRate * (hts * (vts - 9) +
                    fineIntegTime[0] + fineIntegTime[1] + fineIntegTime[2]);

        k = 0;
        for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            if(driverHandle->exposureModeMask & (1 << i)) {
                properties->exposure[k].valid = NVMEDIA_TRUE;

                switch (i) {
                    case 0:
                        properties->exposure[k].minExposureTime = pixelRate *
                                    (fineIntegTime[0] + fineIntegTime[1] + fineIntegTime[2]);
                        break;
                    case 1:
                        properties->exposure[k].minExposureTime = pixelRate *
                                    (fineIntegTime[1] + fineIntegTime[2]);
                        break;
                    case 2:
                        properties->exposure[k].minExposureTime = pixelRate * fineIntegTime[2];
                        break;
                    default:
                        break;
                }

                properties->exposure[k].maxExposureTime = maxExp[i];
                properties->exposure[k].minSensorGain = MIN_AGAIN_LCG * MIN_DGAIN;
                properties->exposure[k].maxSensorGain = driverHandle->maxGain;
                k++;
            }
        }

        properties->frameRate = driverHandle->config_info.frameRate;
        properties->maxHdrRatio = 256.0; //check sensor data sheet
        properties->channelGainRatio = HIGH_CGAIN; //same analog gain for all channels in this case

        LOG_DBG("%s: maxG %f. minG %f, expMaxT1 %f, expMaxT2 %f, expMaxT3 %f, expMinT1 %f, expMinT2 %f, \
            expMinT3 %f\n", __func__, properties->exposure[0].maxSensorGain, \
            MIN_AGAIN_LCG * MIN_DGAIN, maxExp[0], maxExp[1], maxExp[2], \
            properties->exposure[0].minExposureTime, properties->exposure[1].minExposureTime, \
            properties->exposure[2].minExposureTime);
    } else {
        return NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetModuleConfig(
    NvMediaISCDriverHandle *handle,
    NvMediaISCModuleConfig *cameraModuleConfig)
{
    if(!handle || !cameraModuleConfig) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    strncpy(cameraModuleConfig->cameraModuleCfgName,
           ((_DriverHandle *)handle)->moduleCfg.cameraModuleCfgName,
           strlen(((_DriverHandle *)handle)->moduleCfg.cameraModuleCfgName));

    cameraModuleConfig->cameraModuleConfigPass1 = ((_DriverHandle *)handle)->moduleCfg.cameraModuleConfigPass1;
    cameraModuleConfig->cameraModuleConfigPass2 = ((_DriverHandle *)handle)->moduleCfg.cameraModuleConfigPass2;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
SetRamContext(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int numExposureControls,
    NvMediaISCExposureControl *exposureControl)
{
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    unsigned char regPair[2];
    NvMediaBool flag3x;
    unsigned char i;

    if(!handle || !transaction || !exposureControl) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if((numExposureControls > 4) || (numExposureControls < 2)) {
        return NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    unsigned int aGainVal[numExposureControls];
    unsigned int dGainVal[numExposureControls];
    unsigned int cGainVal[numExposureControls];
    unsigned int exposureLines[numExposureControls][3];

    for(i = 0; i < numExposureControls; i++) {
        // conversion gain
        cGainVal[i] = GetCgainVal(&exposureControl[i], &flag3x);

        // analog gain for T1
        aGainVal[i] = GetAgainValT1(&exposureControl[i], cGainVal[i]);
        if(!flag3x) { // aGain is the same for all exp channels
            aGainVal[i] |= (aGainVal[i] << 4) | (aGainVal[i] << 8) | (aGainVal[i] << 12);
        }

        // globle digital gain is accurate for T1
        dGainVal[i] = GetDgainVal(&exposureControl[i], cGainVal[i], aGainVal[i]);

        // analog gain for T2...or more
        if(flag3x) {
            aGainVal[i] += GetAgainValNonT1(&exposureControl[i], cGainVal[i], dGainVal[i]);
        }

        EXPOSURE_PRINT("%s: gainT1 %f, gainT2 %f, gainT3 %f, cG %x, aG %x, dG %x\n", __func__, \
                 exposureControl[i].sensorGain[0].value, exposureControl[i].sensorGain[1].value,\
                 exposureControl[i].sensorGain[2].value, cGainVal[i], aGainVal[i], dGainVal[i]);

        // get integration time for 3-exp
        ret = ReCalculate3ExpTime(handle, &exposureControl[i], exposureLines[i]);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }

    // set n-1 contexts [7:4], high address[3:0] 0x3
    regPair[1] = ((numExposureControls - 1) << 4) | 0x03;
    regPair[0] = 0xF8;
    ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    // expT1
    regPair[1] = 0x09;
    regPair[0] = 0x08;  // set address 0x3012
    ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    for(i = 0; i < numExposureControls; i++) {
        regPair[1] = exposureLines[i][0] & 0xFF;
        regPair[0] = (exposureLines[i][0] >> 8) & 0xFF; // expT1
        ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }
    // expT2
    regPair[1] = 0x09;
    regPair[0] = 0x09;  // set address 0x3212
    ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    for(i = 0; i < numExposureControls; i++) {
        regPair[1] = exposureLines[i][1] & 0xFF;
        regPair[0] = (exposureLines[i][1] >> 8) & 0xFF; // expT2
        ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
       if(ret != NVMEDIA_STATUS_OK) {
           status = NVMEDIA_STATUS_ERROR;
        }
    }
    // expT3
    regPair[1] = 0x0B;
    regPair[0] = 0x09;  // set address 0x3216
    ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    for(i = 0; i < numExposureControls; i++) {
        regPair[1] = exposureLines[i][2] & 0xFF;
        regPair[0] = (exposureLines[i][2] >> 8) & 0xFF; // expT3
        ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
       if(ret != NVMEDIA_STATUS_OK) {
           status = NVMEDIA_STATUS_ERROR;
        }
    }
    // dGain
    regPair[1] = 0x84;
    regPair[0] = 0x09;  // set address 0x3308
    ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    for(i = 0; i < numExposureControls; i++) {
        regPair[1] = dGainVal[i] & 0xFF;
        regPair[0] = (dGainVal[i] >> 8) & 0xFF; // dGain
        ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }
    // aGain
    regPair[1] = 0xB3;
    regPair[0] = 0x09;  // set address 0x3366
    ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    for(i = 0; i < numExposureControls; i++) {
        regPair[1] = aGainVal[i] & 0xFF;
        regPair[0] = (aGainVal[i] >> 8) & 0xFF; // aGain
        ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }
    // cGain
    regPair[1] = 0xB1;
    regPair[0] = 0x09;  // set address 0x3362
    ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    for(i = 0; i < numExposureControls; i++) {
        regPair[1] = cGainVal[i] & 0xFF;
        regPair[0] = 0x00;                    // cGain
        ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
       if(ret != NVMEDIA_STATUS_OK) {
           status = NVMEDIA_STATUS_ERROR;
        }
    }
    // WB Gains, hard coded to 1.0
    regPair[1] = 0x2C;
    regPair[0] = 0x08;  // set address 0x3058
    if((WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair))
        != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    for(i = 0; i < numExposureControls; i++) {
        regPair[1] = 0x80;
        regPair[0] = 0x00;                    // bGain 1.0
        if((WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair))
           != NVMEDIA_STATUS_OK) {
           status = NVMEDIA_STATUS_ERROR;
        }
    }
    regPair[1] = 0x2D;
    regPair[0] = 0x08;  // set address 0x305A
    if((WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair))
        != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }
    for(i = 0; i < numExposureControls; i++) {
        regPair[1] = 0x80;
        regPair[0] = 0x00;                    // rGain 1.0
        if((WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair))
           != NVMEDIA_STATUS_OK) {
           status = NVMEDIA_STATUS_ERROR;
        }
    }
    // end of set context
    regPair[1] = 0x00;
    regPair[0] = 0x00;
    ret = WriteRegister(handle, transaction, REG_CTX_WR_DATA, 2, regPair);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }

    return status;
}

static NvMediaStatus
LoadNextContext(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int numExposureControls)
{
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    unsigned char regPair[2] = { 0x80, 0x00}; // load ram ctx 0

    // manually load context based on loadedContext and numExposureControls
    switch(((_DriverHandle *)handle)->loadedContext) {
        case CONTEXT_0:
            // switch to context 1
            regPair[0] = 0x80;
            regPair[1] = 0x01;
            ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
            ((_DriverHandle *)handle)->loadedContext = CONTEXT_1;
            EXPOSURE_PRINT("context 1 load\n");
            break;
        case CONTEXT_1:
            if(numExposureControls == 2) { // switch to context 0
                regPair[0] = 0x80;
                regPair[1] = 0x00;
                ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    status =  NVMEDIA_STATUS_ERROR;
                }
                ((_DriverHandle *)handle)->loadedContext = CONTEXT_0;
                EXPOSURE_PRINT("context 0 load\n");
            } else { // switch to context 2
                regPair[0] = 0x80;
                regPair[1] = 0x02;
                ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    status =  NVMEDIA_STATUS_ERROR;
                }
                ((_DriverHandle *)handle)->loadedContext = CONTEXT_2;
                EXPOSURE_PRINT("context 2 load\n");
            }
            break;
        case CONTEXT_2:
            if(numExposureControls == 3) { // switch to context 0
                regPair[0] = 0x80;
                regPair[1] = 0x00;
                ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    status =  NVMEDIA_STATUS_ERROR;
                }
                ((_DriverHandle *)handle)->loadedContext = CONTEXT_0;
                EXPOSURE_PRINT("context 0 load\n");
            } else { // switch to context 3
                regPair[0] = 0x80;
                regPair[1] = 0x03;
                ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    status =  NVMEDIA_STATUS_ERROR;
                }
                ((_DriverHandle *)handle)->loadedContext = CONTEXT_3;
                EXPOSURE_PRINT("context 3 load\n");
            }
            break;
        case CONTEXT_3:
        case CONTEXT_A:
        case CONTEXT_B:
            // switch to context 0
            regPair[0] = 0x80;
            regPair[1] = 0x00;
            ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
            ((_DriverHandle *)handle)->loadedContext = CONTEXT_0;
            EXPOSURE_PRINT("context 0 load\n");
            break;
        default:
            status = NVMEDIA_STATUS_NOT_SUPPORTED;
            break;
    }
    return status;
}

static NvMediaStatus
SetBracketedExposure(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int numExposureControls,
    NvMediaISCExposureControl *exposureControl)
{
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    unsigned char sensorFrameId;
    unsigned char regPair[2] = { 0x80, 0x00}; // load ram ctx 0
    unsigned int sensorVersion;
    NvMediaISCWBGainControl wbControl;

    if(!handle || !transaction || !exposureControl) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if((numExposureControls > 4) || (numExposureControls < 1)) {
        return NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    sensorFrameId = exposureControl->sensorFrameId;
    sensorVersion = ((_DriverHandle *)handle)->config_info.sensorVersion;

    // current only support 3-exp(T1,T2,T3) context switch
    // 1 context: context A
    if(numExposureControls == 1) {
        if(exposureControl->sensorGain[0].valid) {
            // switch to context A in case in Ram-based
            regPair[0] = 0x00;
            ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
            if(ret != NVMEDIA_STATUS_OK) {
                // 2nd i2c write: workaround for sensor i2c bus conflict in auto ctx mode
                ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    status = NVMEDIA_STATUS_ERROR;
                }
            }

            // V4: switch to context A in case in B
            if (sensorVersion <= AR0231_VER_4) {
                regPair[0] = 0x0B;
                regPair[1] = 0x02;
                ret = WriteRegister(handle, transaction, REG_DIGITAL_TEST, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    status = NVMEDIA_STATUS_ERROR;
                }
            }

            // prepare the wb gain, hard-coded contextA AWB to [1.5,1.0,1.0,1.5]
            wbControl.wbGain[0].value[0] = 1.5;
            wbControl.wbGain[0].value[1] = 1.0;
            wbControl.wbGain[0].value[2] = 1.0;
            wbControl.wbGain[0].value[3] = 1.5;
            wbControl.wbGain[0].valid = NVMEDIA_TRUE; // one valid is enough
            ret = SetWBGain(handle, transaction, &wbControl);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }

            EXPOSURE_PRINT("context A setup\n");
            ret = SetExposure(handle,
                              transaction,
                              exposureControl);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }

            ((_DriverHandle *)handle)->loadedContext = CONTEXT_A;
            EXPOSURE_PRINT("context A load\n");
        } else {
            ret = WriteRegister(handle, transaction, REG_STAT_FRAME_ID, 1, &sensorFrameId);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
            EXPOSURE_PRINT("%s: set sensor FrameId = %x\n", __func__, sensorFrameId);
        }
    } else if((numExposureControls == 2u) &&  // same 2 settings, WB gain = 1/1/1/1
               (exposureControl[0].exposureTime[0].value == exposureControl[1].exposureTime[0].value) &&
               (exposureControl[0].exposureTime[1].value == exposureControl[1].exposureTime[1].value) &&
               (exposureControl[0].exposureTime[2].value == exposureControl[1].exposureTime[2].value) &&
               (exposureControl[0].sensorGain[0].value == exposureControl[1].sensorGain[0].value) &&
               (exposureControl[0].sensorGain[1].value == exposureControl[1].sensorGain[1].value) &&
               (exposureControl[0].sensorGain[2].value == exposureControl[1].sensorGain[2].value)) {
        if(exposureControl->sensorGain[0].valid == NVMEDIA_TRUE) {
            // switch from case numExposureControls = 1
            // prepare the wb gain, hard-coded AWB to [1.0,1.0,1.0,1.0]
            wbControl.wbGain[0].value[0] = 1.0;
            wbControl.wbGain[0].value[1] = 1.0;
            wbControl.wbGain[0].value[2] = 1.0;
            wbControl.wbGain[0].value[3] = 1.0;
            wbControl.wbGain[0].valid = NVMEDIA_TRUE; // one valid is enough
            ret = SetWBGain(handle, transaction, &wbControl);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }

            EXPOSURE_PRINT("set exposure\n");
            ret = SetExposure(handle,
                              transaction,
                              exposureControl);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }

            ((_DriverHandle *)handle)->loadedContext = CONTEXT_A;
        } else {
            ret = WriteRegister(handle, transaction, REG_STAT_FRAME_ID, 1, &sensorFrameId);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
            EXPOSURE_PRINT("%s: set sensor FrameId = %x\n", __func__, sensorFrameId);
        }
    } else if((numExposureControls == 2) && (sensorVersion <= AR0231_VER_4)) {
        // switch context A & B
        if(exposureControl->sensorGain[0].valid) {
            // switch to context A in case in ram-based
            regPair[0] = 0x00;
            ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }

            // prepare the wb gain, hard-coded contextA AWB to [1.0,1.0,1.0,1.0]
            wbControl.wbGain[0].value[0] = 1.0;
            wbControl.wbGain[0].value[1] = 1.0;
            wbControl.wbGain[0].value[2] = 1.0;
            wbControl.wbGain[0].value[3] = 1.0;
            wbControl.wbGain[0].valid = NVMEDIA_TRUE;
            ret = SetWBGain(handle, transaction, &wbControl);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }

            // set context A
            EXPOSURE_PRINT("context A&B setup\n");
            ret = SetExposure(handle,
                              transaction,
                              exposureControl);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
            //set context B
            ret = SetExposureCb(handle,
                                transaction,
                                &exposureControl[1]);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }

            //make sure it will load context A
            ((_DriverHandle *)handle)->loadedContext = CONTEXT_B;
        } else {
            ret = WriteRegister(handle, transaction, REG_STAT_FRAME_ID, 1, &sensorFrameId);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
            EXPOSURE_PRINT("%s: set sensor FrameId = %x\n", __func__, sensorFrameId);
        }

        if(((_DriverHandle *)handle)->loadedContext == CONTEXT_B) {
            regPair[0] = 0x0B;

            ((_DriverHandle *)handle)->loadedContext = CONTEXT_A;
            EXPOSURE_PRINT("context A load\n");
        } else {
            regPair[0] = 0x2B;
            ((_DriverHandle *)handle)->loadedContext = CONTEXT_B;
            EXPOSURE_PRINT("context B load\n");
        }

        regPair[1] = 0x02;
        ret = WriteRegister(handle, transaction, REG_DIGITAL_TEST, 2, regPair);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    } else { // ver6, or ver4 with 3~4 contexts
        if(exposureControl[0].sensorGain[0].valid) {
            if(sensorVersion >= AR0231_VER_6) { // auto-cycle
                regPair[0] = 0x03;
                regPair[1] = 0x20;
                ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                    if(ret != NVMEDIA_STATUS_OK) {
                        status = NVMEDIA_STATUS_ERROR;
                    }
                }
            } else { // v4, to context A
                regPair[0] = 0x00;
                ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                    if(ret != NVMEDIA_STATUS_OK) {
                        status = NVMEDIA_STATUS_ERROR;
                    }
                    // make sure it will load context 0
                    ((_DriverHandle *)handle)->loadedContext = CONTEXT_B;
                }
            }

            ret = SetRamContext(handle,
                                transaction,
                                numExposureControls,
                                exposureControl);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
            EXPOSURE_PRINT("ram-based contexts setup\n");

            if(sensorVersion >= AR0231_VER_6) {
                // start auto context switch
                regPair[0] = 0x03;
                regPair[1] = 0xA0;
                ret = WriteRegister(handle, transaction, REG_CTX_CTL, 2, regPair);
                if(ret != NVMEDIA_STATUS_OK) {
                    status =  NVMEDIA_STATUS_ERROR;
                }
                ((_DriverHandle *)handle)->loadedContext = CONTEXT_0;
                EXPOSURE_PRINT("context auto switch start\n");
            }
        }

        ret = WriteRegister(handle, transaction, REG_STAT_FRAME_ID, 1, &sensorFrameId);
        if(ret != NVMEDIA_STATUS_OK) {
            ret = WriteRegister(handle, transaction, REG_STAT_FRAME_ID, 1, &sensorFrameId);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
        }
        EXPOSURE_PRINT("%s: set sensor FrameId = %x\n", __func__, sensorFrameId);

        if(sensorVersion == AR0231_VER_4) { // manual switch
            ret = LoadNextContext(handle,
                                  transaction,
                                  numExposureControls);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
        }
    }

    return status;
}

static NvMediaStatus
GetSensorAttr(
    NvMediaISCDriverHandle *handle,
    NvMediaISCSensorAttrType type,
    unsigned int size,
    void *attribute)
{
    _DriverHandle *driverHandle;
    const unsigned short int *fineIntegTime;
    float val[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {0};
    float tmpVal[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {0};
    float pixelRate;
    int i, k = 0;

    if(!handle || !attribute) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    driverHandle = (_DriverHandle *)handle;
    fineIntegTime = driverHandle->config_info.fineIntegTime;
    pixelRate = 1e9 / AR0231_PCLK;

    switch(type) {
        case NVMEDIA_ISC_SENSOR_ATTR_FUSE_ID:
        {
            if(size < SIZE_FUSE_ID) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            (void)memset(attribute, 0, size);
            (void)memcpy(attribute, ((_DriverHandle *)handle)->fuseId, SIZE_FUSE_ID);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_GAIN_MIN:
        {
            if(size != sizeof(val)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = MIN_AGAIN_LCG * MIN_DGAIN;
                    k++;
                }
            }

            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_GAIN_MAX:
        {
            if(size != sizeof(val)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = driverHandle->maxGain;
                    k++;
                }
            }

            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_MIN:
        {
            if(size != sizeof(val)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            tmpVal[2] = (pixelRate * fineIntegTime[2]) / 1e9;
            tmpVal[1] = (pixelRate * fineIntegTime[1]) / 1e9 + tmpVal[2];
            tmpVal[0] = (pixelRate * fineIntegTime[0]) / 1e9 + tmpVal[1];

            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = tmpVal[i];
                    k++;
                }
            }

            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_MAX:
        {
            if(size != sizeof(val)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            tmpVal[2] = (pixelRate * (driverHandle->timingSettings->hts *
                        (MAX_AE_EXP_LINES_T3 - 1) + fineIntegTime[2])) / 1e9;
            tmpVal[1] = (pixelRate * (driverHandle->timingSettings->hts *
                        (MAX_AE_EXP_LINES_T2 - 1) + fineIntegTime[1])) / 1e9 + tmpVal[2];
            tmpVal[0] = (pixelRate * (driverHandle->timingSettings->hts *
                        (driverHandle->timingSettings->vts - 9) + fineIntegTime[0]
                        + fineIntegTime[1] + fineIntegTime[2])) / 1e9;
            for (i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = tmpVal[i];
                    k++;
                }
            }

            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_HDR_MAX:
        {
            if (size != sizeof(unsigned int)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            *((unsigned int *) attribute) = 256;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_FRAME_RATE:
        {
            if (size != sizeof(float)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            *((float *) attribute) = driverHandle->config_info.frameRate;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_NUM_EXPOSURES:
        {
            if (size != sizeof(unsigned int)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    k++;
                }
            }

            *((unsigned int *) attribute) = k;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_GAIN_FACTOR:
        {
            if(size != sizeof(float)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            *((float *) attribute) = HIGH_CGAIN;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_FINE:
        {
            if(size != sizeof(val)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            tmpVal[2] = (pixelRate * fineIntegTime[2]) / 1e9;
            tmpVal[1] = (pixelRate * fineIntegTime[1] + tmpVal[2]) / 1e9;
            tmpVal[0] = (pixelRate * fineIntegTime[0] + tmpVal[1]) / 1e9;

            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    val[k] = tmpVal[i];
                    k++;
                }
            }

            memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_STEP:
        {
            double step[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {0};

            if(size != sizeof(step)) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if(driverHandle->exposureModeMask & (1 << i)) {
                    step[k] = (pixelRate * driverHandle->timingSettings->hts) / 1e9;
                    k++;
                }
            }

            memcpy(attribute, step, size);
            return NVMEDIA_STATUS_OK;
        }
        default:
            return NVMEDIA_STATUS_NOT_SUPPORTED;
    }
}

static NvMediaStatus
SetCompandingCurve(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    unsigned int numPoints,
    NvMediaPoint *kneePoints)
{
    NvMediaStatus status;
    unsigned int i, val;
    unsigned char ctlData = 0;
    unsigned char data[NUM_COMPANDING_KNEE_POINTS * 2];
    unsigned int input[NUM_COMPANDING_KNEE_POINTS] =
        {511, 1023, 2047, 4095, 8191, 16383, 32767,
         65535, 131071, 262143, 524287, 1048575};

    printf("[%s:%d] Failed\n", __func__, __LINE__);

    if (!handle || !transaction || !numPoints || !kneePoints) {
        printf("[%s:%d] Failed\n", __func__, __LINE__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if (numPoints != NUM_COMPANDING_KNEE_POINTS) {
        printf("[%s:%d] Failed\n", __func__, __LINE__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    for (i = 0; i < numPoints; i++) {
        if (((unsigned int) kneePoints[i].x) != input[i]) {
            printf("[%s:%d] Failed\n", __func__, __LINE__);
            return NVMEDIA_STATUS_BAD_PARAMETER;
        }
    }

    for (i = 0; i < numPoints; i++) {
        val = ((unsigned int) kneePoints[i].y) << 4;
        data[i * 2] = val & 0xFF;
        data[i * 2 + 1] = (val >> 8) & 0xFF;
    }

    status = WriteRegister(handle, transaction, REG_USER_KNEE_LUT0, sizeof(data), data);
    if (status != NVMEDIA_STATUS_OK) {
        printf("[%s:%d] Failed\n", __func__, __LINE__);
        return status;
    }

    // Disable legacy companding
    return WriteRegister(handle, transaction, REG_KNEE_LUT_CTL + 1, 1, &ctlData);
}

static NvMediaISCDeviceDriver deviceDriver = {
    .deviceName = "AR0231 Image Sensor",
    .deviceType = NVMEDIA_ISC_DEVICE_IMAGE_SENSOR,
    .regLength = 2,
    .dataLength = 2,
    .DriverCreate = DriverCreate,
    .DriverDestroy = DriverDestroy,
    .CheckPresence = CheckPresence,
    .EnableLink = EnableLink,
    .GetSensorFrameId = GetSensorFrameId,
    .ParseEmbeddedData = ParseEmbeddedData,
    .SetDefaults = SetDefaults,
    .SetDeviceConfig = SetDeviceConfig,
    .GetTemperature = GetTemperature,
    .ReadRegister = ReadRegister,
    .WriteRegister = WriteRegister,
    .SetExposure = SetExposure,
    .SetWBGain = SetWBGain,
    .WriteParameters = WriteParameters,
    .ReadParameters = ReadParameters,
    .DumpRegisters = DumpRegisters,
    .GetSensorProperties = GetSensorProperties,
    .GetSensorAttr = GetSensorAttr,
    .GetModuleConfig = GetModuleConfig,
    .SetBracketedExposure = SetBracketedExposure,
    .SetCompandingCurve = SetCompandingCurve
};

NvMediaISCDeviceDriver *
GetAR0231RccbDriver(void)
{
    return &deviceDriver;
}

NvMediaStatus
GetAR0231RccbConfigSet(
    char *resolution,
    char *inputFormat,
    int *configSet,
    unsigned int framerate)
{
    // set input mode setting for ar0231
    if(inputFormat && !strcasecmp(inputFormat, "raw12")) {
        if(resolution && !strcasecmp(resolution, "1920x1208")) {
            if(framerate == 20) {
                *configSet = ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208_20FPS;
            } else {
                *configSet = ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208;
            }
        } else if(resolution && !strcasecmp(resolution, "1920x1008")) {
            *configSet = ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1008;
        } else {
            LOG_DBG("%s: not support %s \n", __func__, inputFormat);
            return NVMEDIA_STATUS_ERROR;
        }
    } else {
        LOG_DBG("%s: not support %s \n", __func__, inputFormat);
        return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}
