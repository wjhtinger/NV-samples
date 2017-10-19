/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "cmdline.h"
#include "sensorInfo_ov10640.h"

typedef struct {
    CmdlineParameter             etl;
    CmdlineParameter             ets;
    CmdlineParameter             etvs;
    CmdlineParameter             dgl;
    CmdlineParameter             dgs;
    CmdlineParameter             dgvs;
    CmdlineParameter             agl;
    CmdlineParameter             ags;
    CmdlineParameter             agvs;
    CmdlineParameter             cgl;
    CmdlineParameter             cgvs;
} Ov10640Properties;

static char *ov10640SupportedArgs[] = {
    "-etl",
    "-ets",
    "-etvs",
    "-dgl",
    "-dgs",
    "-dgvs",
    "-agl",
    "-ags",
    "-agvs",
    "-cgl",
    "-cgvs"
};

static NvMediaStatus
ReadSensorExposureInfo(I2cCommands *settings,
                       CalibrationParameters *calParam,
                       NvRawSensorHDRInfo_v2 * sensorInfo)
{
    I2cHandle handle = NULL;
    NvMediaStatus status;
    NvU8 address[2];
    unsigned char buffer[2];

    NvU8 *analogGain = NULL;
    NvU8 *htsH = NULL;
    NvU8 *htsL = NULL;
    NvU8 *sclkPllPre = NULL;
    NvU8 *sclkPllMult = NULL;
    NvU8 *sclkPllPost = NULL;
    NvU32 i = 0, temp = 0;
    RegisterSetup rgstrArray[OV10640_NUM_REG_TO_READ];

    NvF32 sensorAnalogGain[OV10640_MAX_EXPOSURES] = {1.0};
    NvF32 sensorDigitalGain[OV10640_MAX_EXPOSURES] = {1.0};
    NvF32 sensorConversionGain[OV10640_MAX_EXPOSURES] = {1.0};

    NvU32 hts = 0;
    NvU32 nbLinesL = 0;
    NvU32 nbLinesS = 0;
    NvU32 nbLinesVS = 0;
    NvU32 sclk = 0;
    unsigned int digitalGain;
    unsigned int aGain;
    float sclkPrePll[8] = {1, 1.5, 2, 3, 4, 5, 6, 7};

    memset(rgstrArray, 0, sizeof(RegisterSetup)*8);
    rgstrArray[0].rgstrAddr = OV10640_CG_AGAIN;
    rgstrArray[1].rgstrAddr = OV10640_VTS_H;
    rgstrArray[2].rgstrAddr = OV10640_VTS_L;
    rgstrArray[3].rgstrAddr = OV10640_HTS_H;
    rgstrArray[4].rgstrAddr = OV10640_HTS_L;
    rgstrArray[5].rgstrAddr = OV10640_SCLK_PLL_PRE;
    rgstrArray[6].rgstrAddr = OV10640_SCLK_PLL_MULT;
    rgstrArray[7].rgstrAddr = OV10640_SCLK_PLL_POST;

    // Read registers needed to setup sensor settings
    for (i = 0; i < OV10640_NUM_REG_TO_READ; i++) {
        // Re-arrage address to match i2c read/write format (switch bytes)
        temp = rgstrArray[i].rgstrAddr;
        rgstrArray[i].rgstrAddr = (temp >> 8) & 0xFF;
        rgstrArray[i].rgstrAddr |= (temp & 0xFF) << 8;

        rgstrArray[i].rgstrVal = (NvU16 *) I2cSetupRegister(settings,
                             READ_REG_2,
                             calParam->sensorAddress,
                             (NvU8 *)&rgstrArray[i].rgstrAddr,
                             NULL,
                             OV10640_DATA_REG_LEN);
        if (!rgstrArray[i].rgstrVal) {
            LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                    rgstrArray[i].rgstrAddr);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }
    status = I2cProcessCommands(settings,
                                I2C_READ,
                                calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        goto failed;
    }

    analogGain = (NvU8 *) rgstrArray[0].rgstrVal;
    htsH = (NvU8 *) rgstrArray[3].rgstrVal;
    htsL = (NvU8 *) rgstrArray[4].rgstrVal;
    sclkPllPre = (NvU8 *) rgstrArray[5].rgstrVal;
    sclkPllMult = (NvU8 *) rgstrArray[6].rgstrVal;
    sclkPllPost = (NvU8 *) rgstrArray[7].rgstrVal;

    hts = ((*htsH << 8) & 0xFFFF) | (*htsL & 0xFFFF);

    sclk = calParam->crystalFrequency / sclkPrePll[(*sclkPllPre & 0x7)] *
           (*sclkPllMult) / ((*sclkPllPost & 0xF) + 1);

    testutil_i2c_open(calParam->i2cDevice, &handle);

    // Read exposureTime - L
    address[0] = (OV10640_EXPO_L_L >> 8) & 0xFF;
    address[1] = OV10640_EXPO_L_L & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[0], sizeof(unsigned char));

    address[0] = (OV10640_EXPO_L_H >> 8) & 0xFF;
    address[1] = OV10640_EXPO_L_H & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[1], sizeof(unsigned char));

    nbLinesL = 0x00FF & buffer[1];
    nbLinesL  = (nbLinesL << 8) | buffer[0];
    sensorInfo[0].exposure.exposureTime = (nbLinesL * (hts/sclk))/1000000.0;  //seconds

    // Read exposureTime - S
    address[0] = (OV10640_EXPO_S_L >> 8) & 0xFF;
    address[1] = OV10640_EXPO_S_L & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[0], sizeof(unsigned char));

    address[0] = (OV10640_EXPO_S_H >> 8) & 0xFF;
    address[1] = OV10640_EXPO_S_H & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[1], sizeof(unsigned char));

    nbLinesS = 0x00FF & buffer[1];
    nbLinesS  = (nbLinesS << 8) | buffer[0];
    sensorInfo[1].exposure.exposureTime = (nbLinesS * (hts/sclk))/1000000.0;

    // Read exposureTime - VS
    address[0] = (OV10640_EXPO_VS >> 8) & 0xFF;
    address[1] = OV10640_EXPO_VS & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[0], sizeof(unsigned char));

    nbLinesVS = 0x00FF & buffer[0];
    sensorInfo[2].exposure.exposureTime = (nbLinesVS * ((hts/sclk)/32.0))/1000000.0;

    // Read digitalGain - L
    address[0] = (OV10640_DGAIN_L_L >> 8) & 0xFF;
    address[1] = OV10640_DGAIN_L_L & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[0], sizeof(unsigned char));

    address[0] = (OV10640_DGAIN_L_H >> 8) & 0xFF;
    address[1] = OV10640_DGAIN_L_H & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[1], sizeof(unsigned char));

    digitalGain = 0x00FF & buffer[1];
    digitalGain = (digitalGain << 8) | buffer[0];
    sensorDigitalGain[0] = (float)(digitalGain/256.0);

    // Read digitalGain - S
    address[0] = (OV10640_DGAIN_S_L >> 8) & 0xFF;
    address[1] = OV10640_DGAIN_S_L & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[0], sizeof(unsigned char));

    address[0] = (OV10640_DGAIN_S_H >> 8) & 0xFF;
    address[1] = OV10640_DGAIN_S_H & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[1], sizeof(unsigned char));

    digitalGain = 0x00FF & buffer[1];
    digitalGain = (digitalGain << 8) | buffer[0];
    sensorDigitalGain[1] = (float)(digitalGain/256.0);

    // Read digitalGain - VS
    address[0] = (OV10640_DGAIN_VS_L >> 8) & 0xFF;
    address[1] = OV10640_DGAIN_VS_L & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[0], sizeof(unsigned char));

    address[0] = (OV10640_DGAIN_VS_H >> 8) & 0xFF;
    address[1] = OV10640_DGAIN_VS_H & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer[1], sizeof(unsigned char));

    digitalGain = 0x00FF & buffer[1];
    digitalGain = (digitalGain << 8) | buffer[0];
    sensorDigitalGain[2] = (float)(digitalGain/256.0);

    // Read analogGain
    for (i = 0; i < OV10640_MAX_EXPOSURES; i++) {
        aGain = ((*analogGain) & (0x3 << 2*i)) >> (2*i);
        sensorAnalogGain[i] = (float)(1 << aGain);
    }

    // Read converion Gain
    sensorConversionGain[0]= ((((*analogGain) & (0x1 << 6)) >> 6) & 0x1) ? 2.57:1;
    sensorConversionGain[1] = 1.0;
    sensorConversionGain[2] = ((((*analogGain) & (0x1 << 7)) >> 7) & 0x1) ? 2.57:1;

    // Populate analog and digital Gain as follows
    // to match nvraw generated from IPP-capture
    // nvraw_analogGain = 1.0 ,
    // nvraw_digitalGain = sensor_analogGain * sensor_digitalGain * sensor_conversionGain
    for (i = 0; i < OV10640_MAX_EXPOSURES; i++) {
        sensorInfo[i].exposure.analogGain = 1.0;
        sensorInfo[i].exposure.digitalGain = sensorAnalogGain[i] * sensorDigitalGain[i] * sensorConversionGain[i];
    }

    if (handle)
        testutil_i2c_close(handle);

