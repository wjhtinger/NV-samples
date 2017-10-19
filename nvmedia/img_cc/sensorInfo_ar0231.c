/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "cmdline.h"
#include "sensorInfo_ar0231.h"

typedef enum {
    AR0231_EXPOSURE_1 = 0,
    AR0231_EXPOSURE_2,
    AR0231_EXPOSURE_3,
    AR0231_EXPOSURE_4,
    AR0231_NUM_EXPOSURES
} Ar0231Exposures;

typedef struct {
    CmdlineParameter        et[AR0231_NUM_EXPOSURES];
    CmdlineParameter        etR[AR0231_NUM_EXPOSURES];
    CmdlineParameter        ag[AR0231_NUM_EXPOSURES];
    CmdlineParameter        dg;
    CmdlineParameter        cg[AR0231_NUM_EXPOSURES];
    CmdlineParameter        max_exp;
    CmdlineParameter        n_exp;
    CmdlineParameter        wbRGGB;
    CmdlineParameter        lkp;
    CmdlineParameter        one_exp;
} Ar0231Properties;

static char *ar0231SupportedArgs[] = {
    "-et1",
    "-et2",
    "-et3",
    "-et4",
    "-etR1",
    "-etR2",
    "-etR3",
    "-etR4",
    "-ag1",
    "-ag2",
    "-ag3",
    "-ag4",
    "-dg",
    "-cg1",
    "-cg2",
    "-cg3",
    "-cg4",
    "-max_exp",
    "-n_exp",
    "-wbRGGB",
    "-lkp",
    "-one_exp"
};

static const float aGainTbl[15] = {
    0.1250, // 0 - 1/8x
    0.2500, // 1 - 2/8x
    0.2857, // 2 - 2/7x
    0.4286, // 3 - 3/7x
    0.5000, // 4 - 3/6x
    0.6667, // 5 - 4/6x
    0.8000, // 6 - 4/5x
    1.0000, // 7 - 5/5x
    1.2500, // 8 - 5/4x
    1.5000, // 9 - 6/4x
    2.0000, //10 - 6/3x
    2.3333, //11 - 7/3x
    3.5000, //12 - 7/2x
    4.0000, //13 - 8/2x
    8.0000  //14 - 8/1x
};

