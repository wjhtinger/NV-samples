/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved. All
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
#include "nvcommon.h"
#include "isc_ar0231.h"
#include "isc_ar0231_setting.h"

#define REG_ADDRESS_BYTES     2u
#define NUM_COMPANDING_KNEE_POINTS  12

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
#define REG_TEMPSENS0_CTL     0x30B4
#define REG_DLO_CTL           0x318E
#define REG_DATA_FORMAT       0x31AC
#define REG_FIT1              0x3014
#define REG_FIT2              0x321E
#define REG_FIT3              0x3222
#define REG_FUSE_ID           0x34C0

// top emb data offset
#define EMB_OFFSET_FRAME_CNT  3u
#define EMB_OFFSET_EXP_T1     31u
#define EMB_OFFSET_TEMPSENS0  139u
#define EMB_OFFSET_DGAIN_GR   433u
#define EMB_OFFSET_FRAME_ID   671u // offset of register 0x31d2 in top emb
#define EMB_OFFSET_CHIP_VER   704u // offset of register 0x31ff in top emb
#define EMB_OFFSET_DGAIN      ((AR0231_ACTIVE_H / 2u) + 75u)
#define EMB_OFFSET_CGAIN      ((AR0231_ACTIVE_H / 2u) + 166u)
#define EMB_OFFSET_AGAIN      ((AR0231_ACTIVE_H / 2u) + 169u)
// extra offset of some of V6 emb data
#define EMB_EXTRA_OFFSET_V6   4u

#define HIGH_CGAIN            3.0f     // cgain bit 0->1.0, 1->3.0
#define MIN_AGAIN_LCG         0.8f     // sensor vendor recommanded @ lcg
#define MIN_AGAIN_HCG         1.0f     // sensor vendor recommanded @ hcg
#define MIN_DGAIN             1.0f
#define MAX_DGAIN_VAL         0x7FF   // =3 + 511 / 512
#define ONE_DGAIN_VAL         0x200   // =1.0; format2.9 XX.YYYYYYYYY
#define ONE_COLOR_DGAIN_VAL   0x80    // =1.0; format4.7 XXXX.YYYYYYY
// exposure time limitation from sensor vendor
#define MAX_AE_EXP_LINES_T2   62u
#define MAX_AE_EXP_LINES_T3   5u
#define MAX_GAIN              ((HIGH_CGAIN * 8.0f) * (3.0f + (511.0f / 512.0f)))

#define EXPOSURE_PRINT(args...)
//#define EXPOSURE_PRINT(args...) printf(args)

static const NvF32 aGainTbl[] = {
    0.12500f, // 0 - 1/8x
    0.25000f, // 1 - 2/8x
    0.28571f, // 2 - 2/7x
    0.42857f, // 3 - 3/7x
    0.50000f, // 4 - 3/6x
    0.66667f, // 5 - 4/6x
    0.80000f, // 6 - 4/5x - min aGain in lcg
    1.00000f, // 7 - 5/5x - min aGain in hcg
    1.25000f, // 8 - 5/4x
    1.50000f, // 9 - 6/4x
    2.00000f, //10 - 6/3x
    2.33333f, //11 - 7/3x
    3.50000f, //12 - 7/2x
    4.00000f, //13 - 8/2x - ver3 max Again = 4.0
    8.00000f  //14 - 8/1x
};

#define NUM_A_GAIN ((sizeof(aGainTbl)) / (sizeof(aGainTbl[0])))

#define GET_BLOCK_LENGTH(x) x[0]
#define GET_BLOCK_DATA(x)   (&x[1])
#define SET_NEXT_BLOCK(x)   ((x) += ((x)[0] + 1u))

static NvU32 colorIndex[4] = {
    1u, // Gr - index 1; index order: R, Gr, Gb, B
    3u, // B  - index 3
    0u, // R  - index 0
    2u  // Gb - index 2
};

typedef struct {
    NvMediaISCSupportFunctions *funcs;
    const NvU8 *default_setting;
    const TimingSettingAR0231 *timingSettings;
    ConfigInfoAR0231 config_info;
    NvS32 tempratureData;
    NvU32 oscMhz;
    NvMediaISCModuleConfig moduleCfg;
    NvMediaBool grgbOneFlag;     // set flag to TRUE if GR and Gb gains = 1.0
    NvU32 exposureModeMask;
    NvU8 fuseId[SIZE_FUSE_ID];
    unsigned int configSetIdx;   // 0 - internal sync, 1 - external sync
} _DriverHandle;

static NvMediaStatus
WriteRegister(NvMediaISCDriverHandle *handle, NvMediaISCTransactionHandle *transaction,
        NvU32 registerNum, NvU32 dataLength, NvU8 *dataBuff);

static NvMediaStatus
ReadRegister(NvMediaISCDriverHandle *handle, NvMediaISCTransactionHandle *transaction,
        NvU32 registerNum, NvU32 dataLength, NvU8 *dataBuff);