return NVMEDIA_STATUS_OK;

failed:
    return status;

}

static NvMediaStatus
ReadSensorWbGainsInfo(I2cCommands *settings,
                      CalibrationParameters *calParam,
                      NvRawSensorHDRInfo_v2 * sensorInfo)
{
    I2cHandle handle = NULL;
    int i, j, startAddress;
    NvU8 address[2];
    unsigned char buffer[2];
    float value;
    static int indexMap[4] = {
       1, // R
       0, // GR
       3, // GB
       2  // B
    };
    int index;

    testutil_i2c_open(calParam->i2cDevice, &handle);

    startAddress = OV10640_R_GAIN_L_I_H;
    for (i = 0; i < OV10640_MAX_EXPOSURES; i++) {
        for (j = 0; j < 4; j++) {
            index = indexMap[j];

            // Read exposureTime - L
            address[0] = (startAddress >> 8) & 0xFF;
            address[1] = startAddress & 0xFF;
            testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2 * sizeof(NvU8), &buffer[0], sizeof(unsigned char));

            address[0] = ((startAddress+1) >> 8) & 0xFF;
            address[1] = (startAddress+1) & 0xFF;
            testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2 * sizeof(NvU8), &buffer[1], sizeof(unsigned char));

            value = (float)buffer[0] + buffer[1]/256.0f;
            sensorInfo[i].wbGain.value[index] = value;
            startAddress += 2;
        }
    }

    if (handle)
        testutil_i2c_close(handle);

    return NVMEDIA_STATUS_OK;
}