static NvMediaStatus
ReadSensorExposureInfo(I2cCommands *settings,
    CalibrationParameters *calParam,
    NvRawSensorHDRInfo_v2 * sensorInfo, NvU32 numExposures)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    RegisterSetup rgstrArrayExp[9], rgstrArrayGain[4];
    float expTime[AR0231_NUM_EXPOSURES] = {0.0},
          coarseTime[AR0231_NUM_EXPOSURES] = {0.0},
          fineIntTime[AR0231_NUM_EXPOSURES] = {0.0};
    NvU16 lineLenPck;
    NvU32 i = 0;

    memset(rgstrArrayExp, 0, sizeof(RegisterSetup) * 9);
    //read back exp time - rows
    rgstrArrayExp[0].rgstrAddr = (AR0231_REG_EXP_TIME_T1_ROW >> 8) + (AR0231_REG_EXP_TIME_T1_ROW << 8);
    rgstrArrayExp[1].rgstrAddr = ((AR0231_REG_EXP_TIME_T1_ROW+2) >> 8) + ((AR0231_REG_EXP_TIME_T1_ROW+2) << 8);
    rgstrArrayExp[2].rgstrAddr = ((AR0231_REG_EXP_TIME_T1_ROW+4) >> 8) + ((AR0231_REG_EXP_TIME_T1_ROW+4) << 8);
    rgstrArrayExp[3].rgstrAddr = ((AR0231_REG_EXP_TIME_T1_ROW+6) >> 8) + ((AR0231_REG_EXP_TIME_T1_ROW+6) << 8);
    //read back fine exp time - pclks
    rgstrArrayExp[4].rgstrAddr = (AR0231_REG_FINE_EXP_TIME_T1_PCLK >> 8) + (AR0231_REG_FINE_EXP_TIME_T1_PCLK << 8);
    rgstrArrayExp[5].rgstrAddr = ((AR0231_REG_FINE_EXP_TIME_T1_PCLK+2) >> 8) + ((AR0231_REG_FINE_EXP_TIME_T1_PCLK+2) << 8);
    rgstrArrayExp[6].rgstrAddr = ((AR0231_REG_FINE_EXP_TIME_T1_PCLK+4) >> 8) + ((AR0231_REG_FINE_EXP_TIME_T1_PCLK+4) << 8);
    rgstrArrayExp[7].rgstrAddr = ((AR0231_REG_FINE_EXP_TIME_T1_PCLK+6) >> 8) + ((AR0231_REG_FINE_EXP_TIME_T1_PCLK+6) << 8);

    rgstrArrayExp[8].rgstrAddr = (AR0231_REG_LINE_LENGTH_PCK >> 8) + (AR0231_REG_LINE_LENGTH_PCK << 8); //HTS
    for (i = 0; i < 9; i++) {
        rgstrArrayExp[i].rgstrVal = (NvU16 *)I2cSetupRegister(
                                     settings,
                                     READ_REG_2,
                                     calParam->sensorAddress,
                                     (NvU8 *)&rgstrArrayExp[i].rgstrAddr,
                                     NULL,
                                     AR0231_REG_DATA_BYTES);
        if (!rgstrArrayExp[i].rgstrVal) {
            LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                    rgstrArrayExp[i].rgstrAddr);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }

    status = I2cProcessCommands(
                 settings,
                 I2C_READ,
                 calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    //lineLenPck
    lineLenPck = (*rgstrArrayExp[8].rgstrVal >> 8) + (*rgstrArrayExp[8].rgstrVal << 8);

    // calculate exposure time (us)
    for (i = 0; i < numExposures; i++) {
        coarseTime[i] = ((*rgstrArrayExp[i].rgstrVal >> 8) & 0xff) |
                         ((*rgstrArrayExp[i].rgstrVal << 8) & 0xff00);
        fineIntTime[i] = ((*rgstrArrayExp[i+4].rgstrVal >> 8) & 0xff) |
                          ((*rgstrArrayExp[i+4].rgstrVal << 8) & 0xff00);
        expTime[i] = 1.0/AR0231_1928X1208_PCLK *
                     ((coarseTime[i] - 1)*lineLenPck + fineIntTime[i]);
        sensorInfo[i].exposure.exposureTime = expTime[i];  //seconds
    }

    LOG_INFO("%s: Sensor exposure time readback: \n", __func__);
    for (i = 0; i < numExposures; i++) {
        LOG_INFO(" T%d: %f\n", i + 1,expTime[i]);
    }


    //Readback gains
    memset(rgstrArrayGain, 0, sizeof(RegisterSetup) * 4);
    //digitalGain
    rgstrArrayGain[0].rgstrAddr = ((AR0231_REG_DGAIN >> 8) & 0xff) + ((AR0231_REG_DGAIN << 8) & 0xff00);
    //analogGain
    rgstrArrayGain[1].rgstrAddr = ((AR0231_REG_AGAIN >> 8) & 0xff) + ((AR0231_REG_AGAIN << 8) & 0xff00);
    //conversionGain
    rgstrArrayGain[2].rgstrAddr = ((AR0231_REG_CGAIN >> 8) & 0xff) + ((AR0231_REG_CGAIN << 8) & 0xff00);


    for (i = 0; i < 4; i++) {
        rgstrArrayGain[i].rgstrVal = (NvU16 *)I2cSetupRegister(
                                     settings,
                                     READ_REG_2,
                                     calParam->sensorAddress,
                                     (NvU8 *)&rgstrArrayGain[i].rgstrAddr,
                                     NULL,
                                     AR0231_REG_DATA_BYTES);
        if (!rgstrArrayGain[i].rgstrVal) {
            LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                    rgstrArrayGain[i].rgstrAddr);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }

    status = I2cProcessCommands(
                 settings,
                 I2C_READ,
                 calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    for (i = 0; i < numExposures; i++){

        sensorInfo[i].exposure.digitalGain = (1.0/ AR0231_ONE_DGAIN_VAL) *
                                             (((*rgstrArrayGain[0].rgstrVal >> 8) & 0xff) |
                                             ((*rgstrArrayGain[0].rgstrVal << 8) & 0xff00));

        sensorInfo[i].exposure.conversionGain = ((((((*rgstrArrayGain[2].rgstrVal >> 8) & 0xff) |
                                                ((*rgstrArrayGain[2].rgstrVal << 8) & 0xff00)) & (0x1 << i)) >> i) & 0x1) ? 3.0 : 1.0;


        sensorInfo[i].exposure.analogGain = aGainTbl[(((((*rgstrArrayGain[1].rgstrVal >> 8) & 0xff) |
                                                     ((*rgstrArrayGain[1].rgstrVal << 8) & 0xff00)) & (0xf << i*4)) >> i*4 ) & 0xf];

        sensorInfo[i].exposure.digitalGain = sensorInfo[i].exposure.digitalGain *
                                             sensorInfo[i].exposure.conversionGain *
                                             sensorInfo[i].exposure.analogGain;
        sensorInfo[i].exposure.analogGain = 1.0;
        sensorInfo[i].exposure.conversionGain = 1.0;
     }

failed:
    return status;
}

static NvMediaStatus
ReadSensorWbGainsInfo(I2cCommands *settings,
    CalibrationParameters *calParam,
    NvRawSensorHDRInfo_v2 * sensorInfo,
    NvU32 numExposures)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU8 address[2];
    NvF32 val;
    RegisterSetup rgstr;
    NvU32 i = 0, j = 0;

    for (i = 0; i < 4; i++) {  //Gr, B, R, Gb
        address[0] = ((AR0231_REG_DGAIN_GR + i * 2) >> 8) & 0xff;
        address[1] = (AR0231_REG_DGAIN_GR + i *2) & 0xff;
        rgstr.rgstrAddr = address[0] + (address[1] << 8);
        rgstr.rgstrVal = (NvU16 *) I2cSetupRegister(
                    settings,
                    READ_REG_2,
                    calParam->sensorAddress,
                    (NvU8 *)&rgstr.rgstrAddr,
                    NULL,
                    AR0231_REG_DATA_BYTES);
        if (!rgstr.rgstrVal) {
            LOG_ERR("%s: Failed to get WB gains\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }

        status = I2cProcessCommands(
                     settings,
                     I2C_READ,
                     calParam->i2cDevice);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to process register settings\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }

        val = (((*rgstr.rgstrVal >> 8) & 0xff) |
               ((*rgstr.rgstrVal << 8) & 0xff00)) * (1.0 / AR0231_ONE_COLOR_DGAIN_VAL);

        for (j = 0; j < numExposures; j++) {
            if (i == 0)
                sensorInfo[j].wbGain.value[1] = val; // Gr
            else if (i == 1)
                sensorInfo[j].wbGain.value[3] = val; // B
            else if (i == 2)
                sensorInfo[j].wbGain.value[0] = val; // R
            else
                sensorInfo[j].wbGain.value[2] = val; // Gb
        }
    }

failed:
    return status;
}

static NvMediaStatus
ReadEmbeddedLinesInfo(I2cCommands *settings,
    CalibrationParameters *calParam,
    NvU32 *embeddedLinesTop, NvU32 *embeddedLinesBottom)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    RegisterSetup rgstrArrayEmbedLines[4];
    NvU32 i = 0;
    NvU32 val = 0;
    NvU32 embTopEnabled = 0, embBottomEnabled = 0;
    NvU32 embTopDarkRows = 0, embBotDarkRows = 0;
    NvU32 embTopTestRows = 0, embBotTestRows = 0;

    //Readback emdLines
    memset(rgstrArrayEmbedLines, 0, sizeof(RegisterSetup) * 4);
    rgstrArrayEmbedLines[0].rgstrAddr = ((AR0231_REG_EMBEDDED_LINES_ENABLE >> 8) & 0x00ff) +
                                         ((AR0231_REG_EMBEDDED_LINES_ENABLE << 8) & 0xff00);
    rgstrArrayEmbedLines[1].rgstrAddr = ((AR0231_REG_EMBEDDED_TOP_DARK_ROWS >> 8) & 0x00ff) +
                                         ((AR0231_REG_EMBEDDED_TOP_DARK_ROWS << 8) & 0xff00);
    rgstrArrayEmbedLines[2].rgstrAddr = ((AR0231_REG_EMBEDDED_BOT_DARK_ROWS >> 8) & 0x00ff) +
                                         ((AR0231_REG_EMBEDDED_BOT_DARK_ROWS << 8) & 0xff00);
    rgstrArrayEmbedLines[3].rgstrAddr = ((AR0231_REG_EMBEDDED_TEST_ROWS >> 8) & 0x00ff) +
                                         ((AR0231_REG_EMBEDDED_TEST_ROWS << 8) & 0xff00);

    for (i = 0; i < 4; i++) {
        rgstrArrayEmbedLines[i].rgstrVal = (NvU16 *)I2cSetupRegister(
                                     settings,
                                     READ_REG_2,
                                     calParam->sensorAddress,
                                     (NvU8 *)&rgstrArrayEmbedLines[i].rgstrAddr,
                                     NULL,
                                     AR0231_REG_DATA_BYTES);
        if (!rgstrArrayEmbedLines[i].rgstrVal) {
            LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                    rgstrArrayEmbedLines[i].rgstrAddr);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }

    status = I2cProcessCommands(
                 settings,
                 I2C_READ,
                 calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    val = (((*rgstrArrayEmbedLines[0].rgstrVal >> 8) & 0xff) |
                                              ((*rgstrArrayEmbedLines[0].rgstrVal << 8) & 0xff00));

    if ((val >> 8) & 0x1) {
       embTopEnabled = 1;
       *embeddedLinesTop = AR0231_MIN_NUM_EMB_LINES;
    }

    if ((val >> 7) & 0x1) {
       embBottomEnabled = 1;
       *embeddedLinesBottom = AR0231_MIN_NUM_EMB_LINES;
    }

    if (embTopEnabled) {
           embTopDarkRows = (((((*rgstrArrayEmbedLines[1].rgstrVal >> 8) & 0xff) |
                               ((*rgstrArrayEmbedLines[1].rgstrVal << 8) & 0xff00)) >> 3) & 0x1f);

           embTopTestRows = (((((*rgstrArrayEmbedLines[3].rgstrVal >> 8) & 0xff) |
                               ((*rgstrArrayEmbedLines[3].rgstrVal << 8) & 0xff00)) >> 3) & 0x1f);
           *embeddedLinesTop += embTopDarkRows + embTopTestRows;
    }

    if (embBottomEnabled) {
           embBotDarkRows = (((((*rgstrArrayEmbedLines[2].rgstrVal >> 8) & 0xff) |
                               ((*rgstrArrayEmbedLines[2].rgstrVal << 8) & 0xff00)) >> 3) & 0x1f);

           embBotTestRows = (((((*rgstrArrayEmbedLines[3].rgstrVal >> 8) & 0xff) |
                               ((*rgstrArrayEmbedLines[3].rgstrVal << 8) & 0xff00)) >> 8) & 0x0f);
           *embeddedLinesBottom += embBotDarkRows + embBotTestRows;
    }

failed:
    return status;

}

static NvMediaStatus
ReadSensorLUTInfo(I2cCommands *settings,
    CalibrationParameters *calParam,
    NvRawSensorHDRInfo_v2 * sensorInfo, NvF32 *LUTInfo)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    RegisterSetup rgstrArrayLUT[12];
    NvU32 origLUT[14] = {0};
    NvU32 i = 0, j = 0;
    NvF32 alpha = 0.0, beta = 0.0, k = 0.0;

    //Readback LUT knee points
    memset(rgstrArrayLUT, 0, sizeof(RegisterSetup) * 12);
    for (i = 0; i < 12; i++) {
        rgstrArrayLUT[i].rgstrAddr = (((AR0231_REG_USER_KNEE_LUT0 + i*2) >> 8) & 0x00ff) +
                                       (((AR0231_REG_USER_KNEE_LUT0 + i*2) << 8) & 0xff00);
    }

    for (i = 0; i < 12; i++) {
        rgstrArrayLUT[i].rgstrVal = (NvU16 *)I2cSetupRegister(
                                     settings,
                                     READ_REG_2,
                                     calParam->sensorAddress,
                                     (NvU8 *)&rgstrArrayLUT[i].rgstrAddr,
                                     NULL,
                                     AR0231_REG_DATA_BYTES);
        if (!rgstrArrayLUT[i].rgstrVal) {
            LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                    rgstrArrayLUT[i].rgstrAddr);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }

    status = I2cProcessCommands(
                 settings,
                 I2C_READ,
                 calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    // interpolate 14 points
    // min(0) + 12LUT points + max(4095)
    for (i = 0; i < 14; i++) {
        if (i == 0){
            origLUT[i] = 0;
        } else if (i == 13) {
            origLUT[i] = 0x0fff;
        } else {
            origLUT[i] = ((((*rgstrArrayLUT[i-1].rgstrVal >> 8) & 0xff) |
                          ((*rgstrArrayLUT[i-1].rgstrVal << 8) & 0xff00)) >> 4) & 0x0fff;
        }
    }

    // interpolate-LUT
    for (i = 0; i < 256; ++i) {
        for (j = 0; j < 13; j++){
            if (i == 0){
                LUTInfo[i] = 0;
            } else {
                // interpolate 0:4095 in 256 pts
                // each interval = 4095/255 = 16.059
                k = (16.059) * i;
                if (k > origLUT[j] && k <= origLUT[j+1]){
                    alpha = k - origLUT[j];
                    beta = origLUT[j+1] - k;
                    LUTInfo[i] = (1.0/ pow(2, 20)) *
                                  (256.0 *(origLUT[j]*beta + origLUT[j+1]*alpha)/(alpha + beta));
                    if (LUTInfo[i] >= 1.0) LUTInfo[i] = 1.0;
                } else if (k > origLUT[13]) {
                    // clip the values beyond maxLUT
                    LUTInfo[i] = 1.0;
                }
            }
        }
    }

failed:
    return status;
}

static unsigned int
GetAgainVal(
    float again)
{
    unsigned int i;

    if (again < aGainTbl[0])
        again = aGainTbl[0]; // Minimal again

    for (i = 14; i >= 0; i--) {
        if (again >= aGainTbl[i])
            break;
    }

    return i;
}

static NvMediaStatus
SetExposureTime(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam,
    SensorDefaultSet *sensorDefaultSet)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU32 i;
    NvU8 address[2];
    NvU8 data[2];
    NvU16 val;
    float expRows;
    NvU8 *pWreg = NULL;

    for (i = 0; i < AR0231_NUM_EXPOSURES; i++) {
        if (ar0231Properties->et[i].isUsed == NVMEDIA_TRUE) {
            val = i ? (AR0231_REG_COARSE_INTEG_TIME_T2 + (i - 1) * 4) : AR0231_REG_COARSE_INTEG_TIME_T1;

            // Set coarse time lines, fine integration tiem is fixed
            address[0] = (val >> 8) & 0xff;
            address[1] = val & 0xff;

            // Use etRx values if both etRx and etx being set
            if (ar0231Properties->etR[i].floatValue > 0.0)
                expRows = ar0231Properties->etR[i].floatValue + 1 -
                          ((float) sensorDefaultSet->fineIntTime[i]) /
                          (sensorDefaultSet->hts) + 0.5;  // round
            else
                expRows = (ar0231Properties->et[i].floatValue * AR0231_1928X1208_PCLK / 1000000 +
                          sensorDefaultSet->hts - ((float) sensorDefaultSet->fineIntTime[i]))
                          / (sensorDefaultSet->hts) + 0.5; // et[i] - micro sec

            val = expRows;
            if (val <= 0)
                val = 1;

            if(((i==1) && (val > MAX_COARSE_INTEG_TIME_T2)) ||
                ((i==2) && (val > MAX_COARSE_INTEG_TIME_T3)) ||
                ((i==3) && (val > MAX_COARSE_INTEG_TIME_T4))) {
                LOG_ERR("%s: Max exp time: T2 <= %d, T3 <= %d, T4 = %d rows + fine Integration time; \
                         T%d: %d rows \n", __func__, (MAX_COARSE_INTEG_TIME_T2 - 1),
                         (MAX_COARSE_INTEG_TIME_T3 - 1), (MAX_COARSE_INTEG_TIME_T4 - 1), i + 1, val);
                status = NVMEDIA_STATUS_ERROR;
                goto failed;
            }

            data[0] = (val >> 8) & 0xff;
            data[1] = val & 0xff;

            pWreg = I2cSetupRegister(
                        commands,
                        WRITE_REG_2,
                        calParam->sensorAddress,
                        address,
                        data,
                        AR0231_REG_DATA_BYTES);
            if (!pWreg) {
                LOG_ERR("%s: Failed to set exp T%x\n", __func__, i + 1);
                status = NVMEDIA_STATUS_ERROR;
                goto failed;
            }
            status = NVMEDIA_STATUS_OK;
        }
    }

    LOG_INFO("%s:\n    Done!\n", __func__);

failed:
    return status;
}