static NvMediaStatus
DriverCreate(
    NvMediaISCDriverHandle **handle,
    NvMediaISCSupportFunctions *supportFunctions,
    void *clientContext)
{
    _DriverHandle *drvrHandle;
    const ContextAR0231 *ctx = NULL;
    NvU32 lenCfgName;
    ConfigResolutionAR0231 resSel = ISC_CONFIG_AR0231_1920X1208;

    if((handle == NULL) || (supportFunctions == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    drvrHandle = calloc(1, sizeof(_DriverHandle));
    if(drvrHandle == NULL) {
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    drvrHandle->funcs = supportFunctions;
    drvrHandle->default_setting = ar0231_raw12_default;
    drvrHandle->tempratureData = 0;

    drvrHandle->oscMhz = 27;
    drvrHandle->grgbOneFlag = NVMEDIA_TRUE;
    drvrHandle->config_info.maxGain = MAX_GAIN;
    drvrHandle->config_info.frameRate = 30;
    drvrHandle->config_info.sensorVersion = 0;

    (void)memset(drvrHandle->fuseId, 0, SIZE_FUSE_ID);

    *handle = (NvMediaISCDriverHandle *)drvrHandle;

    if(clientContext != NULL) {  // ok to be NULL, then use default values
        ctx = clientContext;
        drvrHandle->oscMhz = ctx->oscMHz;
        drvrHandle->config_info.maxGain = ctx->maxGain;
        drvrHandle->configSetIdx = ctx->configSetIdx;

        lenCfgName = strlen(ctx->moduleConfig.cameraModuleCfgName);
        if((sizeof(drvrHandle->moduleCfg.cameraModuleCfgName)) > lenCfgName) {
            (void)strncpy(drvrHandle->moduleCfg.cameraModuleCfgName,
                    ctx->moduleConfig.cameraModuleCfgName,
                    lenCfgName);
        } else {
            return NVMEDIA_STATUS_OUT_OF_MEMORY;
        }

        drvrHandle->moduleCfg.cameraModuleConfigPass1 = ctx->moduleConfig.cameraModuleConfigPass1;
        drvrHandle->moduleCfg.cameraModuleConfigPass2 = ctx->moduleConfig.cameraModuleConfigPass2;

        if(ctx->frameRate != 0u) {
            drvrHandle->config_info.frameRate = ctx->frameRate;

            switch(ctx->frameRate) {
                case 36: /* 36fps */
                    resSel = ISC_CONFIG_AR0231_1920X1008;
                    break;
                case 30: /* default fps is 30fps */
                default:
                    resSel = ISC_CONFIG_AR0231_1920X1208;
                    break;
            }
        }
    }

    drvrHandle->config_info.resolution = resSel;
    drvrHandle->timingSettings = &ar0231_timing[ctx->configSetIdx][resSel];
    drvrHandle->exposureModeMask = ISC_AR0231_ALL_EXPOSURE_MASK;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
DriverDestroy(
    NvMediaISCDriverHandle *handle)
{
    if(handle == NULL) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    free(handle);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetResolution(
    NvU32 enumeratedDeviceConfig,
    ConfigResolutionAR0231 *resolution)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208:
            *resolution = ISC_CONFIG_AR0231_1920X1208;
            break;
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1008:
            *resolution = ISC_CONFIG_AR0231_1920X1008;
            break;
        default:
            status = NVMEDIA_STATUS_BAD_PARAMETER;
            break;
    }

    return status;
}

static NvMediaStatus
CalculateExposureLineRate(
    NvF32 *lineRate,
    NvU32 hts)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if(lineRate == NULL) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    *lineRate = (NvF32)(AR0231_PCLK) / (NvF32)hts;

    LOG_DBG("%s: lineRate %f\n", __func__, *lineRate);

    return status;
}

static NvMediaStatus
EnableLink(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvU32 instanceNumber,
    NvMediaBool enable)
{
    return NVMEDIA_STATUS_NOT_SUPPORTED;
}

static NvMediaStatus
WriteArrayWithCommand(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    const NvU8 *arrayData)
{
    const NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if((handle == NULL) || (transaction == NULL) || (arrayData == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    funcs = ((_DriverHandle *)handle)->funcs;
    while(arrayData[0] != (NvU8)'e') {
        switch (arrayData[0]) {
            case 'w':
                status = funcs->Write(
                    transaction,
                    arrayData[1],
                    &arrayData[2]);
                arrayData += (arrayData[1] + 2u);
                break;
            case 'd':
                (void)usleep((arrayData[1] << 8) + arrayData[2]);
                arrayData += 3u;
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
    if((handle == NULL) || (transaction == NULL)) {
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
    NvU32 arrayByteLength,
    const NvU8 *arrayData)
{
    const NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status;

    if((handle == NULL) || (transaction == NULL) || (arrayData == NULL))
        return NVMEDIA_STATUS_BAD_PARAMETER;

    funcs = ((_DriverHandle *)handle)->funcs;

    while(arrayByteLength != 0u) {
        status = funcs->Write(
            transaction,                 // transaction
            GET_BLOCK_LENGTH(arrayData), // dataLength
            GET_BLOCK_DATA(arrayData));  // data
        if(status != NVMEDIA_STATUS_OK) {
            break;
        }

        arrayByteLength -= GET_BLOCK_LENGTH(arrayData) + 1u;
        SET_NEXT_BLOCK(arrayData);
    }
    return NVMEDIA_STATUS_OK;
}

static void
GetFITbySensorVer(
    NvMediaISCDriverHandle *handle)
{
    NvU32 hts;

    hts = ((_DriverHandle *)handle)->timingSettings->hts;
    // FIT - fine integration time is depend on sensor version and HTS
    if(((_DriverHandle *)handle)->config_info.sensorVersion == AR0231_VER_6) {
        ((_DriverHandle *)handle)->config_info.fineIntegTime[0] = AR0231_FINE_INTEG_TIME_V6(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[1] = AR0231_FINE_INTEG_TIME_V6(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[2] = AR0231_FINE_INTEG_TIME_V6(hts);
    } else if(((_DriverHandle *)handle)->config_info.sensorVersion == AR0231_VER_4) { // v4
        ((_DriverHandle *)handle)->config_info.fineIntegTime[0] = AR0231_FINE_INTEG_TIME_V4(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[1] = AR0231_FINE_INTEG_TIME_V4(hts);
        ((_DriverHandle *)handle)->config_info.fineIntegTime[2] = AR0231_FINE_INTEG_TIME_V4(hts);
    } else { // v3 or older
        ((_DriverHandle *)handle)->config_info.fineIntegTime[0] = AR0231_FINE_INTEG_TIME_V3_T1;
        ((_DriverHandle *)handle)->config_info.fineIntegTime[1] = AR0231_FINE_INTEG_TIME_V3_T2;
        ((_DriverHandle *)handle)->config_info.fineIntegTime[2] = AR0231_FINE_INTEG_TIME_V3_T3;
    }
}

static NvMediaStatus
SetFIT(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    NvU8 fit[2];

    if((handle == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    GetFITbySensorVer(handle);

    // FIT T1
    fit[0] = (((_DriverHandle *)handle)->config_info.fineIntegTime[0] >> 8) & (NvU32)0xFF;
    fit[1] = ((_DriverHandle *)handle)->config_info.fineIntegTime[0] & (NvU32)0xFF;
    ret = WriteRegister(handle, transaction, REG_FIT1, 2, fit);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }

    // FIT T2
    fit[0] = (((_DriverHandle *)handle)->config_info.fineIntegTime[1] >> 8) & (NvU32)0xFF;
    fit[1] = ((_DriverHandle *)handle)->config_info.fineIntegTime[1] & (NvU32)0xFF;
    ret = WriteRegister(handle, transaction, REG_FIT2, 2, fit);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }

    // FIT T3
    fit[0] = (((_DriverHandle *)handle)->config_info.fineIntegTime[2] >> 8) & (NvU32)0xFF;
    fit[1] = ((_DriverHandle *)handle)->config_info.fineIntegTime[2] & (NvU32)0xFF;
    ret = WriteRegister(handle, transaction, REG_FIT3, 2, fit);
    if(ret != NVMEDIA_STATUS_OK) {
        status = NVMEDIA_STATUS_ERROR;
    }

    return status;
}

static NvMediaStatus
SetDeviceConfig(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        NvU32 enumeratedDeviceConfig)
{
    NvMediaStatus status;
    NvU8 ctlData = 0;
    ConfigResolutionAR0231 resolution = ISC_CONFIG_AR0231_1920X1208;

    if((handle == NULL) || (transaction == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    switch(enumeratedDeviceConfig) {
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208:
        case ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1008:
            status = GetResolution(enumeratedDeviceConfig, &resolution);
            if(status != NVMEDIA_STATUS_OK) {
                return status;
            }

            ((_DriverHandle *)handle)->config_info.enumeratedDeviceConfig =
                     enumeratedDeviceConfig;
            ((_DriverHandle *)handle)->config_info.resolution = resolution;
            ((_DriverHandle *)handle)->timingSettings = &ar0231_timing[((_DriverHandle *)handle)->configSetIdx][resolution];

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
            ctlData = 0x11; // bit 4&0: enable temp sensor
            status = WriteRegister(
                handle,
                transaction,
                REG_TEMPSENS0_CTL + 1,  //low byte
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
    if((handle == NULL) || (transaction == NULL) || (configInfo == NULL)) {
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
    if((handle == NULL) || (transaction == NULL) || (configInfo == NULL)) {
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
        NvU8 *lut)
{
    NvMediaStatus status;

    if((handle == NULL) || (transaction == NULL) || (lut == NULL)) {
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
    NvU8 whichExp)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU8 regVal;

    // bypass only one channel
    regVal = (NvU8)0x12 + (NvU32)((whichExp << 2u) + ((whichExp == 2u) ? 4u : 0u));
    status = WriteRegister(handle, transaction, REG_DLO_CTL, 1, &regVal);
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }
    // 12bit raw
    regVal = 0x0C;
    status = WriteRegister(handle, transaction, REG_DATA_FORMAT, 1, &regVal);
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    return status;
}

static NvMediaStatus
setHdr(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU8 regVal;

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
        NvU32 exposureModeMask)
{
    _DriverHandle *drvrHandle = (_DriverHandle *)handle;
    NvU8 expIdx = 0;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if ((handle == NULL) || (transaction == NULL) || (exposureModeMask == 0u))
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if (exposureModeMask == drvrHandle->exposureModeMask) {
        // No change, just return
        return NVMEDIA_STATUS_OK;
    }

    if (exposureModeMask == ISC_AR0231_ALL_EXPOSURE_MASK) {
        // full exposure mode
        status = setHdr(handle, transaction);
    } else {
        switch (exposureModeMask) {
            case ISC_AR0231_T1_EXPOSURE_MASK:
                expIdx = 0u;
                break;
            case ISC_AR0231_T2_EXPOSURE_MASK:
                expIdx = 1u;
                break;
            case ISC_AR0231_T3_EXPOSURE_MASK:
                expIdx = 2u;
                break;
            default:
                return NVMEDIA_STATUS_BAD_PARAMETER;
        }

        status = setNonHdr(handle, transaction, expIdx);
    }

    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }

    drvrHandle->exposureModeMask = exposureModeMask;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
WriteParameters(
        NvMediaISCDriverHandle *handle,
        NvMediaISCTransactionHandle *transaction,
        NvU32 parameterType,
        NvU32 parameterSize,
        void *parameter)
{
    NvMediaStatus status;
    const WriteReadParametersParamAR0231 *param;

    if((parameter == NULL) || (handle == NULL) || (transaction == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    param = (WriteReadParametersParamAR0231 *)parameter;

    switch(parameterType) {
        case ISC_WRITE_PARAM_CMD_AR0231_CONFIG_INFO:
            if(parameterSize != (sizeof(param->configInfo))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }
            status = SetConfigInfo(
                handle,
                transaction,
                param->configInfo);
            break;
        case ISC_WRITE_PARAM_CMD_AR0231_SET_KNEE_LUT:
            if(parameterSize != (sizeof(param->KneeLutReg))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }
            status = SetKneeLut(
                handle,
                transaction,
                param->KneeLutReg);
            break;
        case ISC_WRITE_PARAM_CMD_AR0231_EXPO_MODE:
            if(parameterSize != (sizeof(param->exposureModeMask)))
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
        NvU32 parameterType,
        NvU32 parameterSize,
        void *parameter)
{
    const WriteReadParametersParamAR0231 *param;
    NvMediaStatus status;

    if((parameter == NULL) || (handle == NULL) || (transaction == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    param = (WriteReadParametersParamAR0231 *)parameter;
    if(parameterSize != (sizeof(param->configInfo))) {
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
    NvU32 registerNum,
    NvU32 dataLength,
    NvU8 *dataBuff)
{
    const NvMediaISCSupportFunctions *funcs;
    NvU8 registerData[REG_ADDRESS_BYTES];
    NvMediaStatus status;

    if((handle == NULL) || (transaction == NULL) || (dataBuff == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    funcs = ((_DriverHandle *)handle)->funcs;

    registerData[0] = registerNum >> 8;
    registerData[1] = (registerNum & (NvU32)0xFF);
    status = funcs->Read(
        transaction,
        REG_ADDRESS_BYTES, // regLength
        registerData,      // regData
        dataLength,        // dataLength
        dataBuff);         // data

    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: sensor read failed: 0x%x, length %d\n", __func__, registerNum, dataLength);
    }
    return status;
}

static NvMediaStatus
WriteRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvU32 registerNum,
    NvU32 dataLength,
    NvU8 *dataBuff)
{
    const NvMediaISCSupportFunctions *funcs;
    NvU8 data[REG_ADDRESS_BYTES + REG_WRITE_BUFFER];
    NvMediaStatus status;

    if((handle == NULL) || (transaction  == NULL) || (dataBuff == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    funcs = ((_DriverHandle *)handle)->funcs;

    data[0] = registerNum >> 8;
    data[1] = registerNum & (NvU32)0xFF;
    (void)memcpy(&data[2], dataBuff, dataLength);

    status = funcs->Write(
        transaction,
        dataLength + (NvU32)REG_ADDRESS_BYTES,    // dataLength
        data);                             // data

    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: sensor write failed: 0x%x, length %d\n", __func__, registerNum, dataLength);
    }
    return status;
}

static NvMediaStatus
GetTemperature(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvF32 *temperature)
{
    _DriverHandle *drvrHandle;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU8 buff[REG_DATA_BYTES] = {0u};
    NvU32 regAdd;

    if((handle == NULL) || (transaction == NULL) || (temperature == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    drvrHandle = (_DriverHandle *)handle;
    regAdd = (NvU32)REG_TEMPSENS0_DATA;

    if(drvrHandle->tempratureData == 0) {// in case parseEmbeddedData() hasn't been called yet
        status = ReadRegister(handle,
                              transaction,
                              regAdd,
                              REG_DATA_BYTES,
                              buff);
        if(status != NVMEDIA_STATUS_OK) {
            return status;
        }

        drvrHandle->tempratureData = (buff[0] << 8) + buff[1];
    }

    // temp sensor 0; slope is hardcoded here, values may change after temp calibration
    *temperature = (0.725f * (NvF32)drvrHandle->tempratureData) - 271.25f;

    LOG_DBG("%s: sensor temp %f\n", __func__, *temperature);

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
CheckPresence(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    NvMediaStatus status;
    NvU8 readBuff[2] = {0u};

    if((handle == NULL) || (transaction == NULL)) {
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

    if((readBuff[0] != (((NvU32)(AR0231_CHIP_ID) >> 8) & (NvU32)0xff)) ||
            (readBuff[1] != (((NvU32)(AR0231_CHIP_ID)) & (NvU32)0xff))) {
        return NVMEDIA_STATUS_ERROR;
    }

    status = ReadRegister(handle,
                          transaction,
                          REG_CHIP_VER + 1, // low byte only
                          1,
                          readBuff);
    if(status != NVMEDIA_STATUS_OK) {
        return status;
    }

    if((readBuff[0] & 0x0F) >= 5u) { // sensor version, [3:0]
        ((_DriverHandle *)handle)->config_info.sensorVersion = AR0231_VER_6;
    } else if((readBuff[0] & 0x0F) == 4u) {
        ((_DriverHandle *)handle)->config_info.sensorVersion = AR0231_VER_4;
        ((_DriverHandle *)handle)->default_setting = ar0231_raw12_default_v4;
    } else { // v3 or older
        ((_DriverHandle *)handle)->config_info.sensorVersion = AR0231_VER_3_OR_OLDER;
        ((_DriverHandle *)handle)->default_setting = ar0231_raw12_default_v3;
        if(((_DriverHandle *)handle)->config_info.maxGain > ((NvF32)MAX_GAIN / 2.0f)) {
            ((_DriverHandle *)handle)->config_info.maxGain = (NvF32)MAX_GAIN / 2.0f;
        }
    }
    return status;
}

static NvMediaStatus
DumpRegisters(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction)
{
    const NvMediaISCSupportFunctions *funcs;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU8 data[REG_DATA_BYTES] = {0, 0};
    NvU8 addr[REG_ADDRESS_BYTES] = {0, 0};
    const NvU8 *arrayData;

   if((handle == NULL) || (transaction == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
   }

    funcs = ((_DriverHandle *)handle)->funcs;
    arrayData = ((_DriverHandle *)handle)->default_setting;

    while(arrayData[0] != (NvU8)'e') {
        if(arrayData[0] == (NvU8)'w') {
            addr[0] = arrayData[2];
            addr[1] = arrayData[3];
            status = funcs->Read(
                transaction,
                REG_ADDRESS_BYTES,     // regLength
                addr,                  // regData
                REG_DATA_BYTES,        // dataLength
                data);                 // dataBuff

            LOG_DBG("20 %.2X%.2X %.2x %.2x\n", (NvU32)arrayData[2],
                (NvU32)arrayData[3], data[0], data[1]);
            arrayData += (arrayData[1] + 2u);
        } else {
            arrayData += 3;  // for 'd'
        }
    }

    return status;
}

static NvU32
GetAgainValT1(
    NvMediaISCExposureControl *exposureControl,
    NvU32 cgain)
{
    NvU32 i, minAgVal;
    NvF32 tmp;

    minAgVal = ((cgain & (NvU32)0x1) == 1u) ? 7u : 6u;

    tmp = exposureControl->sensorGain[0].value / (((cgain & ((NvU32)0x1)) == ((NvU32)0x1)) ? HIGH_CGAIN : 1.0f);
    if(tmp < MIN_AGAIN_LCG) {
        tmp = ((cgain & ((NvU32)0x1)) == ((NvU32)0x1)) ? MIN_AGAIN_HCG : MIN_AGAIN_LCG;
    }

    for(i = NUM_A_GAIN - 1u; i >= minAgVal; i--) {
        if(tmp >= aGainTbl[i]) {
            break;
        }
    }

    return i;
}

static NvU32
GetAgainValNonT1(
    NvMediaISCExposureControl *exposureControl,
    NvU32 cgain,
    NvU32 dGain)
{
    NvU32 i, j, k = 0, minAgVal;
    NvF32 tmp;

    for(i = 1; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        minAgVal = (((cgain >> i) & (NvU32)0x1) == 1u) ? 7u : 6u;

        tmp = (((exposureControl->sensorGain[i].value /
                ((((cgain >> i) & (NvU32)0x1) == (NvU32)0x1) ? HIGH_CGAIN : 1.0f)) /
                (NvF32)dGain) *
                (NvF32)ONE_DGAIN_VAL);
        if(tmp < MIN_AGAIN_LCG) {
            tmp = ((cgain & (NvU32)0x1) == (NvU32)0x1) ? MIN_AGAIN_HCG : MIN_AGAIN_LCG;
        }

        for(j = NUM_A_GAIN - 1u; j >= minAgVal; j--) {
            if(tmp >= aGainTbl[j]) {
                break;
            }
        }

        if(((j < ((NvU32)NUM_A_GAIN - 1u))) && ((aGainTbl[j + 1u] - tmp) < (tmp - aGainTbl[j]))) {
            j++;
        }

        k += j << (i * 4u);
    }

    return k;
}

static NvU32
GetDgainVal(
    NvMediaISCExposureControl *exposureControl,
    NvU32 cgain,
    NvU32 again)
{
    NvU32 i;

    //accrate for T1, T2/T3 use the same digital gain
    i = (NvU32)((((exposureControl->sensorGain[0].value /
            ((((cgain & (NvU32)0x1)) == (NvU32)0x1) ? HIGH_CGAIN : 1.0f)) /
            aGainTbl[again & (NvU32)0xf]) * (NvF32)ONE_DGAIN_VAL) + 0.5f);

    if(i > (NvU32)MAX_DGAIN_VAL) {
        return MAX_DGAIN_VAL;
    }

    if(i < (NvU32)ONE_DGAIN_VAL) {
        return ONE_DGAIN_VAL;
    }

    return i;
}

static NvU32
GetCgainVal(
    NvMediaISCExposureControl *exposureControl,
    NvMediaBool *flag3x)
{
    NvU8 i, cGain = 0;
    NvF32 mGain = exposureControl->sensorGain[0].value;

    *flag3x = NVMEDIA_FALSE;

    if((mGain == exposureControl->sensorGain[1].value) &&
        (mGain == exposureControl->sensorGain[2].value)) {
        if(mGain >= (HIGH_CGAIN * MIN_AGAIN_HCG * MIN_DGAIN)) {
            cGain = 0xf;
        }
        return cGain;
    }

    for(i = 1; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if(mGain > exposureControl->sensorGain[i].value) {
            mGain = exposureControl->sensorGain[i].value;
        }
    }

    for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if(exposureControl->sensorGain[i].value != mGain) {
            if((NvU32)((exposureControl->sensorGain[i].value * 10.0f) + 0.5f) ==
                (NvU32)(((mGain * 10.0f) * (NvF32)HIGH_CGAIN) + 0.5f)) {
                cGain |= 1u << i;
            } else {
                break;
            }
        }
    }

    if(i != (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX) {
        *flag3x = NVMEDIA_TRUE;
        cGain = 0;
        for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
             if(exposureControl->sensorGain[i].value >= (HIGH_CGAIN * MIN_AGAIN_HCG * MIN_DGAIN)) {
                 cGain |= 1u << i;
             }
        }
    }

    return cGain;
}

static NvMediaStatus
ReCalculate3ExpTime(
    NvMediaISCDriverHandle *handle,
    NvMediaISCExposureControl *exposureControl,
    NvU32 *exposureLines)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU32 hts;
    const NvU16 *fineIntegTime;

    if((handle == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    hts = ((_DriverHandle *)handle)->timingSettings->hts;
    fineIntegTime = ((_DriverHandle *)handle)->config_info.fineIntegTime;

    // re-calculate expTime for 3-exp
    // e3 = (cit3 - 1) * llpck + fit3
    // e2 = (cit2 + cit3 - 2) * llpck + fit2 + fit3
    // e1 = (cit1 + cit2 + cit3 - 3) * llpck + fit1 + fit2 + fit3
    exposureLines[2] = (NvU32)((exposureControl->exposureTime[2].value *
                       ((_DriverHandle *)handle)->config_info.exposureLineRate) + 1.0f -
                       ((NvF32)fineIntegTime[2] / (NvF32)hts) + 0.5f);
    if(exposureLines[2] < 1u) {
         exposureLines[2] = 1;
    } else if (exposureLines[2] > (NvU32)MAX_AE_EXP_LINES_T3) {
         exposureLines[2] = MAX_AE_EXP_LINES_T3;
    }

    exposureLines[1] = (NvU32)((exposureControl->exposureTime[1].value *
                       ((_DriverHandle *)handle)->config_info.exposureLineRate) -
                       (NvF32)exposureLines[2] + 2.0f - ((NvF32)(fineIntegTime[1] +
                       fineIntegTime[2]) / (NvF32)hts) + 0.5f);
    if(exposureLines[1] < 1u) {
         exposureLines[1] = 1;
    } else if (exposureLines[1] > (NvU32)MAX_AE_EXP_LINES_T2) {
         exposureLines[1] = MAX_AE_EXP_LINES_T2;
    }

    exposureLines[0] = (NvU32)((exposureControl->exposureTime[0].value *
                       ((_DriverHandle *)handle)->config_info.exposureLineRate) -
                       (NvF32)exposureLines[1] - (NvF32)exposureLines[2] + 3.0f - ((NvF32)(fineIntegTime[0] +
                       fineIntegTime[1] + fineIntegTime[2]) / (NvF32)hts) + 0.5f);
    if(exposureLines[0] < 1u) {
        exposureLines[0] = 1;
    }

    EXPOSURE_PRINT("%s: ExpT1: %f Lines: %d; ExpT2: %f Lines: %d; ExpT3: %f Lines: %d\n",
                   __func__, exposureControl->exposureTime[0].value,
                   (NvU32)(exposureLines[0] + exposureLines[1] + exposureLines[2] - 3u +
                   (NvF32)(fineIntegTime[0] + fineIntegTime[1] + fineIntegTime[2]) /
                   hts), exposureControl->exposureTime[1].value,
                   (NvU32)(exposureLines[1] + exposureLines[2] - 2u +
                   (NvF32)(fineIntegTime[1] + fineIntegTime[2]) / hts),
                   exposureControl->exposureTime[2].value,
                   (NvU32)(exposureLines[2] - 1u + (NvF32)fineIntegTime[2] /
                   hts));
    return status;
}

static NvMediaStatus
WriteExpRegister(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvU32 exposureLines,
    NvU32 whichExp)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU8 regVal[2];
    NvU32 regAdd;
    NvU32 dataLength;

    if(whichExp == 0u) { // T1 exp has 2 bytes
        regAdd = REG_INTEG_TIME_T1;
        regVal[0] = (exposureLines >> 8) & (NvU32)0xFF;
        regVal[1] = exposureLines & (NvU32)0xFF;
        dataLength = 2;
    } else { // T2, T3 exp has 1 byte
        regAdd = ((NvU32)REG_INTEG_TIME_T2 + ((whichExp - 1u) * 4u)) + 1u;
        regVal[0] = exposureLines & (NvU32)0xFF;
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
    const _DriverHandle *drvrHandle;
    NvMediaStatus ret, status = NVMEDIA_STATUS_OK;
    NvU8 regVal[2];
    NvU32 aGainVal = 0, dGainVal = 0, cGainVal = 0;
    NvU32 i, exposureLines[3] = {0,0,0};
    NvMediaBool flag3x;
    NvU32 hts;
    const NvU16 *fineIntegTime;

    if((handle == NULL) || (transaction == NULL) || (exposureControl == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    drvrHandle = (_DriverHandle *)handle;

    hts = drvrHandle->timingSettings->hts;
    fineIntegTime = drvrHandle->config_info.fineIntegTime;
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

        regVal[0] = cGainVal & (NvU32)0x0F;
        ret = WriteRegister(handle, transaction, REG_CGAIN + 1, 1u, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
        regVal[0] = (aGainVal >> 8) & (NvU32)0xFF;
        regVal[1] = aGainVal & (NvU32)0xFF;
        ret = WriteRegister(handle, transaction, REG_AGAIN, 2, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
        regVal[0] = (dGainVal >> 8) & (NvU32)0xFF;
        regVal[1] = dGainVal & (NvU32)0xFF;
        ret = WriteRegister(handle, transaction, REG_DGAIN, 2, regVal);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }
    }

    if(drvrHandle->exposureModeMask == ISC_AR0231_ALL_EXPOSURE_MASK) { // 3-exp
        ret = ReCalculate3ExpTime(handle, exposureControl, exposureLines);
        if(ret != NVMEDIA_STATUS_OK) {
            status = NVMEDIA_STATUS_ERROR;
        }

        for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            ret = WriteExpRegister(handle, transaction, exposureLines[i], i);
            if(ret != NVMEDIA_STATUS_OK) {
                status = NVMEDIA_STATUS_ERROR;
            }
        }
    } else { // non-HDR 1 exp only
        for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            if(exposureControl->exposureTime[i].valid == NVMEDIA_TRUE) { // 1st valid form T1, T2 or T3
                exposureLines[i] = (NvU32)((exposureControl->exposureTime[i].value *
                                    drvrHandle->config_info.exposureLineRate) + 1.0f -
                                    ((NvF32)fineIntegTime[i] / (NvF32)hts) + 0.5f);

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
    const _DriverHandle *drvrHandle = (_DriverHandle *)handle;
    NvMediaISCExposureControl exposureControlTmp;
    NvU32 i, k;

    if ((handle == NULL) || (transaction == NULL) || (exposureControl == NULL))
        return NVMEDIA_STATUS_BAD_PARAMETER;

    /* For all exposure mode and long exposure mode */
    if (drvrHandle->exposureModeMask == (NvU32)(ISC_AR0231_ALL_EXPOSURE_MASK)) {
        return SetExposureWork(handle, transaction, exposureControl);
    }

    /* Re-map the settings for other modes combinations */
    (void)memset(&exposureControlTmp, 0, sizeof(exposureControlTmp));
    exposureControlTmp.sensorFrameId = exposureControl->sensorFrameId;
    exposureControlTmp.hdrRatio = exposureControl->hdrRatio;
    exposureControlTmp.sensorMode = exposureControl->sensorMode;

    k = 0;
    for (i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if ((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
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
getSenVerFromEmb(
    _DriverHandle *driverHandle,
    const NvU8 *data,
    EmbeddedDataType embType)
{
   NvMediaStatus status = NVMEDIA_STATUS_OK;
   NvU8 emb[3];

    if(embType == EMB_PARSED) {
        emb[0] = data[EMB_OFFSET_CHIP_VER - EMB_EXTRA_OFFSET_V6];
        emb[1] = data[EMB_OFFSET_CHIP_VER];
        emb[2] = data[EMB_OFFSET_CHIP_VER + EMB_EXTRA_OFFSET_V6];
    } else if(embType == EMB_UNPARSED) {
        emb[0] = (NvU8)(*((NvU32 *)(data + EMB_OFFSET_CHIP_VER * 4 - EMB_EXTRA_OFFSET_V6 * 4)) >> 6);
        emb[1] = (NvU8)(*((NvU32 *)(data + EMB_OFFSET_CHIP_VER * 4 )) >> 6);
        emb[2] = (NvU8)(*((NvU32 *)(data + EMB_OFFSET_CHIP_VER * 4 + EMB_EXTRA_OFFSET_V6 * 4)) >> 6);
    }

    if((emb[1] == 0xFC) && (( emb[2] & 0x0F) >= 5u)) {
        driverHandle->config_info.sensorVersion = AR0231_VER_6;
    } else if((emb[0] == 0xFC) && ((emb[1] & 0x0F) == 4u)) {
        driverHandle->config_info.sensorVersion = AR0231_VER_4;
    } else if(emb[0] == 0xFC) {
        driverHandle->config_info.sensorVersion = AR0231_VER_3_OR_OLDER;
    } else {
        status = NVMEDIA_STATUS_NOT_SUPPORTED;
    }

    return status;
}

static NvMediaStatus
ParseExposure(
    _DriverHandle *drvrHandle,
    NvMediaISCEmbeddedData *parsedInformation,
    NvF32 lineRate)
{
    const NvU8 *data;
    NvU32 i, k, agVal, numValidExposures = 0;
    NvU8 cgVal, regPair[2];
    NvF32 dgain, exposureMidpoint = 0.0f;
    NvF32 expT[3];
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU32 hts;
    const NvU16 *fineIntegTime;

    if((drvrHandle == NULL) || (parsedInformation == NULL) || (lineRate == 0.0f)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    hts = drvrHandle->timingSettings->hts;
    fineIntegTime = drvrHandle->config_info.fineIntegTime;

    data = (NvU8*)(parsedInformation->top.data);
    regPair[0] = data[EMB_OFFSET_DGAIN];
    regPair[1] = data[EMB_OFFSET_DGAIN + 1u];
    dgain = (NvF32)((regPair[0] << 8) + regPair[1]) / (NvF32)ONE_DGAIN_VAL;

    regPair[0] = data[EMB_OFFSET_AGAIN];
    regPair[1] = data[EMB_OFFSET_AGAIN + 1u];
    agVal = (regPair[0] << 8) + regPair[1];

    cgVal = data[EMB_OFFSET_CGAIN];

    for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        (void)memset(&parsedInformation->exposure[i], 0,
               sizeof(parsedInformation->exposure[i]));
    }

    // for re-process
    if(drvrHandle->config_info.sensorVersion == 0u) {
        status = getSenVerFromEmb(drvrHandle, data, EMB_PARSED);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_WARN("AR0231 sensor version is unknown!\n");
        }

        GetFITbySensorVer(drvrHandle);
    }

    k = 0;
    for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if ((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
            parsedInformation->exposure[k].digitalGain = dgain;

            parsedInformation->exposure[k].analogGain = aGainTbl[(agVal >> (4u * i)) & (NvU32)0xF];
            parsedInformation->exposure[k].conversionGain = (((cgVal >> i) & (NvU32)0x01) == 0u) ? 1.0 : HIGH_CGAIN;

            regPair[0] = data[(NvU32)EMB_OFFSET_EXP_T1 + (i * 2u)];
            regPair[1] = data[(NvU32)EMB_OFFSET_EXP_T1 + (i * 2u) + 1u];
            expT[i] = ((((NvF32)(regPair[0] << 8) + (NvF32)regPair[1]) - 1.0f +
                ((NvF32)fineIntegTime[i] / (NvF32)hts)) / lineRate);

            parsedInformation->exposure[k].exposureTime = expT[i];
            k++;
        }
    }

    if(drvrHandle->exposureModeMask == ISC_AR0231_ALL_EXPOSURE_MASK) {
        // HDR
        // e2 += e3
        // e1 += e2 + e3
        parsedInformation->exposure[1].exposureTime += expT[2];
        parsedInformation->exposure[0].exposureTime += (expT[1] + expT[2]);
    }

    for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        EXPOSURE_PRINT("%s: T%i, cGain = %f, aGain = %f, expTime = %f\n", __func__, i+1u, \
            parsedInformation->exposure[i].conversionGain, parsedInformation->exposure[i].analogGain, \
            parsedInformation->exposure[i].exposureTime);

        if(parsedInformation->exposure[i].exposureTime != 0.0f) {
            exposureMidpoint += 0.5f * parsedInformation->exposure[i].exposureTime;
            numValidExposures++;
        }
    }

    EXPOSURE_PRINT("%s: global dGain = %f\n", __func__, parsedInformation->exposure[0].digitalGain);

    if(numValidExposures != 0u) {
        // average frame middle points
        exposureMidpoint /= (NvF32)numValidExposures;
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
    NvU16 gain;
    NvU8 regVal[8];
    NvU8 *dataAdd;
    NvU32 i, index, regAdd, dataLength;
    NvU32 firstValid = 0xff; // overwrite with i when 1st wbGain[i].valid is TRUE

    if((handle == NULL) || (transaction == NULL) || (wbControl == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    // check 1st valid
    for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        if((wbControl->wbGain[i].valid) == NVMEDIA_TRUE) {
            firstValid = i;
            break;
        }
    }

    // global WB gains
    if(firstValid != (NvU32)0xff) {
        for(i = 0; i < 4u; i++) {  //Gr, B, R, Gb
            index = colorIndex[i]; //R, Gr, Gb, B
            gain = (NvU16) ((wbControl->wbGain[firstValid].value[index] *
                    (NvF32)ONE_COLOR_DGAIN_VAL) + 0.5f);
            if(gain != 0u) {
                regVal[i * 2u] = (gain >> 8) & (NvU32)0xFF;
                regVal[(i * 2u) + 1u] = gain & (NvU32)0xFF;
                LOG_DBG("%s: SetWBGain %d: %x\n", __func__, i, gain);
            }
        }
        // in most cases, Gr gain and Gb gain are 1.0
        if((((_DriverHandle *)handle)->grgbOneFlag == NVMEDIA_TRUE) &&
            (regVal[0] == (NvU32)0x00) && (regVal[1] == (NvU32)ONE_COLOR_DGAIN_VAL) &&
            (regVal[6] == (NvU32)0x00) && (regVal[7] == (NvU32)ONE_COLOR_DGAIN_VAL)) {
            regAdd = (NvU32)REG_DGAIN_GR + 2u; // start from B gain
            dataLength = 4u; // write only B and R gain
            dataAdd = &regVal[2];
        } else {
            regAdd = (NvU32)REG_DGAIN_GR;
            dataLength = 8u;
            dataAdd = &regVal[0];

            if((regVal[0] == (NvU32)0x00) && (regVal[1] == (NvU32)ONE_COLOR_DGAIN_VAL) &&
            (regVal[6] == (NvU32)0x00) && (regVal[7] == (NvU32)ONE_COLOR_DGAIN_VAL)) {
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
    _DriverHandle *drvrHandle,
    NvMediaISCEmbeddedData *parsedInformation)
{
    const NvU8 *data = (NvU8*)(parsedInformation->top.data);
    NvU32 i, k, index, address;
    NvU32 j;
    NvU8 regPair[2];
    NvF32 value;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if((parsedInformation == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(drvrHandle->config_info.sensorVersion == 0u) {
        status = getSenVerFromEmb(drvrHandle, data, EMB_PARSED);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_WARN("AR0231 sensor version is unknown!\n");
        }
    }

    if(drvrHandle->config_info.sensorVersion == AR0231_VER_6) {
        address = EMB_OFFSET_DGAIN_GR + EMB_EXTRA_OFFSET_V6;
    } else {
        address = EMB_OFFSET_DGAIN_GR;
    }

    for (i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
        (void)memset(parsedInformation->wbGain[i].value, 0,
               sizeof(parsedInformation->wbGain[i].value));
    }

    for(i = 0; i < 4u; i++) {  // Gr, B, R, Gb
        index = colorIndex[i]; // R, Gr, Gb, B
        regPair[0] = data[address];
        regPair[1] = data[address + 1u];
        value = (NvF32)((regPair[0] << 8) + regPair[1]) / (NvF32)ONE_COLOR_DGAIN_VAL;

        // all channels (if enabled) have same wb gains
        // if channel is not enabled, wb gain values are zero
        k = 0;
        for(j = 0; j < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; j++) {
            if ((drvrHandle->exposureModeMask & (1u << j)) == (1u << j)) {
                parsedInformation->wbGain[k].value[index] = value;
                k++;
            }
        }
        address += 2u;
        LOG_DBG("%s: %d: %f\n", __func__, i, value);
    }

    return status;
}

static NvMediaStatus
ParseEmbeddedData(
    NvMediaISCDriverHandle *handle,
    NvMediaISCTransactionHandle *transaction,
    NvU32 lineCount,
    NvU32 *lineLength,
    NvU8 *lineData[],
    NvMediaISCEmbeddedData *parsedInformation)
{
    _DriverHandle *drvrHandle;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU32 i;
    NvU32 stride = 4u;
    NvU32 shift;
    NvU8 *dst;
    NvF32 lineRate;
    NvU32 frame_counter;

    if((handle == NULL) || (transaction == NULL) || (lineLength == NULL) || (lineData == NULL) || (parsedInformation == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    drvrHandle = (_DriverHandle *)handle;
    lineRate = drvrHandle->config_info.exposureLineRate;

    shift = 6u; // top: 8 bit data in bit[13:6] on each 16-bit

    if(parsedInformation->top.bufferSize < (lineLength[0] / stride)) {
        status = NVMEDIA_STATUS_INSUFFICIENT_BUFFERING;
        goto done;
    }

    dst = (NvU8*)parsedInformation->top.data;

    // total buffer size: lineLength[0] = ACTIVE_H * 32; only extract used data bytes
    for(i = 0; i < ((NvU32)((EMB_OFFSET_AGAIN * 4u) + 20u)); (i += stride)) {
        *dst++  = (*((NvU16*)(lineData[0] + i))) >> shift; // skip tag byte
    }

    // top.baseRegAddress can not be used by app
    parsedInformation->top.baseRegAddress = 0x0;
    parsedInformation->top.size = lineLength[0] / stride;

    // parse gains and the exposure time
    status = ParseExposure(drvrHandle, parsedInformation, lineRate);
    if(status != NVMEDIA_STATUS_OK) {
        goto done;
    }

    // parse WB gains
    status = ParseWBGain(drvrHandle, parsedInformation);
    if(status != NVMEDIA_STATUS_OK) {
        goto done;
    }

    // parse sensor temprature data
    drvrHandle->tempratureData = (parsedInformation->top.data[EMB_OFFSET_TEMPSENS0] << 8)
                                  + parsedInformation->top.data[EMB_OFFSET_TEMPSENS0 + 1u];

    parsedInformation->mode = 1u;  // 12bit: 1;

    // parse frame counter
    frame_counter = (parsedInformation->top.data[EMB_OFFSET_FRAME_CNT] << 24) |
                    (parsedInformation->top.data[EMB_OFFSET_FRAME_CNT + 1u] << 16) |
                    (parsedInformation->top.data[EMB_OFFSET_FRAME_CNT + 2u] << 8) |
                    parsedInformation->top.data[EMB_OFFSET_FRAME_CNT + 3u];
    parsedInformation->frameSequenceNumber = frame_counter;

    if((lineCount == 1u) || (lineLength[1] == 0u)) {
        goto done;
    }

    if(parsedInformation->bottom.bufferSize < (lineLength[1] / stride)) {
        status = NVMEDIA_STATUS_INSUFFICIENT_BUFFERING;
        goto done;
    }

    // AR0231 RGGB sensor bottom emb is not enabled
    LOG_ERR("AR0231 RGGB sensor bottom emb is not enabled!");

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
    NvU32 lineCount,
    NvU32 *lineLength,
    NvU8 *lineData[],
    NvU32 *sensorFrameId)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    const NvU8 *offset;

    if((handle == NULL) || (transaction == NULL) || (lineLength == NULL) || (lineData == NULL) || (sensorFrameId == NULL)
       || (lineCount < 1u) || (lineLength[0] < (NvU32)(EMB_OFFSET_FRAME_ID * 4u))) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if(((_DriverHandle *)handle)->config_info.sensorVersion == 0u) { // for reprocess
        status = getSenVerFromEmb((_DriverHandle *)handle, lineData[0], EMB_UNPARSED);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_WARN("AR0231 sensor version is unknown!\n");
        }
    }

    if(((_DriverHandle *)handle)->config_info.sensorVersion == AR0231_VER_6) {
        offset = lineData[0] + ((EMB_OFFSET_FRAME_ID + EMB_EXTRA_OFFSET_V6) * 4u);
    } else {
        offset = lineData[0] + (EMB_OFFSET_FRAME_ID * 4u);
    }

    *sensorFrameId = ((*((const NvU32 *)offset)) >> 6) & (NvU32)0xff;

    LOG_DBG("%s: FrameId %x\n", __func__, *sensorFrameId);

    return status;
}

static NvMediaStatus
GetSensorProperties(
    NvMediaISCDriverHandle *handle,
    NvMediaISCSensorProperties *properties)
{
    const _DriverHandle *drvrHandle = (_DriverHandle *)handle;
    ConfigResolutionAR0231 resolution;
    NvU32 i;
    NvU32 k;
    NvF32 pixelRate, maxExp[NVMEDIA_ISC_EXPOSURE_MODE_MAX];
    NvU32 hts = ((_DriverHandle *)handle)->timingSettings->hts;
    NvU32 vts = ((_DriverHandle *)handle)->timingSettings->vts;
    const NvU16 *fineIntegTime = ((_DriverHandle *)handle)->config_info.fineIntegTime;

    if((handle == NULL) || (properties == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    (void)memset(properties, 0, sizeof(*properties));
    resolution = drvrHandle->config_info.resolution;
    pixelRate = 1e9 / (NvF32)AR0231_PCLK;

    if(resolution < ISC_CONFIG_AR0231_NUM_RESOLUTION) {
        maxExp[2] = ((pixelRate * (NvF32)(hts) * (NvF32)(MAX_AE_EXP_LINES_T3 - 1u)) +
                    (NvF32)fineIntegTime[2]);
        maxExp[1] = ((pixelRate * (NvF32)(hts) * (NvF32)(MAX_AE_EXP_LINES_T2 - 1u)) +
                    (NvF32)fineIntegTime[1]) + maxExp[2];
        // maxExp[0] < VTS - 4; give couple of extra rows
        maxExp[0] = (((pixelRate * (NvF32)(hts)) * (NvF32)(vts - 9u)) +
                    (NvF32)fineIntegTime[0] + (NvF32)fineIntegTime[1] + (NvF32)fineIntegTime[2]);

        k = 0;
        for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
            if((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
                properties->exposure[k].valid = NVMEDIA_TRUE;

                switch (i) {
                    case 0:
                        properties->exposure[k].minExposureTime = pixelRate *
                                    (NvF32)(fineIntegTime[0] + fineIntegTime[1] + fineIntegTime[2]);
                        break;
                    case 1:
                        properties->exposure[k].minExposureTime = pixelRate *
                                    (NvF32)(fineIntegTime[1] + fineIntegTime[2]);
                        break;
                    case 2:
                        properties->exposure[k].minExposureTime = pixelRate * (NvF32)fineIntegTime[2];
                        break;
                    default:
                        break;
                }

                properties->exposure[k].maxExposureTime = maxExp[i];
                properties->exposure[k].minSensorGain = MIN_AGAIN_LCG * MIN_DGAIN;
                properties->exposure[k].maxSensorGain = drvrHandle->config_info.maxGain;
                k++;
            }
        }

        properties->frameRate = drvrHandle->config_info.frameRate;
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
    if((handle == NULL) || (cameraModuleConfig == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    (void)strncpy(cameraModuleConfig->cameraModuleCfgName,
           ((_DriverHandle *)handle)->moduleCfg.cameraModuleCfgName,
           strlen(((_DriverHandle *)handle)->moduleCfg.cameraModuleCfgName));

    cameraModuleConfig->cameraModuleConfigPass1 = ((_DriverHandle *)handle)->moduleCfg.cameraModuleConfigPass1;
    cameraModuleConfig->cameraModuleConfigPass2 = ((_DriverHandle *)handle)->moduleCfg.cameraModuleConfigPass2;

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
GetSensorAttr(
    NvMediaISCDriverHandle *handle,
    NvMediaISCSensorAttrType type,
    NvU32 size,
    void *attribute)
{
    const _DriverHandle *drvrHandle;
    const NvU16 *fineIntegTime;
    NvF32 val[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {0};
    NvF32 tmpVal[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {0};
    NvF32 pixelRate;
    NvU32 i;
    NvS32 k = 0;

    if((handle == NULL) || (attribute == NULL)) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    drvrHandle = (_DriverHandle *)handle;
    fineIntegTime = drvrHandle->config_info.fineIntegTime;
    pixelRate = 1e9 / (NvF32)AR0231_PCLK;

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
            if(size != (sizeof(val))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
                    val[k] = MIN_AGAIN_LCG * MIN_DGAIN;
                    k++;
                }
            }

            (void)memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_GAIN_MAX:
        {
            if(size != (sizeof(val))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
                    val[k] = drvrHandle->config_info.maxGain;
                    k++;
                }
            }

            (void)memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_MIN:
        {
            if(size != (sizeof(val))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            tmpVal[2] = (pixelRate * (NvF32)fineIntegTime[2]) / 1e9;
            tmpVal[1] = (pixelRate * (NvF32)fineIntegTime[1]) / 1e9 + tmpVal[2];
            tmpVal[0] = (pixelRate * (NvF32)fineIntegTime[0]) / 1e9 + tmpVal[1];

            for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
                    val[k] = tmpVal[i];
                    k++;
                }
            }

            (void)memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_MAX:
        {
            if(size != (sizeof(val))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }
            tmpVal[2] = (pixelRate * ((NvF32)(drvrHandle->timingSettings->hts) *
                        (NvF32)(MAX_AE_EXP_LINES_T3 - 1u) + (NvF32)fineIntegTime[2])) / 1e9;
            tmpVal[1] = (pixelRate * ((NvF32)(drvrHandle->timingSettings->hts) *
                        (NvF32)(MAX_AE_EXP_LINES_T2 - 1u) + (NvF32)fineIntegTime[1])) / 1e9 + tmpVal[2];
            tmpVal[0] = (pixelRate * ((NvF32)(drvrHandle->timingSettings->hts) *
                        (NvF32)(drvrHandle->timingSettings->vts - 9u) + (NvF32)fineIntegTime[0]
                        + (NvF32)fineIntegTime[1] + (NvF32)fineIntegTime[2])) / 1e9;
            for (i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
                    val[k] = tmpVal[i];
                    k++;
                }
            }

            (void)memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_HDR_MAX:
        {
            if (size != (sizeof(NvU32))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            *((NvU32 *) attribute) = 256u;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_FRAME_RATE:
        {
            if (size != (sizeof(NvF32))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            *((NvF32 *) attribute) = drvrHandle->config_info.frameRate;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_NUM_EXPOSURES:
        {
            if (size != (sizeof(NvU32))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
                    k++;
                }
            }

            *((NvU32 *) attribute) = k;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_GAIN_FACTOR:
        {
            if(size != (sizeof(NvF32))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            *((NvF32 *) attribute) = HIGH_CGAIN;
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_FINE:
        {
            if(size != (sizeof(val))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            tmpVal[2] = (pixelRate * (NvF32)fineIntegTime[2]) / 1e9;
            tmpVal[1] = ((pixelRate * (NvF32)fineIntegTime[1]) + tmpVal[2]) / 1e9;
            tmpVal[0] = ((pixelRate * (NvF32)fineIntegTime[0]) + tmpVal[1]) / 1e9;

            for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if((drvrHandle->exposureModeMask & (1u << i)) == (1u << i)) {
                    val[k] = tmpVal[i];
                    k++;
                }
            }

            (void)memcpy(attribute, val, size);
            return NVMEDIA_STATUS_OK;
        }
        case NVMEDIA_ISC_SENSOR_ATTR_ET_STEP:
        {
            NvF64 step[NVMEDIA_ISC_EXPOSURE_MODE_MAX] = {0};

            if(size != (sizeof(step))) {
                return NVMEDIA_STATUS_BAD_PARAMETER;
            }

            for(i = 0; i < (NvU32)NVMEDIA_ISC_EXPOSURE_MODE_MAX; i++) {
                if( (drvrHandle->exposureModeMask & (1u << i)) == (1u << i) ) {
                    step[k] = (pixelRate * (NvF32)(drvrHandle->timingSettings->hts)) / 1e9;
                    k++;
                }
            }

            (void)memcpy(attribute, step, size);
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

    if (!handle || !transaction || !numPoints || !kneePoints) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    if (numPoints != NUM_COMPANDING_KNEE_POINTS) {
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }

    for (i = 0; i < numPoints; i++) {
        if (((unsigned int) kneePoints[i].x) != input[i]) {
            return NVMEDIA_STATUS_BAD_PARAMETER;
        }
    }

    for (i = 0; i < numPoints; i++) {
        val = ((unsigned int) kneePoints[i].y) << 4;
        data[i * 2] = ((unsigned char) ((val >> 8) & 0xFF));
        data[i * 2 + 1] = ((unsigned char) (val & 0xFF));
    }

    status = WriteRegister(handle, transaction, REG_USER_KNEE_LUT0, sizeof(data), data);
    if (status != NVMEDIA_STATUS_OK) {
        return status;
    }

    // Disable legacy companding
    status = WriteRegister(handle, transaction, REG_KNEE_LUT_CTL + 1, 1, &ctlData);
    return status;
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
    .SetCompandingCurve = SetCompandingCurve
};

NvMediaISCDeviceDriver *
GetAR0231Driver(void)
{
    return &deviceDriver;
}

NvMediaStatus
GetAR0231ConfigSet(
    const char *resolution,
    const char *inputFormat,
    int *configSet)
{
    // set input mode setting for ar0231
    if((inputFormat != NULL) && (strcasecmp(inputFormat, "raw12") == 0)) {
        if((resolution != NULL) && (strcasecmp(resolution, "1920x1208") == 0)) {
            *configSet = ISC_CONFIG_AR0231_DVP_RAW12_COMP_1920X1208;
        } else if((resolution != NULL) && (strcasecmp(resolution, "1920x1008") == 0)) {
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