static NvU32
ReadRawCompressionFormat(CalibrationParameters *calParam)
{
    I2cHandle handle = NULL;
    NvU8 address[2];
    unsigned char buffer;
    NvU32 outputCompressionFormat = 0;

    testutil_i2c_open(calParam->i2cDevice, &handle);
    address[0] = (OV10640_INTERFACE_CTRL >> 8) & 0xFF;
    address[1] = OV10640_INTERFACE_CTRL & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2 * sizeof(NvU8), &buffer, sizeof(unsigned char));
    outputCompressionFormat = 0x0007 & buffer;

    if (handle)
        testutil_i2c_close(handle);

    return outputCompressionFormat;
}

static NvU32
ReadRawCompressionMode(CalibrationParameters *calParam)
{
    I2cHandle handle = NULL;
    NvU8 address[2];
    unsigned char buffer;
    NvU32 outputCompressionMode = 0;

    testutil_i2c_open(calParam->i2cDevice, &handle);
    address[0] = (OV10640_COMBINATN_MODE >> 8) & 0xFF;
    address[1] = OV10640_COMBINATN_MODE & 0xFF;
    testutil_i2c_read_subaddr(handle, calParam->sensorAddress, address, 2*sizeof(NvU8), &buffer, sizeof(unsigned char));
    outputCompressionMode = 0x0001 & buffer;

    if (handle)
        testutil_i2c_close(handle);

    return outputCompressionMode;
}