static NvMediaStatus
SetDigitalGain(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU8 address[2];
    NvU8 data[2];
    NvU16 val;
    NvU8 *pWreg = NULL;

    if (ar0231Properties->dg.isUsed == NVMEDIA_TRUE) {
        address[0] = (AR0231_REG_DGAIN >> 8) & 0xff;
        address[1] = AR0231_REG_DGAIN & 0xff;

        val = (NvU16) (ar0231Properties->dg.floatValue * AR0231_ONE_DGAIN_VAL);
        if (val > 0x7ff) {
            LOG_ERR("%s: ***** Digital gain CANNOT bigger than 3.998 !!! set it to 3.998 *****\n", __func__);
            val = 0x7ff;
        }

        data[0] = (val >> 8) & 0xff;
        data[1] = val & 0xff;

        pWreg = I2cSetupRegister(
                    commands,
                    WRITE_REG_2,
                    calParam->sensorAddress,
                    address,
                    data,
                    AR0231_REG_DATA_BYTES);
        if (!pWreg) {
            LOG_ERR("%s: Failed to set WB gains\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
        status = NVMEDIA_STATUS_OK;
    }

    LOG_INFO("%s:\n    Digital gain: %.3f\n", __func__, (float) val / AR0231_ONE_DGAIN_VAL);

failed:
    return status;
}

static NvMediaStatus
SetConversionGain(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam,
    SensorDefaultSet *sensorDefaultSet)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU8 address[2];
    NvU8 data[2];
    NvU16 val = 0;
    NvU8 *pWreg = NULL;
    NvU32 i = 0;

    val = sensorDefaultSet->cGain & 0xff;

    for (i = 0; i < AR0231_NUM_EXPOSURES; i++) {
        if (ar0231Properties->cg[i].isUsed == NVMEDIA_TRUE) {
            val &= ~(0x1 << i);
            val |= ar0231Properties->cg[i].uIntValue << i;
        }
    }

    address[0] = (AR0231_REG_CGAIN >> 8) & 0xff;
    address[1] = AR0231_REG_CGAIN & 0xff;

    data[0] = 0x0;   // High byte not used
    data[1] = val & 0xff;

    pWreg = I2cSetupRegister(
                commands,
                WRITE_REG_2,
                calParam->sensorAddress,
                address,
                data,
                AR0231_REG_DATA_BYTES);
    if (!pWreg) {
       LOG_ERR("%s: Failed to set conversion gains\n", __func__);
       status = NVMEDIA_STATUS_ERROR;
       goto failed;
    }
    status = NVMEDIA_STATUS_OK;

    LOG_INFO("%s:\n    Conversion gain T4T3T2T1: 0x%x\n", __func__, val);

failed:
    return status;
}

static NvMediaStatus
SetAnalogGain(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam,
    SensorDefaultSet *sensorDefaultSet)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU8 address[2];
    NvU8 data[2];
    NvU16 val = 0;
    NvU8 *pWreg = NULL;
    NvU32 i = 0;

    val = sensorDefaultSet->aGain;

    for (i = 0; i < AR0231_NUM_EXPOSURES; i++) {
        if (ar0231Properties->ag[i].isUsed == NVMEDIA_TRUE) {
            val &= ~(0xf << (i * 4));
            val |= (GetAgainVal(ar0231Properties->ag[i].floatValue)) << (i * 4);
        }
    }

    address[0] = (AR0231_REG_AGAIN >> 8) & 0xff;
    address[1] = AR0231_REG_AGAIN & 0xff;

    data[0] = (val >> 8) & 0xff;
    data[1] = val & 0xff;

    pWreg = I2cSetupRegister(
                commands,
                WRITE_REG_2,
                calParam->sensorAddress,
                address,
                data,
                AR0231_REG_DATA_BYTES);
    if (!pWreg) {
       LOG_ERR("%s: Failed to set analog gains\n", __func__);
       status = NVMEDIA_STATUS_ERROR;
       goto failed;
    }
    status = NVMEDIA_STATUS_OK;

    LOG_INFO("%s:\n    Analog gain T1T2T3T4: %.3f,%.3f,%.3f,%.3f\n", __func__, \
            aGainTbl[val & 0xf], aGainTbl[(val >> 4) & 0xf], \
            aGainTbl[(val >> 8) & 0xf], aGainTbl[(val >> 12) & 0xf]);

failed:
    return status;
}

static NvMediaStatus
SetWBGain(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU8 address[2];
    NvU8 data[2];
    NvU16 val;
    NvU8 *pWreg = NULL;
    NvU32 i = 0;
    NvU16 tempGain[4];

    if (ar0231Properties->wbRGGB.isUsed == NVMEDIA_TRUE) {
        for (i = 0; i < 4; i++) {  //Gr, B, R, Gb
            address[0] = ((AR0231_REG_DGAIN_GR + i * 2) >> 8) & 0xff;
            address[1] = (AR0231_REG_DGAIN_GR + i *2) & 0xff;

            if (i == 0)
                val = ar0231Properties->wbRGGB.G1 * AR0231_ONE_COLOR_DGAIN_VAL;
            else if (i == 1)
                val = ar0231Properties->wbRGGB.B * AR0231_ONE_COLOR_DGAIN_VAL;
            else if (i == 2)
                val = ar0231Properties->wbRGGB.R * AR0231_ONE_COLOR_DGAIN_VAL;
            else
                val = ar0231Properties->wbRGGB.G2 * AR0231_ONE_COLOR_DGAIN_VAL;

            data[0] = (val >> 8) & 0xff;
            data[1] = val & 0xff;

            tempGain[i] = val;

            pWreg = I2cSetupRegister(
                        commands,
                        WRITE_REG_2,
                        calParam->sensorAddress,
                        address,
                        data,
                        AR0231_REG_DATA_BYTES);
            if (!pWreg) {
                LOG_ERR("%s: Failed to set WB gains\n", __func__);
                status = NVMEDIA_STATUS_ERROR;
                goto failed;
            }
            status = NVMEDIA_STATUS_OK;
        }
    }

    LOG_INFO("%s:\n    WB RGGB gain: %.3f,%.3f,%.3f,%.3f\n", __func__, \
            (float) tempGain[2] / AR0231_ONE_COLOR_DGAIN_VAL, (float) tempGain[0] / AR0231_ONE_COLOR_DGAIN_VAL, \
            (float) tempGain[3] / AR0231_ONE_COLOR_DGAIN_VAL, (float) tempGain[1] / AR0231_ONE_COLOR_DGAIN_VAL);

failed:
    return status;
}

static NvMediaStatus
SetKneeLut(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU8 address[2];
    NvU8 data[2];
    NvU16 val;
    NvU8 *pWreg = NULL;
    NvU32 i = 0;

    if (ar0231Properties->lkp.isUsed == NVMEDIA_TRUE) {
        for (i = 0; i < 12; i++) {  // 12 knee points
            address[0] = ((AR0231_REG_USER_KNEE_LUT0 + i * 2) >> 8) & 0xff;
            address[1] = (AR0231_REG_USER_KNEE_LUT0 + i *2) & 0xff;

            val = ar0231Properties->lkp.kp[i];

            data[0] = (val >> 8) & 0xff;
            data[1] = val & 0xff;

            pWreg = I2cSetupRegister(
                        commands,
                        WRITE_REG_2,
                        calParam->sensorAddress,
                        address,
                        data,
                        AR0231_REG_DATA_BYTES);
            if (!pWreg) {
                LOG_ERR("%s: Failed to set knee points\n", __func__);
                status = NVMEDIA_STATUS_ERROR;
                goto failed;
            }
            status = NVMEDIA_STATUS_OK;
        }

        // Enable user knee LUT
        address[0] = (AR0231_REG_KNEE_LUT_CTL >> 8) & 0xff;
        address[1] = AR0231_REG_KNEE_LUT_CTL & 0xff;

        data[0] = 0x00;
        data[1] = 0x0;  // bit0: 0 - disable legacy companding

        pWreg = I2cSetupRegister(
                    commands,
                    WRITE_REG_2,
                    calParam->sensorAddress,
                    address,
                    data,
                    AR0231_REG_DATA_BYTES);
        if (!pWreg) {
            LOG_ERR("%s: Failed to enable user knee LUT\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
        status = NVMEDIA_STATUS_OK;
    }

    LOG_INFO("%s: knee LUT:\n     %x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x\n", __func__,\
            ar0231Properties->lkp.kp[0], ar0231Properties->lkp.kp[1], ar0231Properties->lkp.kp[2], \
            ar0231Properties->lkp.kp[3], ar0231Properties->lkp.kp[4], ar0231Properties->lkp.kp[5], \
            ar0231Properties->lkp.kp[6], ar0231Properties->lkp.kp[7], ar0231Properties->lkp.kp[8], \
            ar0231Properties->lkp.kp[9], ar0231Properties->lkp.kp[10], ar0231Properties->lkp.kp[11]);

failed:
    return status;
}

static NvMediaStatus
SetMaxExp(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam,
    SensorDefaultSet *sensorDefaultSet)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU8 address[2];
    NvU8 data[2];
    NvU16 val;
    NvU8 *pWreg = NULL;

    if (ar0231Properties->max_exp.isUsed == NVMEDIA_TRUE) {
        if (((ar0231Properties->n_exp.isUsed == NVMEDIA_TRUE) &&
            (ar0231Properties->n_exp.uIntValue > ar0231Properties->max_exp.uIntValue)) ||
            ((ar0231Properties->n_exp.isUsed == NVMEDIA_FALSE) &&
            (((sensorDefaultSet->opCtl >> 2) & 0x3) > (ar0231Properties->max_exp.uIntValue - 1))))
            LOG_ERR("%s: !!! max_exp < n_exp, please set max_exp or n_exp again !!!", __func__);

        address[0] = (AR0231_REG_DIGITAL_CTRL  >> 8) & 0xff;
        address[1] = AR0231_REG_DIGITAL_CTRL  & 0xff;

        val = sensorDefaultSet->dCtl & 0xfc;  //bit[1:0]
        val |= ar0231Properties->max_exp.uIntValue - 1;

        data[0] = (val >> 8) & 0xff;
        data[1] = val & 0xff;

        pWreg = I2cSetupRegister(
                    commands,
                    WRITE_REG_2,
                    calParam->sensorAddress,
                    address,
                    data,
                    AR0231_REG_DATA_BYTES);
        if (!pWreg) {
            LOG_ERR("%s: Failed to set maximum number of exposure\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
        status = NVMEDIA_STATUS_OK;
    }

    LOG_INFO("%s:\n    max_exp: %x\n", __func__, ar0231Properties->max_exp.uIntValue);

failed:
    return status;
}

static NvMediaStatus
SetNumExp(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam,
    SensorDefaultSet *sensorDefaultSet)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU8 address[2];
    NvU8 data[2];
    NvU16 val;
    NvU8 *pWreg = NULL;

    if (ar0231Properties->n_exp.isUsed == NVMEDIA_TRUE) {
        // Need adjust max_exp?
        if (ar0231Properties->n_exp.uIntValue > ar0231Properties->max_exp.uIntValue) {
            if (ar0231Properties->max_exp.isUsed == NVMEDIA_TRUE) {
               LOG_ERR("%s: !!! max_exp < n_exp, please set max_exp or n_exp again !!!", __func__);
               status = NVMEDIA_STATUS_ERROR;
               goto failed;
            } else if ((ar0231Properties->n_exp.uIntValue - 1) > (sensorDefaultSet->dCtl & 0x3)) {
                ar0231Properties->max_exp.isUsed = NVMEDIA_TRUE;
                ar0231Properties->max_exp.uIntValue = ar0231Properties->n_exp.uIntValue;
                LOG_INFO("%s: Set max_exp to the same as n_exp\n", __func__);
                status = SetMaxExp(
                            ar0231Properties,
                            commands,
                            calParam,
                            sensorDefaultSet);
                if (status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to set maximum number of exposure\n", __func__);
                    goto failed;
                }
            }
        }

        address[0] = (AR0231_REG_OP_MODE_CTRL >> 8) & 0xff;
        address[1] = AR0231_REG_OP_MODE_CTRL & 0xff;

        val = sensorDefaultSet->opCtl & 0xf3;  // bit[3:2]
        val |= (ar0231Properties->n_exp.uIntValue - 1) << 2;

        data[0] = (val >> 8) & 0xff;
        data[1] = val & 0xff;

        pWreg = I2cSetupRegister(
                    commands,
                    WRITE_REG_2,
                    calParam->sensorAddress,
                    address,
                    data,
                    AR0231_REG_DATA_BYTES);
        if (!pWreg) {
            LOG_ERR("%s: Failed to set number of exposure\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }

        if (ar0231Properties->n_exp.uIntValue == 1) {
            LOG_WARN("%s: 1 exposure: set linear companding LUT with '-lkp [p1,...,p12]'!!!", __func__);
        }

        status = NVMEDIA_STATUS_OK;
    }

    LOG_INFO("%s:\n    n_exp: %x\n", __func__, ar0231Properties->n_exp.uIntValue);

failed:
    return status;
}

static NvMediaStatus
SetOneExp(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam,
    SensorDefaultSet *sensorDefaultSet)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU8 address[2];
    NvU8 data[2];
    NvU8 *pWreg = NULL;

    if (ar0231Properties->one_exp.isUsed == NVMEDIA_TRUE) {

        if ((ar0231Properties->one_exp.uIntValue > 3) ||
            (ar0231Properties->one_exp.uIntValue < 1)) {
            LOG_ERR("%s: !!! one_exp valid input: T1, T2, T3\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }

        address[0] = (AR0231_REG_DLO_CTL  >> 8) & 0xff;
        address[1] = AR0231_REG_DLO_CTL  & 0xff;

        data[0] = 0x12 + ((ar0231Properties->one_exp.uIntValue - 1) << 2) +
                  ((ar0231Properties->one_exp.uIntValue == 3) ? 4 : 0);
        data[1] = 0x00;

        pWreg = I2cSetupRegister(
                    commands,
                    WRITE_REG_2,
                    calParam->sensorAddress,
                    address,
                    data,
                    AR0231_REG_DATA_BYTES);
        if (!pWreg) {
            LOG_ERR("%s: Failed to set one exposure\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }

        address[0] = (AR0231_REG_DATA_FORMAT  >> 8) & 0xff;
        address[1] = AR0231_REG_DATA_FORMAT  & 0xff;

        data[0] = 0x0C;
        data[1] = 0x0C;

        pWreg = I2cSetupRegister(
                    commands,
                    WRITE_REG_2,
                    calParam->sensorAddress,
                    address,
                    data,
                    AR0231_REG_DATA_BYTES);
        if (!pWreg) {
            LOG_ERR("%s: Failed to set one exposure\n", __func__);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
        status = NVMEDIA_STATUS_OK;
    }

    LOG_INFO("%s:\n    one_exp: T%d\n", __func__, ar0231Properties->one_exp.uIntValue);

failed:
    return status;
}

static NvMediaStatus
ReadbackExpTime(
    Ar0231Properties *ar0231Properties,
    I2cCommands *commands,
    CalibrationParameters *calParam,
    SensorDefaultSet *sensorDefaultSet)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    RegisterSetup rgstrArray[8];
    NvU32 i = 0;
    NvU16 rows[4], pclks[4], rowsAdj, pclksAdj;
    float expT;

    memset(rgstrArray, 0, sizeof(RegisterSetup) * 8);

    // Read back exp time - rows
    rgstrArray[0].rgstrAddr = (AR0231_REG_EXP_TIME_T1_ROW >> 8) +
                              (AR0231_REG_EXP_TIME_T1_ROW << 8);
    rgstrArray[1].rgstrAddr = ((AR0231_REG_EXP_TIME_T1_ROW + 2) >> 8) +
                              ((AR0231_REG_EXP_TIME_T1_ROW + 2) << 8);
    rgstrArray[2].rgstrAddr = ((AR0231_REG_EXP_TIME_T1_ROW + 4) >> 8) +
                              ((AR0231_REG_EXP_TIME_T1_ROW + 4) << 8);
    rgstrArray[3].rgstrAddr = ((AR0231_REG_EXP_TIME_T1_ROW + 6) >> 8) +
                              ((AR0231_REG_EXP_TIME_T1_ROW + 6) << 8);
    // Read back fine exp time - pclks
    rgstrArray[4].rgstrAddr = (AR0231_REG_FINE_EXP_TIME_T1_PCLK >> 8) +
                              (AR0231_REG_FINE_EXP_TIME_T1_PCLK << 8);
    rgstrArray[5].rgstrAddr = ((AR0231_REG_FINE_EXP_TIME_T1_PCLK + 2) >> 8) +
                              ((AR0231_REG_FINE_EXP_TIME_T1_PCLK + 2) << 8);
    rgstrArray[6].rgstrAddr = ((AR0231_REG_FINE_EXP_TIME_T1_PCLK + 4) >> 8) +
                              ((AR0231_REG_FINE_EXP_TIME_T1_PCLK + 4) << 8);
    rgstrArray[7].rgstrAddr = ((AR0231_REG_FINE_EXP_TIME_T1_PCLK + 6) >> 8) +
                              ((AR0231_REG_FINE_EXP_TIME_T1_PCLK + 6) << 8);

    for (i = 0; i < 8; i++) {
        rgstrArray[i].rgstrVal = (NvU16 *) I2cSetupRegister(
                                     commands,
                                     READ_REG_2,
                                     calParam->sensorAddress,
                                     (NvU8 *)&rgstrArray[i].rgstrAddr,
                                     NULL,
                                     AR0231_REG_DATA_BYTES);
        if (!rgstrArray[i].rgstrVal) {
            LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                    rgstrArray[i].rgstrAddr);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }

    status = I2cProcessCommands(
                 commands,
                 I2C_READ,
                 calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    LOG_INFO("%s: Sensor frame size HTS x VTS: %d x %d\n",
             __func__, sensorDefaultSet->hts, sensorDefaultSet->vts);

    LOG_INFO("\n");
    LOG_INFO("%s: Sensor exposure time readback from register value: cit & fit\n", __func__);
    LOG_INFO("No need re-calculation exp time if in linear mode:\n");
    LOG_INFO("   expT3 = (cit3 - 1)(rows) + (fit3)(pclk) \n");
    LOG_INFO("   expT2 = (cit2 - 1)(rows) + (fit2)(pclk) \n");
    LOG_INFO("   expT1 = (cit1 - 1)(rows) + (fit1)(pclk) \n");
    for(i = 0; i < 4; i++) {
        rows[i] = ((*rgstrArray[i].rgstrVal >> 8) & 0xff) | ((*rgstrArray[i].rgstrVal << 8) & 0xff00);
        if(rows[i] < 1)
            rows[i] = 1;
        pclks[i] = ((*rgstrArray[i+4].rgstrVal >> 8) & 0xff) | ((*rgstrArray[i+4].rgstrVal << 8) & 0xff00);
        expT = ((rows[i] - 1) * sensorDefaultSet->hts + pclks[i]) * 1000000.0 / AR0231_1928X1208_PCLK;
        LOG_INFO("     T%d: %4d rows + %4d pclk; %.3f micro-second\n", i + 1, rows[i] - 1, pclks[i], expT);
    }

    if((ar0231Properties->n_exp.isUsed == NVMEDIA_FALSE) || (ar0231Properties->n_exp.uIntValue != 1)) {
        LOG_INFO("\n");
        LOG_INFO("Updated exposure time for T1/T2/T3/T4 for HDR mode after re-calculation:\n");
        LOG_INFO("   expT3 = (cit3 - 1)(rows) + fit3(pclk) \n");
        LOG_INFO("   expT2 = (cit2 + cit3 - 2)(rows) + (fit2 + fit3)(pclk) \n");
        LOG_INFO("   expT1 = (cit1 + cit2 + cit3 -3)(rows) + (fit1 + fit2 + fit3)(pclk) \n");
        for(i = 0; i < 4; i++) {
            if((i == 2) || (i ==3)) { //e3 or e4
                rowsAdj = rows[i] - 1;
                pclksAdj = pclks[i];
            } else if(i == 1) {
                rowsAdj = rows[1] + rows[2] - 2;
                pclksAdj = pclks[1] + pclks[2];
            } else if(i == 0) {
                rowsAdj = rows[0] + rows[1] + rows[2] - 3;
                pclksAdj = pclks[0] + pclks[1] + pclks[2];
            }
            expT = (rowsAdj * sensorDefaultSet->hts + pclksAdj) * 1000000.0 / AR0231_1928X1208_PCLK;
            LOG_INFO("   T%d: %4d rows + %4d pclk; %.3f micro-second\n", i + 1, rowsAdj, pclksAdj, expT);
        }
    }

failed:
    return status;
}

static NvMediaStatus
GetSensorSetting(
    I2cCommands *commands,
    CalibrationParameters *calParam,
    SensorDefaultSet *sensorDefaultSet)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    NvU32 i = 0;
    RegisterSetup rgstrArray[10];

    memset(rgstrArray, 0, sizeof(RegisterSetup) * 10);

    // Re-arrange address to match i2c read/write format (switch bytes)
    rgstrArray[0].rgstrAddr = (AR0231_REG_LINE_LENGTH_PCK >> 8) +
                              (AR0231_REG_LINE_LENGTH_PCK << 8); //HTS
    rgstrArray[1].rgstrAddr = (AR0231_REG_FRAME_LENGTH_LINE >> 8) +
                              (AR0231_REG_FRAME_LENGTH_LINE << 8); //VTS
    rgstrArray[2].rgstrAddr = (AR0231_REG_FINE_INTEG_TIME_T1 >> 8) +
                              (AR0231_REG_FINE_INTEG_TIME_T1 << 8);
    rgstrArray[3].rgstrAddr = (AR0231_REG_FINE_INTEG_TIME_T2 >> 8) +
                              (AR0231_REG_FINE_INTEG_TIME_T2 << 8);
    rgstrArray[4].rgstrAddr = (AR0231_REG_FINE_INTEG_TIME_T3 >> 8) +
                              (AR0231_REG_FINE_INTEG_TIME_T3 << 8);
    rgstrArray[5].rgstrAddr = (AR0231_REG_FINE_INTEG_TIME_T4 >> 8) +
                              (AR0231_REG_FINE_INTEG_TIME_T4 << 8);
    rgstrArray[6].rgstrAddr = (AR0231_REG_AGAIN >> 8) + (AR0231_REG_AGAIN << 8); // aGain
    rgstrArray[7].rgstrAddr = (AR0231_REG_CGAIN >> 8) + (AR0231_REG_CGAIN << 8); // cGain
    rgstrArray[8].rgstrAddr = (AR0231_REG_OP_MODE_CTRL >> 8) +
                              (AR0231_REG_OP_MODE_CTRL << 8); // opCtl
    rgstrArray[9].rgstrAddr = (AR0231_REG_DIGITAL_CTRL >> 8) +
                              (AR0231_REG_DIGITAL_CTRL << 8); // dCtl

    // Read registers needed to setup sensor settings
    for (i = 0; i < 10; i++) {
        rgstrArray[i].rgstrVal = (NvU16 *) I2cSetupRegister(
                                     commands,
                                     READ_REG_2,
                                     calParam->sensorAddress,
                                     (NvU8 *)&rgstrArray[i].rgstrAddr,
                                     NULL,
                                     AR0231_REG_DATA_BYTES);
        if (!rgstrArray[i].rgstrVal) {
            LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                    rgstrArray[i].rgstrAddr);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }

    status = I2cProcessCommands(
                 commands,
                 I2C_READ,
                 calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    // Re-arrange data to get correct 12c read 16-bit value (switch bytes)
    sensorDefaultSet->hts = (*rgstrArray[0].rgstrVal >> 8) + (*rgstrArray[0].rgstrVal << 8);
    sensorDefaultSet->vts = (*rgstrArray[1].rgstrVal >> 8) + (*rgstrArray[1].rgstrVal << 8);
    sensorDefaultSet->fineIntTime[AR0231_EXPOSURE_1] = (*rgstrArray[2].rgstrVal >> 8) | (*rgstrArray[2].rgstrVal << 8);
    sensorDefaultSet->fineIntTime[AR0231_EXPOSURE_2] = (*rgstrArray[3].rgstrVal >> 8) | (*rgstrArray[3].rgstrVal << 8);
    sensorDefaultSet->fineIntTime[AR0231_EXPOSURE_3] = (*rgstrArray[4].rgstrVal >> 8) | (*rgstrArray[4].rgstrVal << 8);
    sensorDefaultSet->fineIntTime[AR0231_EXPOSURE_4] = (*rgstrArray[5].rgstrVal >> 8) | (*rgstrArray[5].rgstrVal << 8);
    sensorDefaultSet->aGain = (*rgstrArray[6].rgstrVal >> 8) + (*rgstrArray[6].rgstrVal << 8);
    sensorDefaultSet->cGain = (*rgstrArray[7].rgstrVal >> 8) + (*rgstrArray[7].rgstrVal << 8);
    sensorDefaultSet->opCtl = (*rgstrArray[8].rgstrVal >> 8) + (*rgstrArray[8].rgstrVal << 8);
    sensorDefaultSet->dCtl  = (*rgstrArray[9].rgstrVal >> 8) + (*rgstrArray[9].rgstrVal << 8);

failed:
    return status;
}

static NvMediaStatus
CalibrateSensor(I2cCommands *commands, CalibrationParameters *calParam, SensorProperties *properties)
{
    NvMediaStatus status = NVMEDIA_STATUS_ERROR;
    SensorDefaultSet sensorDefaultSet;
    Ar0231Properties *ar0231Properties = (SensorProperties *)properties;

    // Get sensor default setting which needed for sensor calibartion
    status = GetSensorSetting(
                 commands,
                 calParam,
                 &sensorDefaultSet);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to read sensor registers\n", __func__);
        goto failed;
    }

    // Set maximum number of exposure if set by command
    if (ar0231Properties->max_exp.isUsed == NVMEDIA_TRUE) {
        status = SetMaxExp(
                    ar0231Properties,
                    commands,
                    calParam,
                    &sensorDefaultSet);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set maximum number of exposure\n", __func__);
            goto failed;
        }
    }

    // Set number of exposure if set by command
    if (ar0231Properties->n_exp.isUsed == NVMEDIA_TRUE) {
        status = SetNumExp(
                    ar0231Properties,
                    commands,
                    calParam,
                    &sensorDefaultSet);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set number of exposure\n", __func__);
            goto failed;
        }
    }

    // Set one exposure only if set by command
    if (ar0231Properties->one_exp.isUsed == NVMEDIA_TRUE) {
        status = SetOneExp(
                    ar0231Properties,
                    commands,
                    calParam,
                    &sensorDefaultSet);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set one exposure only\n", __func__);
            goto failed;
        }
    }

    // Set exposure time if set by comand
    if ((ar0231Properties->et[AR0231_EXPOSURE_1].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->et[AR0231_EXPOSURE_2].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->et[AR0231_EXPOSURE_3].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->et[AR0231_EXPOSURE_4].isUsed == NVMEDIA_TRUE)) {
        status = SetExposureTime(
                       ar0231Properties,
                       commands,
                       calParam,
                       &sensorDefaultSet);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set exposure time\n", __func__);
            goto failed;
        }
    }

    // Set WB gain if set by command
    if (ar0231Properties->wbRGGB.isUsed == NVMEDIA_TRUE) {
        status = SetWBGain(
                    ar0231Properties,
                    commands,
                    calParam);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set WB gains\n", __func__);
            goto failed;
        }
    }

    // Set conversion gain if set by command
    if ((ar0231Properties->cg[AR0231_EXPOSURE_1].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->cg[AR0231_EXPOSURE_2].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->cg[AR0231_EXPOSURE_3].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->cg[AR0231_EXPOSURE_4].isUsed == NVMEDIA_TRUE)) {
        status = SetConversionGain(
                    ar0231Properties,
                    commands,
                    calParam,
                    &sensorDefaultSet);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set conversion gains\n", __func__);
            goto failed;
        }
    }

    // Set analog gain if set by command
    if ((ar0231Properties->ag[AR0231_EXPOSURE_1].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->ag[AR0231_EXPOSURE_2].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->ag[AR0231_EXPOSURE_3].isUsed == NVMEDIA_TRUE) ||
        (ar0231Properties->ag[AR0231_EXPOSURE_4].isUsed == NVMEDIA_TRUE)) {
        status = SetAnalogGain(
                    ar0231Properties,
                    commands,
                    calParam,
                    &sensorDefaultSet);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set analog gains\n", __func__);
            goto failed;
        }
    }

    // Set digital gain if set by command
    if (ar0231Properties->dg.isUsed == NVMEDIA_TRUE) {
        status = SetDigitalGain(
                    ar0231Properties,
                    commands,
                    calParam);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set digital gains\n", __func__);
            goto failed;
        }
    }

    // Set knee LUT if set by command
    if (ar0231Properties->lkp.isUsed == NVMEDIA_TRUE) {
        status = SetKneeLut(
                    ar0231Properties,
                    commands,
                    calParam);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set user knee LUT\n", __func__);
            goto failed;
        }
    }