static NvMediaStatus
CalculateExposureTimes(Ov10640Properties *ov10640Properties,
                       I2cCommands *commands,
                       CalibrationParameters *calParam,
                       NvU8 *vtsH,
                       NvU8 *vtsL,
                       NvU8 *htsH,
                       NvU8 *htsL,
                       NvU8 *sclkPllPre,
                       NvU8 *sclkPllMult,
                       NvU8 *sclkPllPost)
{
    NvU32 vts = 0;
    NvU32 hts = 0;
    NvU32 nbLines = 0;
    NvU32 nbLinesL = 0;
    NvU32 nbLinesS = 0;
    NvU32 nbLinesVS = 0;
    NvU32 sclk = 0;
    NvU8 buf = 0;
    NvU8 address[2];

    NvU8 *expoLH = NULL;
    NvU8 *expoLL = NULL;
    NvU8 *expoSH = NULL;
    NvU8 *expoSL = NULL;
    NvU8 *expoVS = NULL;

    float sclkPrePll[8] = {1, 1.5, 2, 3, 4, 5, 6, 7};

    vts = ((*vtsH << 8) & 0xFFFF) | (*vtsL & 0xFFFF);
    hts = ((*htsH << 8) & 0xFFFF) | (*htsL & 0xFFFF);

    sclk = calParam->crystalFrequency / sclkPrePll[(*sclkPllPre & 0x7)] *
           (*sclkPllMult) / ((*sclkPllPost & 0xF) + 1);

    if (ov10640Properties->etl.isUsed) {
        nbLinesL = (int)(ov10640Properties->etl.floatValue * sclk / hts);

        buf = (nbLinesL >> 8) & 0xFF;
        address[0] = (OV10640_EXPO_L_H >> 8) & 0xFF;
        address[1] = OV10640_EXPO_L_H & 0xFF;

        expoLH = I2cSetupRegister(commands,
                                  WRITE_REG_2,
                                  calParam->sensorAddress,
                                  address,
                                  &buf,
                                  OV10640_DATA_REG_LEN);
        if (!expoLH) {
            LOG_ERR("%s: Failed to setup register EXPO_L_H\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        buf = nbLinesL & 0xFF;
        address[0] = (OV10640_EXPO_L_L >> 8) & 0xFF;
        address[1] = OV10640_EXPO_L_L & 0xFF;

        expoLL = I2cSetupRegister(commands,
                                  WRITE_REG_2,
                                  calParam->sensorAddress,
                                  address,
                                  &buf,
                                  OV10640_DATA_REG_LEN);
        if (!expoLL) {
            LOG_ERR("%s: Failed to setup register EXPO_L_L\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    if (ov10640Properties->ets.isUsed) {
        nbLinesS = (int)(ov10640Properties->ets.floatValue * sclk / hts);

        buf = (nbLinesS >> 8) & 0xFF;
        address[0] = (OV10640_EXPO_S_H >> 8) & 0xFF;
        address[1] = OV10640_EXPO_S_H & 0xFF;

        expoSH = I2cSetupRegister(commands,
                                  WRITE_REG_2,
                                  calParam->sensorAddress,
                                  address,
                                  &buf,
                                  OV10640_DATA_REG_LEN);
        if (!expoSH) {
            LOG_ERR("%s: Failed to setup register EXPO_S_H\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        buf = nbLinesS & 0xFF;
        address[0] = (OV10640_EXPO_S_L >> 8) & 0xFF;
        address[1] = OV10640_EXPO_S_L & 0xFF;

        expoSL = I2cSetupRegister(commands,
                                  WRITE_REG_2,
                                  calParam->sensorAddress,
                                  address,
                                  &buf,
                                  OV10640_DATA_REG_LEN);
        if (!expoSL) {
            LOG_ERR("%s: Failed to setup register EXPO_S_L\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    // Check for VTS condition
    nbLines = nbLinesL > nbLinesS ? nbLinesL : nbLinesS;
    if (nbLines >= (vts - 6)) {
        vts += nbLines - (vts - 6) + 1;

        buf = (vts >> 8) & 0xFF;
        address[0] = (OV10640_VTS_H >> 8) & 0xFF;
        address[1] = OV10640_VTS_H & 0xFF;
        if (!I2cSetupRegister(commands,
                             WRITE_REG_2,
                             calParam->sensorAddress,
                             address,
                             &buf,
                             OV10640_DATA_REG_LEN)) {
            LOG_ERR("%s: Failed to setup register VTS_H\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        buf = vts & 0xFF;
        address[0] = (OV10640_VTS_L >> 8) & 0xFF;
        address[1] = OV10640_VTS_L & 0xFF;
        if (!I2cSetupRegister(commands,
                             WRITE_REG_2,
                             calParam->sensorAddress,
                             address,
                             &buf,
                             OV10640_DATA_REG_LEN)) {
            LOG_ERR("%s: Failed to setup register VTS_L\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    if (ov10640Properties->etvs.isUsed) {
        nbLinesVS = (int)(ov10640Properties->etvs.floatValue * 32 * sclk / hts);

        // Check max value of nbLinesVS for very short exposure case
        if (nbLinesVS > 0x7F) {
            LOG_ERR("%s: nbLinesVS is greater than 0x7F\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        buf = nbLinesVS & 0xFF;
        address[0] = (OV10640_EXPO_VS >> 8) & 0xFF;
        address[1] = OV10640_EXPO_VS & 0xFF;

        expoVS = I2cSetupRegister(commands,
                                  WRITE_REG_2,
                                  calParam->sensorAddress,
                                  address,
                                  &buf,
                                  OV10640_DATA_REG_LEN);
        if (!expoVS) {
            LOG_ERR("%s: Failed to setup register EXPO_VS\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
CalculateDigitalGain(Ov10640Properties *ov10640Properties,
                     I2cCommands *commands,
                     CalibrationParameters *calParam)
{
    NvU8 buf;
    NvU8 address[2];
    NvU8 *DgainLH = NULL;
    NvU8 *DgainLL = NULL;
    NvU8 *DgainSH = NULL;
    NvU8 *DgainSL = NULL;
    NvU8 *DgainVSH = NULL;
    NvU8 *DgainVSL = NULL;

    if (ov10640Properties->dgl.isUsed) {
        buf = (int)ov10640Properties->dgl.floatValue;
        address[0] = (OV10640_DGAIN_L_H >> 8) & 0xFF;
        address[1] = OV10640_DGAIN_L_H & 0xFF;
        DgainLH = I2cSetupRegister(commands,
                                   WRITE_REG_2,
                                   calParam->sensorAddress,
                                   address,
                                   &buf,
                                   OV10640_DATA_REG_LEN);
        if (!DgainLH) {
            LOG_ERR("%s: Failed to setup register DGAIN_L_H\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        buf = (int)(ov10640Properties->dgl.floatValue * 256) & 0xFF;
        address[0] = (OV10640_DGAIN_L_L >> 8) & 0xFF;
        address[1] = OV10640_DGAIN_L_L & 0xFF;
        DgainLL = I2cSetupRegister(commands,
                                   WRITE_REG_2,
                                   calParam->sensorAddress,
                                   address,
                                   &buf,
                                   OV10640_DATA_REG_LEN);
        if (!DgainLL) {
            LOG_ERR("%s: Failed to setup register DGAIN_L_L\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    if (ov10640Properties->dgs.isUsed) {
        buf = (int)ov10640Properties->dgs.floatValue;
        address[0] = (OV10640_DGAIN_S_H >> 8) & 0xFF;
        address[1] = OV10640_DGAIN_S_H & 0xFF;
        DgainSH = I2cSetupRegister(commands,
                                   WRITE_REG_2,
                                   calParam->sensorAddress,
                                   address,
                                   &buf,
                                   OV10640_DATA_REG_LEN);
        if (!DgainSH) {
            LOG_ERR("%s: Failed to setup register DGAIN_S_H\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        buf = (int)(ov10640Properties->dgs.floatValue * 256) & 0xFF;
        address[0] = (OV10640_DGAIN_S_L >> 8) & 0xFF;
        address[1] = OV10640_DGAIN_S_L & 0xFF;
        DgainSL = I2cSetupRegister(commands,
                                   WRITE_REG_2,
                                   calParam->sensorAddress,
                                   address,
                                   &buf,
                                   OV10640_DATA_REG_LEN);
        if (!DgainSL) {
            LOG_ERR("%s: Failed to setup register DGAIN_S_L\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    if (ov10640Properties->dgvs.isUsed) {
        buf = (int)ov10640Properties->dgvs.floatValue;
        address[0] = (OV10640_DGAIN_VS_H >> 8) & 0xFF;
        address[1] = OV10640_DGAIN_VS_H & 0xFF;
        DgainVSH = I2cSetupRegister(commands,
                                    WRITE_REG_2,
                                    calParam->sensorAddress,
                                    address,
                                    &buf,
                                    OV10640_DATA_REG_LEN);
        if (!DgainVSH) {
            LOG_ERR("%s: Failed to setup register DGAIN_VS_H\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }

        buf = (int)(ov10640Properties->dgvs.floatValue * 256) & 0xFF;
        address[0] = (OV10640_DGAIN_VS_L >> 8) & 0xFF;
        address[1] = OV10640_DGAIN_VS_L & 0xFF;
        DgainVSL = I2cSetupRegister(commands,
                                    WRITE_REG_2,
                                    calParam->sensorAddress,
                                    address,
                                    &buf,
                                    OV10640_DATA_REG_LEN);
        if (!DgainVSL) {
            LOG_ERR("%s: Failed to setup register DGAIN_VS_L\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
CalculateAnalogGain(Ov10640Properties *ov10640Properties,
                    I2cCommands *commands,
                    CalibrationParameters *calParam,
                    NvU8 *oldGain)
{
    NvU8 analogGain = *oldGain;
    NvU32 sum = 0;
    NvU8 address[2];
    NvU8 *CgAgain = NULL;
    NvU32 temp = 0;

    if (ov10640Properties->agl.isUsed) {
        temp = ov10640Properties->agl.uIntValue;
        sum = 0;
        analogGain &= ~3;
        while (!(temp & 0x1)) {
            temp = temp >> 1;
            sum++;
        }
        analogGain |= sum;
    }

    if (ov10640Properties->ags.isUsed) {
        temp = ov10640Properties->ags.uIntValue;
        sum = 0;
        analogGain &= ~(3<<2);
        while (!(temp & 0x1)) {
            temp = temp >> 1;
            sum++;
        }
        analogGain |= (sum << 2);
    }

    if (ov10640Properties->agvs.isUsed) {
        temp = ov10640Properties->agvs.uIntValue;
        sum = 0;
        analogGain &= ~(3<<4);
        while (!(temp & 0x1)) {
            temp = temp >> 1;
            sum++;
        }
        analogGain |= (sum << 4);
    }

    if (ov10640Properties->cgl.isUsed) {
        analogGain &= ~(1<<6);
        analogGain |=  ((ov10640Properties->cgl.uIntValue & 0xFF) << 6);
    }

    if (ov10640Properties->cgvs.isUsed) {
        analogGain &= ~(1<<7);
        analogGain |=  ((ov10640Properties->cgvs.uIntValue & 0xFF) << 7);
    }

    if (ov10640Properties->agl.isUsed || ov10640Properties->ags.isUsed || ov10640Properties->agvs.isUsed ||
       ov10640Properties->cgl.isUsed || ov10640Properties->cgvs.isUsed) {
        address[0] = (OV10640_CG_AGAIN >> 8) & 0xFF;
        address[1] = OV10640_CG_AGAIN & 0xFF;
        CgAgain = I2cSetupRegister(commands,
                                   WRITE_REG_2,
                                   calParam->sensorAddress,
                                   address,
                                   &analogGain,
                                   OV10640_DATA_REG_LEN);
        if (!CgAgain) {
            LOG_ERR("%s: Failed to setup register CG_AGAIN\n", __func__);
            return NVMEDIA_STATUS_ERROR;
        }
    }
    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
CalibrateSensor(I2cCommands *commands, CalibrationParameters *calParam, SensorProperties *properties)
{
    NvMediaStatus status;
    NvU8 *analogGain = NULL;
    NvU8 *vtsH = NULL;
    NvU8 *vtsL = NULL;
    NvU8 *htsH = NULL;
    NvU8 *htsL = NULL;
    NvU8 *sclkPllPre = NULL;
    NvU8 *sclkPllMult = NULL;
    NvU8 *sclkPllPost = NULL;
    NvU32 i = 0, temp = 0;
    RegisterSetup rgstrArray[OV10640_NUM_REG_TO_READ];
    Ov10640Properties *ov10640Properties = (SensorProperties *)properties;

    memset(rgstrArray, 0, sizeof(RegisterSetup)*OV10640_NUM_REG_TO_READ);
    rgstrArray[0].rgstrAddr = OV10640_CG_AGAIN;
    rgstrArray[1].rgstrAddr = OV10640_VTS_H;
    rgstrArray[2].rgstrAddr = OV10640_VTS_L;
    rgstrArray[3].rgstrAddr = OV10640_HTS_H;
    rgstrArray[4].rgstrAddr = OV10640_HTS_L;
    rgstrArray[5].rgstrAddr = OV10640_SCLK_PLL_PRE;
    rgstrArray[6].rgstrAddr = OV10640_SCLK_PLL_MULT;
    rgstrArray[7].rgstrAddr = OV10640_SCLK_PLL_POST;

    // Read registers needed to setup sensor settings
    for (i = 0; i < OV10640_NUM_REG_TO_READ; i++) {
        // Re-arrage address to match i2c read/write format (switch bytes)
        temp = rgstrArray[i].rgstrAddr;
        rgstrArray[i].rgstrAddr = (temp >> 8) & 0xFF;
        rgstrArray[i].rgstrAddr |= (temp & 0xFF) << 8;

        rgstrArray[i].rgstrVal = (NvU16 *) I2cSetupRegister(commands,
                             READ_REG_2,
                             calParam->sensorAddress,
                             (NvU8 *)&rgstrArray[i].rgstrAddr,
                             NULL,
                             OV10640_DATA_REG_LEN);
        if (!rgstrArray[i].rgstrVal) {
            LOG_ERR("%s: Failed to setup read register %x\n", __func__,
                    rgstrArray[i].rgstrAddr);
            status = NVMEDIA_STATUS_ERROR;
            goto failed;
        }
    }
    status = I2cProcessCommands(commands,
                                I2C_READ,
                                calParam->i2cDevice);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to process register settings\n", __func__);
        goto failed;
    }
    analogGain = (NvU8 *) rgstrArray[0].rgstrVal;
    vtsH = (NvU8 *) rgstrArray[1].rgstrVal;
    vtsL = (NvU8 *) rgstrArray[2].rgstrVal;
    htsH = (NvU8 *) rgstrArray[3].rgstrVal;
    htsL = (NvU8 *) rgstrArray[4].rgstrVal;
    sclkPllPre = (NvU8 *) rgstrArray[5].rgstrVal;
    sclkPllMult = (NvU8 *) rgstrArray[6].rgstrVal;
    sclkPllPost = (NvU8 *) rgstrArray[7].rgstrVal;

    // Calculate and write exposure, digital gain and analog/common gain
    status = CalculateExposureTimes(ov10640Properties, commands, calParam, vtsH,
                                    vtsL, htsH, htsL, sclkPllPre, sclkPllMult,
                                    sclkPllPost);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to calculate exposure settings\n", __func__);
        goto failed;
    }
    status = CalculateDigitalGain(ov10640Properties, commands, calParam);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to calculate digital gain\n", __func__);
        goto failed;
    }
    status = CalculateAnalogGain(ov10640Properties, commands, calParam, analogGain);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to calculate analog gain\n", __func__);
        goto failed;
    }

    return NVMEDIA_STATUS_OK;
failed:
    return status;
}

static NvMediaStatus
ProcessCmdline(int argc, char *argv[], SensorProperties *properties)
{
    NvMediaBool bLastArg = NVMEDIA_FALSE;
    NvMediaBool bDataAvailable = NVMEDIA_FALSE;
    int i;
    Ov10640Properties *ov10640Properties = (SensorProperties *)properties;

    for (i = 1; i < argc; i++) {
        // Check if this is the last argument
        bLastArg = ((argc - i) == 1);

        // Check if there is data available to be parsed
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if (!strcasecmp(argv[i], "-etl")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ov10640Properties->etl.isUsed = NVMEDIA_TRUE;
                ov10640Properties->etl.floatValue = atof(arg);
            } else {
                LOG_ERR("-etl must be followed by an exposure time value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-ets")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ov10640Properties->ets.isUsed = NVMEDIA_TRUE;
                ov10640Properties->ets.floatValue = atof(arg);
            } else {
                LOG_ERR("-ets must be followed by an exposure time value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-etvs")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ov10640Properties->etvs.isUsed = NVMEDIA_TRUE;
                ov10640Properties->etvs.floatValue = atof(arg);
            } else {
                LOG_ERR("-etvs must be followed by an exposure time value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-dgl")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ov10640Properties->dgl.isUsed = NVMEDIA_TRUE;
                ov10640Properties->dgl.floatValue = atof(arg);
            } else {
                LOG_ERR("-dgl must be followed by a digital gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-dgs")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ov10640Properties->dgs.isUsed = NVMEDIA_TRUE;
                ov10640Properties->dgs.floatValue = atof(arg);
            } else {
                LOG_ERR("-dgs must be followed by a digital gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-dgvs")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                ov10640Properties->dgvs.isUsed = NVMEDIA_TRUE;
                ov10640Properties->dgvs.floatValue = atof(arg);
            } else {
                LOG_ERR("-dgvs must be followed by a digital gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-agl")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 1:
                    case 2:
                    case 4:
                    case 8:
                        ov10640Properties->agl.isUsed = NVMEDIA_TRUE;
                        ov10640Properties->agl.uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid analog gain value provided. Valid values are 1, 2, 4 or 8\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-agl must be followed by an analog gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-ags")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 1:
                    case 2:
                    case 4:
                    case 8:
                        ov10640Properties->ags.isUsed = NVMEDIA_TRUE;
                        ov10640Properties->ags.uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid analog gain value provided. Valid values are 1, 2, 4 or 8\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-ags must be followed by an analog gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-agvs")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 1:
                    case 2:
                    case 4:
                    case 8:
                        ov10640Properties->agvs.isUsed = NVMEDIA_TRUE;
                        ov10640Properties->agvs.uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid analog gain value provided. Valid values are 1, 2, 4 or 8\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-agvs must be followed by an analog gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-cgl")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 0:
                    case 1:
                        ov10640Properties->cgl.isUsed = NVMEDIA_TRUE;
                        ov10640Properties->cgl.uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid common gain value provided. Valid values are 0 or 1\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-cgl must be followed by a common gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        } else if (!strcasecmp(argv[i], "-cgvs")) {
            if (bDataAvailable) {
                char *arg = argv[++i];
                switch (atoi(arg)) {
                    case 0:
                    case 1:
                        ov10640Properties->cgvs.isUsed = NVMEDIA_TRUE;
                        ov10640Properties->cgvs.uIntValue = atoi(arg);
                        break;
                    default:
                        LOG_ERR("Invalid common gain value provided. Valid values are 0 or 1\n");
                        return NVMEDIA_STATUS_ERROR;
                }
            } else {
                LOG_ERR("-cgvs must be followed by a common gain value\n");
                return NVMEDIA_STATUS_ERROR;
            }
        }
    }

    return NVMEDIA_STATUS_OK;
}

static NvMediaStatus
WriteNvRawImage(I2cCommands *settings,
                CalibrationParameters *calParam,
                NvMediaImage *image, NvS32 frameNumber, char *outputFileName)
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
    NvU32 numExposures = OV10640_MAX_EXPOSURES;
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
    char fuseBuffer[OV10640_SENSOR_FUSE_ID_SIZE * 2 + 2];
    NvU32 BayerPhase[2][2] = { {NVRAWDUMP_BAYER_ORDERING_RGGB, NVRAWDUMP_BAYER_ORDERING_GRBG},
                               {NVRAWDUMP_BAYER_ORDERING_GBRG, NVRAWDUMP_BAYER_ORDERING_BGGR}};

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

    compressionFormat = ReadRawCompressionFormat(calParam);


    switch (compressionFormat) {
        case 5:
            nvrawCompressionFormat = NvRawCompressionFormat_12BitLinear;
            linearMode = 0;
            numExposures = 1;
            rawBytesPerPixel = 2;
            inputFormatWidthMultiplier = 1;
            nvrawStatus = NvRawFileHeaderChunkSetBitsPerSample(pNvrfHeader,12);
            break;
        case 6:
            nvrawCompressionFormat = NvRawCompressionFormat_12BitLinear;
            linearMode = 1;
            numExposures = 1;
            rawBytesPerPixel = 2;
            inputFormatWidthMultiplier = 1;
            nvrawStatus = NvRawFileHeaderChunkSetBitsPerSample(pNvrfHeader,12);
            break;
        case 7:
            nvrawCompressionFormat = NvRawCompressionFormat_12BitLinear;
            linearMode = 2;
            numExposures = 1;
            rawBytesPerPixel = 2;
            inputFormatWidthMultiplier = 1;
            nvrawStatus = NvRawFileHeaderChunkSetBitsPerSample(pNvrfHeader,12);
            break;
        case 4:
            if (ReadRawCompressionMode(calParam))
                nvrawCompressionFormat = NvRawCompressionFormat_12BitCombinedCompressedExtended;
            else
                nvrawCompressionFormat = NvRawCompressionFormat_12BitCombinedCompressed;
            numExposures = 3;
            rawBytesPerPixel = 2;
            inputFormatWidthMultiplier = 1;
            nvrawStatus = NvRawFileHeaderChunkSetBitsPerSample(pNvrfHeader,12);
            break;
        default:
           LOG_ERR("WriteNvRawImage: Unsupported nvraw compression format\n");
           nvrawStatus = NvRawFileError_BadParameter;
           break;
    }

    if (nvrawStatus != NvRawFileError_Success){
        status = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    sensorData = (NvRawSensorHDRInfo_v2*)calloc(OV10640_MAX_EXPOSURES, sizeof(NvRawSensorHDRInfo_v2));
    //Get Image EmbeddedData
    if (frameNumber >= 0){
        status = ReadSensorExposureInfo(settings, calParam, sensorData);
        if (status != NVMEDIA_STATUS_OK){
            LOG_ERR("WriteNvRawImage: ReadSensorExposureInfo failed\n");
            goto done;
        }

        status = ReadSensorWbGainsInfo(settings, calParam, sensorData);
        if (status != NVMEDIA_STATUS_OK){
            LOG_ERR("WriteNvRawImage: ReadSensorWbGainsInfo failed\n");
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

    if (NvRawFileHeaderChunkSetDataFormat(pNvrfHeader, BayerPhase[1][1])!= NvRawFileError_Success) {
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
        sensorGains[i] = sensorData[linearMode].exposure.digitalGain;
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

    if (NvRawFileCaptureChunkSetEmbeddedLineCountTop(pNvrfCapture,image->embeddedDataTopSize)!= NvRawFileError_Success) {
        status = NVMEDIA_STATUS_ERROR;
        LOG_ERR("WriteNvRawImage: NvRawFileCaptureChunkSetEmbeddedLineCountTop failed \n");
        goto done;
    }

    if (NvRawFileCaptureChunkSetEmbeddedLineCountBottom(pNvrfCapture,image->embeddedDataBottomSize)!= NvRawFileError_Success) {
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
        if (NvRawFileCaptureChunkSetLut(pNvrfCapture, (NvU8*)OV10640_DecompressionLutCurve, sizeof(OV10640_DecompressionLutCurve))!= NvRawFileError_Success) {
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
            LOG_ERR("WriteNvRawImage: NvRawFileHDRChunkSetExposureInfo failed \n");
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

static NvMediaStatus
AppendOutputFilename(char *filename, SensorProperties *properties)
{
    char buf[5] = {0};
    Ov10640Properties *ov10640Properties = (SensorProperties *)properties;

    if (!filename)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if (ov10640Properties->etl.isUsed) {
        strcat(filename, "_etl_");
        sprintf(buf, "%.2f", ov10640Properties->etl.floatValue);
        strcat(filename, buf);
    }
    if (ov10640Properties->ets.isUsed) {
        strcat(filename, "_ets_");
        sprintf(buf, "%.2f", ov10640Properties->ets.floatValue);
        strcat(filename, buf);
    }
    if (ov10640Properties->etvs.isUsed) {
        strcat(filename, "_etvs_");
        sprintf(buf, "%.2f", ov10640Properties->etvs.floatValue);
        strcat(filename, buf);
    }

    if (ov10640Properties->dgl.isUsed) {
        strcat(filename, "_dgl_");
        sprintf(buf, "%.2f", ov10640Properties->dgl.floatValue);
        strcat(filename, buf);
    }
    if (ov10640Properties->dgs.isUsed) {
        strcat(filename, "_dgs_");
        sprintf(buf, "%.2f", ov10640Properties->dgs.floatValue);
        strcat(filename, buf);
    }
    if (ov10640Properties->dgvs.isUsed) {
        strcat(filename, "_dgvs_");
        sprintf(buf, "%.2f", ov10640Properties->dgvs.floatValue);
        strcat(filename, buf);
    }

    if (ov10640Properties->agl.isUsed) {
        strcat(filename, "_agl_");
        sprintf(buf, "%d", ov10640Properties->agl.uIntValue);
        strcat(filename, buf);
    }
    if (ov10640Properties->ags.isUsed) {
        strcat(filename, "_ags_");
        sprintf(buf, "%d", ov10640Properties->ags.uIntValue);
        strcat(filename, buf);
    }
    if (ov10640Properties->agvs.isUsed) {
        strcat(filename, "_agvs_");
        sprintf(buf, "%d", ov10640Properties->agvs.uIntValue);
        strcat(filename, buf);
    }

    if (ov10640Properties->cgl.isUsed) {
        strcat(filename, "_cgl_");
        sprintf(buf, "%d", ov10640Properties->cgl.uIntValue);
        strcat(filename, buf);
    }
    if (ov10640Properties->cgvs.isUsed) {
        strcat(filename, "_cgvs_");
        sprintf(buf, "%d", ov10640Properties->cgvs.uIntValue);
        strcat(filename, buf);
    }

    return NVMEDIA_STATUS_OK;
}

static void
PrintSensorCaliUsage(void)
{
    LOG_MSG("===========================================================\n");
    LOG_MSG("===            OV10640 calibration commands             ===\n");
    LOG_MSG("-etl [time]       Exposure time in microseconds (float) for long\n");
    LOG_MSG("-ets [time]       Exposure time in microseconds (float) for short\n");
    LOG_MSG("-etvs [time]      Exposure time in microseconds (float) for very short\n");
    LOG_MSG("-dgl [gain]       Digital gain (float) for long\n");
    LOG_MSG("-dgs [gain]       Digital gain (float) for short\n");
    LOG_MSG("-dgvs [gain]      Digital gain (float) for very short\n");
    LOG_MSG("-agl [gain]       Analog gain (int) for long\n");
    LOG_MSG("                  Valid values are 1, 2, 4, or 8\n");
    LOG_MSG("-ags [gain]       Analog gain (int) for short\n");
    LOG_MSG("                  Valid values are 1, 2, 4, or 8\n");
    LOG_MSG("-agvs [gain]      Analog gain (int) for very short\n");
    LOG_MSG("                  Valid values are 1, 2, 4, or 8\n");
    LOG_MSG("-cgl [gain]       Common gain (int) for long\n");
    LOG_MSG("                  Valid values are 0 or 1\n");
    LOG_MSG("-cgvs [gain]      Common gain (int) for very short\n");
    LOG_MSG("                  Valid values are 0 or 1\n");
}

static SensorInfo ov10640Info = {
    .name = "ov10640",
    .supportedArgs = ov10640SupportedArgs,
    .numSupportedArgs = sizeof(ov10640SupportedArgs)/sizeof(ov10640SupportedArgs[0]),
    .sizeOfSensorProperties = sizeof(Ov10640Properties),
    .CalibrateSensor = CalibrateSensor,
    .ProcessCmdline = ProcessCmdline,
    .AppendOutputFilename = AppendOutputFilename,
    .WriteNvRawImage = WriteNvRawImage,
    .PrintSensorCaliUsage = PrintSensorCaliUsage,
};

SensorInfo*
GetSensorInfo_ov10640(void) {
    return &ov10640Info;
}