if(0) {
    // Process I2c setting
    status = I2cProcessCommands(
                 commands,
                 I2C_WRITE,
                 calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        goto failed;
    }

    // Wait long enough for readback registers being updated
    usleep(100000);

    // Readback exp time - fyi
    status = ReadbackExpTime(
                 ar0231Properties,
                 commands,
                 calParam,
                 &sensorDefaultSet);
}
failed:
    return status;
}

static NvMediaStatus
ProcessCmdline(int argc, char *argv[], SensorProperties *properties)
{
    NvMediaBool bLastArg = NVMEDIA_FALSE;
    NvMediaBool bDataAvailable = NVMEDIA_FALSE;
    int i;
    Ar0231Properties *ar0231Properties = (SensorProperties *)properties;

    for (i = 1; i < argc; i++) {
        // Check if this is the last argument
        bLastArg = ((argc - i) == 1);

        // Check if there is data available to be parsed
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if (!strcasecmp(argv[i], "-et1")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ar0231Properties->et[AR0231_EXPOSURE_1].isUsed = NVMEDIA_TRUE;
                ar0231Properties->et[AR0231_EXPOSURE_1].floatValue = atof(arg);
            } else {
                LOG_ERR("-et1 must be followed by an exposure time value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-et2")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ar0231Properties->et[AR0231_EXPOSURE_2].isUsed = NVMEDIA_TRUE;
                ar0231Properties->et[AR0231_EXPOSURE_2].floatValue = atof(arg);
            } else {
                LOG_ERR("-et2 must be followed by an exposure time value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-et3")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ar0231Properties->et[AR0231_EXPOSURE_3].isUsed = NVMEDIA_TRUE;
                ar0231Properties->et[AR0231_EXPOSURE_3].floatValue = atof(arg);
            } else {
                LOG_ERR("-et3 must be followed by an exposure time value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-et4")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ar0231Properties->et[AR0231_EXPOSURE_4].isUsed = NVMEDIA_TRUE;
                ar0231Properties->et[AR0231_EXPOSURE_4].floatValue = atof(arg);
            } else {
                LOG_ERR("-et4 must be followed by an exposure time value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-etR1")) {
            if (bDataAvailable) {
                char *arg = argv[++i]; // Share .isUsed with et1
                ar0231Properties->et[AR0231_EXPOSURE_1].isUsed = NVMEDIA_TRUE;
                ar0231Properties->etR[AR0231_EXPOSURE_1].floatValue = atof(arg);
            } else {
                LOG_ERR("-etR1 must be followed by an exposure time in rows\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-etR2")) {
            if (bDataAvailable) {
                char *arg = argv[++i]; // Share .isUsed with et2
                ar0231Properties->et[AR0231_EXPOSURE_2].isUsed = NVMEDIA_TRUE;
                ar0231Properties->etR[AR0231_EXPOSURE_2].floatValue = atof(arg);
            } else {
                LOG_ERR("-etR2 must be followed by an exposure time in rows\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-etR3")) {
            if (bDataAvailable) {
                char *arg = argv[++i]; // Share .isUsed with et3
                ar0231Properties->et[AR0231_EXPOSURE_3].isUsed = NVMEDIA_TRUE;
                ar0231Properties->etR[AR0231_EXPOSURE_3].floatValue = atof(arg);
            } else {
                LOG_ERR("-etR3 must be followed by an exposure time in rows\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-etR4")) {
            if (bDataAvailable) {
                char *arg = argv[++i]; // Share .isUsed with et4
                ar0231Properties->et[AR0231_EXPOSURE_4].isUsed = NVMEDIA_TRUE;
                ar0231Properties->etR[AR0231_EXPOSURE_4].floatValue = atof(arg);
            } else {
                LOG_ERR("-etR4 must be followed by an exposure time in rows\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-dg")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ar0231Properties->dg.isUsed = NVMEDIA_TRUE;
                ar0231Properties->dg.floatValue = atof(arg);
            } else {
                LOG_ERR("-dg must be followed by a digital gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-ag1")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                if ((atof(arg) > 8.0) || (atof(arg) < 0.125)){
                    LOG_ERR("-ag1 must have a value between 0-8.0\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->ag[AR0231_EXPOSURE_1].isUsed = NVMEDIA_TRUE;
                ar0231Properties->ag[AR0231_EXPOSURE_1].floatValue = atof(arg);
            } else {
                LOG_ERR("-ag1 must be followed by an analog gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-ag2")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                if ((atof(arg) > 8.0) || (atof(arg) < 0.125)){
                    LOG_ERR("-ag2 must have a value between 0.125-8.0\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->ag[AR0231_EXPOSURE_2].isUsed = NVMEDIA_TRUE;
                ar0231Properties->ag[AR0231_EXPOSURE_2].floatValue = atof(arg);
            } else {
                LOG_ERR("-ag2 must be followed by an analog gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-ag3")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                if ((atof(arg) > 8.0) || (atof(arg) < 0.125)){
                    LOG_ERR("-ag3 must have a value between 0.125-8.0\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->ag[AR0231_EXPOSURE_3].isUsed = NVMEDIA_TRUE;
                ar0231Properties->ag[AR0231_EXPOSURE_3].floatValue = atof(arg);
            } else {
                LOG_ERR("-ag3 must be followed by an analog gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-ag4")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                if ((atof(arg) > 8.0) || (atof(arg) < 0.125)){
                    LOG_ERR("-ag4 must have a value between 0.125-8.0\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->ag[AR0231_EXPOSURE_4].isUsed = NVMEDIA_TRUE;
                ar0231Properties->ag[AR0231_EXPOSURE_4].floatValue = atof(arg);
            } else {
                LOG_ERR("-ag4 must be followed by an analog gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-cg1")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 0:
                    case 1:
                        ar0231Properties->cg[AR0231_EXPOSURE_1].isUsed = NVMEDIA_TRUE;
                        ar0231Properties->cg[AR0231_EXPOSURE_1].uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid conversion gain value provided. Valid values are 0 or 1\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-cg1 must be followed by an conversion gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-cg2")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 0:
                    case 1:
                        ar0231Properties->cg[AR0231_EXPOSURE_2].isUsed = NVMEDIA_TRUE;
                        ar0231Properties->cg[AR0231_EXPOSURE_2].uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid conversion gain value provided. Valid values are 0 or 1\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-cg2 must be followed by an conversion gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-cg3")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 0:
                    case 1:
                        ar0231Properties->cg[AR0231_EXPOSURE_3].isUsed = NVMEDIA_TRUE;
                        ar0231Properties->cg[AR0231_EXPOSURE_3].uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid conversion gain value provided. Valid values are 0 or 1\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-cg3 must be followed by an conversion gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-cg4")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 0:
                    case 1:
                        ar0231Properties->cg[AR0231_EXPOSURE_4].isUsed = NVMEDIA_TRUE;
                        ar0231Properties->cg[AR0231_EXPOSURE_4].uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid conversion gain value provided. Valid values are 0 or 1\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-cg4 must be followed by an conversion gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-max_exp")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                if (atoi(arg) > 4 || atoi(arg) < 1) {
                    LOG_ERR("-max_exp must have a value between 1-4\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->max_exp.isUsed = NVMEDIA_TRUE;
                ar0231Properties->max_exp.uIntValue = atoi(arg);
            } else {
                LOG_ERR("-max_exp must be followed by max number of exposures\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-n_exp")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                if (atoi(arg) > 4 || atoi(arg) < 1) {
                    LOG_ERR("-n_exp must have a value between 1-4\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->n_exp.isUsed = NVMEDIA_TRUE;
                ar0231Properties->n_exp.uIntValue = atoi(arg);
            } else {
                LOG_ERR("-n_exp must be followed by number of exposures\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-one_exp")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                char preT;
                if ((sscanf(arg, "%c%d", &preT, &ar0231Properties->one_exp.uIntValue) != 2)) {
                    LOG_ERR("-one_exp must be followed by: T1, T2 or T3\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->one_exp.isUsed = NVMEDIA_TRUE;
            } else {
                LOG_ERR("-one_exp must be followed by: T1, T2 or T3\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-wbRGGB")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                if ((sscanf(arg, "[%f,%f,%f,%f]",
                           &ar0231Properties->wbRGGB.R,
                           &ar0231Properties->wbRGGB.G1,
                           &ar0231Properties->wbRGGB.G2,
                           &ar0231Properties->wbRGGB.B) != 4)) {
                    LOG_ERR("-wbRGGB must be followed by a white balance gain in R:G:G:B format\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->wbRGGB.isUsed = NVMEDIA_TRUE;
            } else {
                LOG_ERR("-wbRGGB must be followed by 4 gain values\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-lkp")) {  // Load 12 knee points
            if (bDataAvailable) {
                char *arg = argv[++i];
                if ((sscanf(arg, "[%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%X]",
                           &ar0231Properties->lkp.kp[0],
                           &ar0231Properties->lkp.kp[1],
                           &ar0231Properties->lkp.kp[2],
                           &ar0231Properties->lkp.kp[3],
                           &ar0231Properties->lkp.kp[4],
                           &ar0231Properties->lkp.kp[5],
                           &ar0231Properties->lkp.kp[6],
                           &ar0231Properties->lkp.kp[7],
                           &ar0231Properties->lkp.kp[8],
                           &ar0231Properties->lkp.kp[9],
                           &ar0231Properties->lkp.kp[10],
                           &ar0231Properties->lkp.kp[11]) != 12)) {
                    LOG_ERR("-lkp must be followed by 12 16-bit hex integers seprate with ,\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                ar0231Properties->lkp.isUsed = NVMEDIA_TRUE;
            } else {
                LOG_ERR("-lkp must be followed by 12 knee points integers\n");
                return NVMEDIA_STATUS_ERROR;
            }
        }
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
AppendOutputFilename(char *filename, SensorProperties *properties)
{
    char buf[5] = {0};
    Ar0231Properties *ar0231Properties = (SensorProperties *)properties;

    if (!filename)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    //exposureTime
    if (ar0231Properties->et[AR0231_EXPOSURE_1].isUsed) {
        if (ar0231Properties->etR[AR0231_EXPOSURE_1].floatValue > 0.0){
            strcat(filename, "_etR1_");
            sprintf(buf, "%.2f", ar0231Properties->etR[AR0231_EXPOSURE_1].floatValue);
            strcat(filename, buf);
        } else {
            strcat(filename, "_et1_");
            sprintf(buf, "%.2f", ar0231Properties->et[AR0231_EXPOSURE_1].floatValue);
            strcat(filename, buf);
        }
    }

    if (ar0231Properties->et[AR0231_EXPOSURE_2].isUsed) {
        if (ar0231Properties->etR[AR0231_EXPOSURE_2].floatValue > 0.0){
            strcat(filename, "_etR2_");
            sprintf(buf, "%.2f", ar0231Properties->etR[AR0231_EXPOSURE_2].floatValue);
            strcat(filename, buf);
        } else {
            strcat(filename, "_et2_");
            sprintf(buf, "%.2f", ar0231Properties->et[AR0231_EXPOSURE_2].floatValue);
            strcat(filename, buf);
        }
    }

    if (ar0231Properties->et[AR0231_EXPOSURE_3].isUsed) {
        if (ar0231Properties->etR[AR0231_EXPOSURE_3].floatValue > 0.0){
            strcat(filename, "_etR3_");
            sprintf(buf, "%.2f", ar0231Properties->etR[AR0231_EXPOSURE_3].floatValue);
            strcat(filename, buf);
        } else {
            strcat(filename, "_et3_");
            sprintf(buf, "%.2f", ar0231Properties->et[AR0231_EXPOSURE_3].floatValue);
            strcat(filename, buf);
        }
    }
    if (ar0231Properties->et[AR0231_EXPOSURE_4].isUsed) {
        if (ar0231Properties->etR[AR0231_EXPOSURE_4].floatValue > 0.0){
            strcat(filename, "_etR4_");
            sprintf(buf, "%.2f", ar0231Properties->etR[AR0231_EXPOSURE_4].floatValue);
            strcat(filename, buf);
        } else {
            strcat(filename, "_et4_");
            sprintf(buf, "%.2f", ar0231Properties->et[AR0231_EXPOSURE_4].floatValue);
            strcat(filename, buf);
        }
    }

    //digitalGain
    if (ar0231Properties->dg.isUsed) {
        strcat(filename, "_dg_");
        sprintf(buf, "%.2f", ar0231Properties->dg.floatValue);
        strcat(filename, buf);
    }

    //analogGain
    if (ar0231Properties->ag[AR0231_EXPOSURE_1].isUsed) {
        strcat(filename, "_ag1_");
        sprintf(buf, "%.2f", ar0231Properties->ag[AR0231_EXPOSURE_1].floatValue);
        strcat(filename, buf);
    }
    if (ar0231Properties->ag[AR0231_EXPOSURE_2].isUsed) {
        strcat(filename, "_ag2_");
        sprintf(buf, "%.2f", ar0231Properties->ag[AR0231_EXPOSURE_2].floatValue);
        strcat(filename, buf);
    }
    if (ar0231Properties->ag[AR0231_EXPOSURE_3].isUsed) {
        strcat(filename, "_ag3_");
        sprintf(buf, "%.2f", ar0231Properties->ag[AR0231_EXPOSURE_3].floatValue);
        strcat(filename, buf);
    }
    if (ar0231Properties->ag[AR0231_EXPOSURE_4].isUsed) {
        strcat(filename, "_ag4_");
        sprintf(buf, "%.2f", ar0231Properties->ag[AR0231_EXPOSURE_4].floatValue);
        strcat(filename, buf);
    }

    //conversionGain
    if (ar0231Properties->cg[AR0231_EXPOSURE_1].isUsed) {
        strcat(filename, "_cg1_");
        sprintf(buf, "%d", ar0231Properties->cg[AR0231_EXPOSURE_1].uIntValue);
        strcat(filename, buf);
    }
    if (ar0231Properties->cg[AR0231_EXPOSURE_2].isUsed) {
        strcat(filename, "_cg2_");
        sprintf(buf, "%d", ar0231Properties->cg[AR0231_EXPOSURE_2].uIntValue);
        strcat(filename, buf);
    }
    if (ar0231Properties->cg[AR0231_EXPOSURE_3].isUsed) {
        strcat(filename, "_cg3_");
        sprintf(buf, "%d", ar0231Properties->cg[AR0231_EXPOSURE_3].uIntValue);
        strcat(filename, buf);
    }
    if (ar0231Properties->cg[AR0231_EXPOSURE_4].isUsed) {
        strcat(filename, "_cg4_");
        sprintf(buf, "%d", ar0231Properties->cg[AR0231_EXPOSURE_4].uIntValue);
        strcat(filename, buf);
    }

    //max_exp
    if (ar0231Properties->max_exp.isUsed) {
        strcat(filename, "_max_exp_");
        sprintf(buf, "%d", ar0231Properties->max_exp.uIntValue);
        strcat(filename, buf);
    }

    // num_exp
    if (ar0231Properties->n_exp.isUsed) {
        strcat(filename, "_n_exp_");
        sprintf(buf, "%d", ar0231Properties->n_exp.uIntValue);
        strcat(filename, buf);
    }

    //one_exp
    if (ar0231Properties->one_exp.isUsed) {
        strcat(filename, "_one_exp_");
        sprintf(buf, "%d", ar0231Properties->one_exp.uIntValue);
        strcat(filename, buf);
    }

    return NVMEDIA_STATUS_OK;
}

static NvU32
ReadRawCompressionFormat(I2cCommands *settings, CalibrationParameters *calParam)
{
    NvU32 outputCompressionFormat = 0;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    RegisterSetup rgstrNumExp;

    //Readback compression format
    memset(&rgstrNumExp, 0, sizeof(RegisterSetup));
    rgstrNumExp.rgstrAddr = ((AR0231_REG_OP_MODE_CTRL >> 8) & 0x00ff) +
                             ((AR0231_REG_OP_MODE_CTRL << 8) & 0xff00);


    rgstrNumExp.rgstrVal = (NvU16 *)I2cSetupRegister(
                                 settings,
                                 READ_REG_2,
                                 calParam->sensorAddress,
                                 (NvU8 *)&rgstrNumExp.rgstrAddr,
                                 NULL,
                                 AR0231_REG_DATA_BYTES);
    if (!rgstrNumExp.rgstrVal) {
        LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                rgstrNumExp.rgstrAddr);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }

    status = I2cProcessCommands(
                 settings,
                 I2C_READ,
                 calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        status = NVMEDIA_STATUS_ERROR;
        goto failed;
    }


    outputCompressionFormat = (((((*rgstrNumExp.rgstrVal >> 8) & 0xff) |
                               ((*rgstrNumExp.rgstrVal << 8) & 0xff00)) & (0x03 << 2)) >> 2) & 0x03;
failed:
    return (outputCompressionFormat + 1);
}

static NvMediaStatus
WriteNvRawImage(
    I2cCommands *settings,
    CalibrationParameters *calParam,
    NvMediaImage *image,
    NvS32 frameNumber,
    char *outputFileName)
{

    FILE *file = NULL;
    NvRawFileHeaderChunkHandle *pNvrfHeader = NULL;
    NvRawFileCaptureChunkHandle *pNvrfCapture = NULL;
    NvRawFileCameraStateChunkHandle *pNvrfCameraState = NULL;
    NvRawFileSensorInfoChunkHandle *pNvrfSensorInfo = NULL;
    NvRawFileDataChunkHandle *pNvrfData = NULL;
    NvRawFileHDRChunkHandle *pNvrfHDR = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvRawFileErrorStatus nvrawStatus = NvRawFileError_Success;
    NvU32 numExposures = AR0231_NUM_EXPOSURES -1;
    NvU32 imageWidth = 0, imageHeight = 0;
    NvMediaImageSurfaceMap surfaceMap;
    NvRawSensorHDRInfo_v2 *sensorData = NULL;
    unsigned char *buff = NULL, *dstBuff[3] = {NULL};
    unsigned int dstPitches[3] = {1};
    NvU32 pitch = 0;
    NvU32 rawBytesPerPixel = 0, imageSize = 0;
    NvU32 compressionFormat = 0, nvrawCompressionFormat = 0, linearMode = 0;
    NvU32 i = 0, inputFormatWidthMultiplier = 1;
    NvF32 sensorGains[4] = {0};
    char fuseBuffer[AR0231_SENSOR_FUSE_ID_SIZE * 2 + 2];
    NvU32 BayerPhase[2][2] = { {NVRAWDUMP_BAYER_ORDERING_RGGB, NVRAWDUMP_BAYER_ORDERING_GRBG},
                               {NVRAWDUMP_BAYER_ORDERING_GBRG, NVRAWDUMP_BAYER_ORDERING_BGGR}};

    NvU32 embeddedLinesTop = 0, embeddedLinesBottom = 0;
    NvF32 LUTData[256] = {0.0};

    if (image == NULL) {
        LOG_DBG("%s: Error: Input image is null\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    } else {
        imageWidth = image->width;
        imageHeight = image->height;
    }

    pNvrfHeader = NvRawFileHeaderChunkCreate();
    if (pNvrfHeader == NULL)
    {
        LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkCreate failed\n");
        status = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    pNvrfCapture = NvRawFileCaptureChunkCreate();
    if (pNvrfCapture == NULL)
    {
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkCreate failed\n");
        status = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    pNvrfCameraState = NvRawFileCameraStateChunkCreate();
    if (pNvrfCameraState == NULL)
    {
        LOG_ERR("WriteNvRawImage: NvRawFileCameraStateChunkCreate failed\n");
        status = NVMEDIA_STATUS_ERROR;
        goto done;
    }


    pNvrfSensorInfo = NvRawFileSensorInfoChunkCreate();
    if (pNvrfSensorInfo == NULL)
    {
        LOG_ERR("WriteNvRawImage: NvRawFileSensorInfoChunkCreate failed\n");
        status = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    compressionFormat = ReadRawCompressionFormat(settings, calParam);
    switch (compressionFormat) {
        case 1:
            nvrawCompressionFormat = NvRawCompressionFormat_12BitLinear;
            linearMode = 0;
            numExposures = 1;
            rawBytesPerPixel = 2;
            inputFormatWidthMultiplier = 1;
            nvrawStatus = NvRawFileHeaderChunkSetBitsPerSample(pNvrfHeader,12);
            break;
        case 3:
            nvrawCompressionFormat = NvRawCompressionFormat_12BitCombinedCompressed;
            linearMode = 0;
            numExposures = 3;
            rawBytesPerPixel = 2;
            inputFormatWidthMultiplier = 1;
            nvrawStatus = NvRawFileHeaderChunkSetBitsPerSample(pNvrfHeader,12);
            break;
        default:
           LOG_ERR("WriteNvRawImage: Unsupported nvraw compression mode\n");
           nvrawStatus = NvRawFileError_BadParameter;
           break;
    }

    if (nvrawStatus != NvRawFileError_Success){
        status = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    sensorData = (NvRawSensorHDRInfo_v2*)calloc(numExposures, sizeof(NvRawSensorHDRInfo_v2));
    //Get Image EmbeddedData
    if (frameNumber >= 0){
        status = ReadSensorExposureInfo(settings, calParam, sensorData, numExposures);
        if (status != NVMEDIA_STATUS_OK){
            LOG_ERR("WriteNvRawImage: ReadSensorExposureInfo failed\n");
            goto done;
        }

        status = ReadSensorWbGainsInfo(settings, calParam, sensorData, numExposures);
        if (status != NVMEDIA_STATUS_OK){
            LOG_ERR("WriteNvRawImage: ReadSensorWbGainsInfo failed\n");
            goto done;
        }

        status = ReadSensorLUTInfo(settings, calParam, sensorData, LUTData);
        if (status != NVMEDIA_STATUS_OK){
            LOG_ERR("WriteNvRawImage: ReadSensorLUTInfo failed\n");
            goto done;
        }

        status = ReadEmbeddedLinesInfo(settings, calParam, &embeddedLinesTop, &embeddedLinesBottom);
        if (status != NVMEDIA_STATUS_OK){
            LOG_ERR("WriteNvRawImage: ReadEmbeddedLinesInfo failed\n");
            goto done;
        }
    }

    if (nvrawStatus != NvRawFileError_Success){
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkSetBitsPerSample failed \n");
        goto done;
    }

    if (NvRawFileHeaderChunkSetSamplesPerPixel(pNvrfHeader,inputFormatWidthMultiplier) != NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkSetSamplesPerPixel failed \n");
        goto done;
    }

    if (NvRawFileHeaderChunkSetNumImages(pNvrfHeader,1)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkSetNumImages failed \n");
        goto done;
    }

    if (NvRawFileHeaderChunkSetProcessingFlags(pNvrfHeader,0)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkSetProcessingFlags failed \n");
        goto done;
    }

    if (NvRawFileHeaderChunkSetDataFormat(pNvrfHeader, BayerPhase[0][1])!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkSetDataFormat failed \n");
        goto done;
    }

    if (NvRawFileHeaderChunkSetImageWidth(pNvrfHeader, imageWidth)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkSetImageWidth failed \n");
        goto done;
    }

    if (NvRawFileHeaderChunkSetImageHeight(pNvrfHeader, imageHeight)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkSetImageHeight failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetExposureTime(pNvrfCapture,sensorData[linearMode].exposure.exposureTime)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetExposureTime failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetISO(pNvrfCapture,sensorData[linearMode].exposure.analogGain * 100)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetISO failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetFocusPosition(pNvrfCapture,0.0)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetFocusPosition failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetFlashPower(pNvrfCapture, 0.0)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetFlashPower failed \n");
        goto done;
    }

    for (i = 0; i< 4; i++) {
        sensorGains[i] = sensorData[linearMode].exposure.digitalGain * sensorData[linearMode].wbGain.value[i];
    }

    if (NvRawFileCaptureChunkSetSensorGain(pNvrfCapture, sensorGains)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetSensorGain failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetIspDigitalGain(pNvrfCapture, 1.0)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetIspDigitalGain failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetOutputDataFormat(pNvrfCapture, nvrawCompressionFormat)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetOutputDataFormat failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetPixelEndianness(pNvrfCapture, NV_TRUE)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetPixelEndianness failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetEmbeddedLineCountTop(pNvrfCapture, embeddedLinesTop)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetEmbeddedLineCountTop failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetEmbeddedLineCountBottom(pNvrfCapture,embeddedLinesBottom)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetEmbeddedLineCountBottom failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetLux(pNvrfCapture, 0.0)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetLux failed \n");
        goto done;
    }

    if (nvrawCompressionFormat == NvRawCompressionFormat_12BitCombinedCompressed ||
       nvrawCompressionFormat == NvRawCompressionFormat_12BitCombinedCompressedExtended){
        if (NvRawFileCaptureChunkSetLut(pNvrfCapture, (NvU8*)LUTData, sizeof(LUTData))!= NvRawFileError_Success) {
            status = NVMEDIA_STATUS_ERROR;
            LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetLut failed \n");
            goto done;
        }
    }

    //set Fuse-ID
    fuseBuffer[0] = '\0';
    if (NvRawFileSensorInfoChunkSetFuse(pNvrfSensorInfo, (const char *)fuseBuffer)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileSensorInfoChunkSetFuse failed \n");
        goto done;
    }

    if (numExposures > 1) {
        pNvrfHDR = NvRawFileHDRChunkCreate();
        if (NvRawFileHDRChunkSetNumberOfExposures(pNvrfHDR, numExposures)!= NvRawFileError_Success){
            status = NVMEDIA_STATUS_ERROR;
            LOG_ERR("WriteNvRawImage: NvRawFileHDRChunkSetNumberOfExposures failed \n");
            goto done;
        }

        if (NvRawFileHDRChunkSetReadoutScheme(pNvrfHDR, "A\nA\nA\nA")!= NvRawFileError_Success){
            LOG_ERR("WriteNvRawImage: NvRawFileHDRChunkSetReadoutScheme failed \n");
            goto done;
        }

        if (NvRawFileHDRChunkSetExposureInfo_v2(pNvrfHDR, sensorData)!= NvRawFileError_Success){
            LOG_ERR("WriteNvRawImage: NvRawFileHDRChunkSetExposureInfo_v2 failed \n");
            goto done;
        }
    }

    pitch = imageWidth * rawBytesPerPixel;
    imageSize = pitch * imageHeight;
    imageSize += image->embeddedDataTopSize;
    imageSize += image->embeddedDataBottomSize;

    pNvrfData = NvRawFileDataChunkCreate(imageSize, NV_FALSE);
    if (pNvrfData == NULL) {
        LOG_ERR("WriteNvRawImage: NvRawFileDataChunkCreate failed\n");
        status = NVMEDIA_STATUS_OUT_OF_MEMORY;
        goto done;
    }

    // Write to an output file
    file = fopen(outputFileName, "wb");
    if (file == NULL) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: Failed to open file\n");
        goto done;
    }

    if (image->type == NvMediaSurfaceType_Image_RAW) {
        // write full chunks and the partial data chunk.
        if ( NvRawFileHeaderChunkFileWrite(pNvrfHeader, file)!= NvRawFileError_Success){
            LOG_ERR("WriteNvRawImage: NvRawFileHeaderChunkFileWrite failed\n");
            status = NVMEDIA_STATUS_ERROR;
            goto done;
        }

        if (NvRawFileCaptureChunkFileWrite(pNvrfCapture, file)!= NvRawFileError_Success){
            LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkFileWrite failed\n");
            status = NVMEDIA_STATUS_ERROR;
            goto done;
        }

        if (NvRawFileCameraStateChunkFileWrite(pNvrfCameraState, file)!= NvRawFileError_Success){
            LOG_ERR("WriteNvRawImage: NvRawFileCameraStateChunkFileWrite failed\n");
            status = NVMEDIA_STATUS_ERROR;
            goto done;
        }

        if (NvRawFileSensorInfoChunkFileWrite(pNvrfSensorInfo, file)!= NvRawFileError_Success){
            LOG_ERR("WriteNvRawImage: NvRawFileSensorInfoChunkFileWrite failed\n");
            status = NVMEDIA_STATUS_ERROR;
            goto done;
        }

        if (pNvrfHDR != NULL) {
            if (NvRawFileHDRChunkFileWrite(pNvrfHDR, file)!= NvRawFileError_Success) {
                LOG_ERR("WriteNvRawImage: NvRawFileHDRChunkFileWrite failed\n");
                status = NVMEDIA_STATUS_ERROR;
                goto done;
            }
        }

        if (NvRawFileDataChunkFileWrite(pNvrfData, file, NV_FALSE)!= NvRawFileError_Success){
            status = NVMEDIA_STATUS_ERROR;
            LOG_ERR("WriteNvRawImage: NvRawFileDataChunkFileWrite failed\n");
            goto done;
        }

    }

    if (NvMediaImageLock(image, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK){
        LOG_ERR("WriteNvRawImage: NvMediaImageLock failed\n");
        return NVMEDIA_STATUS_ERROR;
    }

    if (!(buff = malloc(imageSize))) {
        LOG_ERR("WriteNvRawImage: Out of memory\n");
        fclose(file);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    dstBuff[0] = buff;
    dstPitches[0] = pitch;
    status = NvMediaImageGetBits(image, NULL, (void **)dstBuff, dstPitches);
    NvMediaImageUnlock(image);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("WriteNvRawImage: NvMediaVideoSurfaceGetBits() failed\n");
        goto done;
    }

    // re-align 12bit (S1.14 format) pixel data
    // to LSB (right shift by 2) for display/nvraw-viewer
    for (i = 0; i < (imageSize/2); i++) {
        // LSB
        buff[2*i] = buff[2*i] >> 2;
        buff[2*i] |= (buff[(2*i)+1] & 0x03) << 6;
        //MSB
        buff[(2*i)+1] = (buff[(2*i)+1] >> 2) & 0x0F;
    }

    if (fwrite(dstBuff[0], imageSize, 1, file) != 1) {
        LOG_ERR("WriteNvRawImage: file write failed\n");
        goto done;
    }


done:
    NvRawFileHeaderChunkDelete(pNvrfHeader);
    NvRawFileCaptureChunkDelete(pNvrfCapture);
    NvRawFileCameraStateChunkDelete(pNvrfCameraState);
    NvRawFileSensorInfoChunkDelete(pNvrfSensorInfo);
    NvRawFileHDRChunkDelete(pNvrfHDR);
    NvRawFileDataChunkDelete(pNvrfData);

    if (file != NULL){
        fclose(file);
    }

    if (sensorData)
        free(sensorData);

    return status;
}

static void
PrintSensorCaliUsage(void)
{
    LOG_MSG("===========================================================\n");
    LOG_MSG("===            AR0231 calibration commands              ===\n");
    LOG_MSG("-et1 [time]       Exposure time in microseconds (float) for T1\n");
    LOG_MSG("-et2 [time]       Exposure time in microseconds (float) for T2\n");
    LOG_MSG("-et3 [time]       Exposure time in microseconds (float) for T3\n");
    LOG_MSG("-et4 [time]       Exposure time in microseconds (float) for T4\n");
    LOG_MSG("-etR1 [time]      Exposure time in rows (float) for T1\n");
    LOG_MSG("-etR2 [time]      Exposure time in rows (float) for T2\n");
    LOG_MSG("-etR3 [time]      Exposure time in rows (float) for T3\n");
    LOG_MSG("-etR4 [time]      Exposure time in rows (float) for T4\n");
    LOG_MSG("-dg [gain]        Digital gain (float) for all exp chanels\n");
    LOG_MSG("-ag1 [gain]       Analog gain (float) for T1\n");
    LOG_MSG("                  Valid values for AR0231 are 0.125, 0.25, 0.375, 0.429, 0.5, 0.667, 0.8, 1, 1.25, 1.5, 2, 2.333, 3.5, 4, or 8\n");
    LOG_MSG("-ag2 [gain]       Analog gain (float) for T2\n");
    LOG_MSG("                  Valid values for AR0231 are 0.125, 0.25, 0.375, 0.429, 0.5, 0.667, 0.8, 1, 1.25, 1.5, 2, 2.333, 3.5, 4, or 8\n");
    LOG_MSG("-ag3 [gain]       Analog gain (float) for T3\n");
    LOG_MSG("                  Valid values for AR0231 are 0.125, 0.25, 0.375, 0.429, 0.5, 0.667, 0.8, 1, 1.25, 1.5, 2, 2.333, 3.5, 4, or 8\n");
    LOG_MSG("-ag4 [gain]       Analog gain (float) for T4\n");
    LOG_MSG("                  Valid values for AR0231 are 0.125, 0.25, 0.375, 0.429, 0.5, 0.667, 0.8, 1, 1.25, 1.5, 2, 2.333, 3.5, 4, or 8\n");
    LOG_MSG("-cgl [gain]       Conversion gain (int) for T1\n");
    LOG_MSG("                  Valid values are 0 or 1\n");
    LOG_MSG("-cg2 [gain]       Conversion gain (int) for T2\n");
    LOG_MSG("                  Valid values are 0 or 1\n");
    LOG_MSG("-cg3 [gain]       Conversion gain (int) for T3\n");
    LOG_MSG("                  Valid values are 0 or 1\n");
    LOG_MSG("-cg4 [gain]       Conversion gain (int) for T4\n");
    LOG_MSG("                  Valid values are 0 or 1\n");
    LOG_MSG("-max_exp [n]      Maximum number of exposures (int) for HDR mode\n");
    LOG_MSG("                  Valid values are 1, 2, 3, or 4\n");
    LOG_MSG("-n_exp [n]        Number of exposures (int) for HDR mode\n");
    LOG_MSG("                  Valid values are 1, 2, 3, or 4\n");
    LOG_MSG("-one_exp [Tn]     One exposure only for non-HDR\n");
    LOG_MSG("                  Valid values are T1, T2, or T3\n");
    LOG_MSG("-wbRGGB [[g1,g2,g3,g4]]  4 White balance gain (float) in form of R/Gr/Gb/B pattern\n");
    LOG_MSG("                         Valid values are >= 1.0, group inside of []\n");
    LOG_MSG("-lkp [[p1,...,p12]]      12 keen point Look up table, 16-bit hex (int) for user defined companding curve\n");
    LOG_MSG("                         Valid values are from 0x0 to  0xffff, group inside of []\n");
}

static SensorInfo ar0231Info = {
    .name = "ar0231",
    .supportedArgs = ar0231SupportedArgs,
    .numSupportedArgs = sizeof(ar0231SupportedArgs)/sizeof(ar0231SupportedArgs[0]),
    .sizeOfSensorProperties = sizeof(Ar0231Properties),
    .CalibrateSensor = CalibrateSensor,
    .ProcessCmdline = ProcessCmdline,
    .AppendOutputFilename = AppendOutputFilename,
    .WriteNvRawImage = WriteNvRawImage,
    .PrintSensorCaliUsage = PrintSensorCaliUsage,
};

SensorInfo*
GetSensorInfo_ar0231(void) {
    return &ar0231Info;
}
