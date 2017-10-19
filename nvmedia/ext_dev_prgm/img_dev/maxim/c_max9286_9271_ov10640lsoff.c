/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "c_max9286_9271_ov10640lsoff.h"
#include "isc_max9271.h"
#include "isc_max9286.h"
#include "isc_ov10640.h"
#include "log_utils.h"
#include "error_max9286.h"
#include "dev_property.h"
#include "dev_map.h"

#define TEMP_CALIBRATION_ENABLE       1

static NvMediaBool s_channel_pre_matrix_enable = NVMEDIA_TRUE;
static float s_channel_pre_matrix[3][3] = {
    { 1.031400, -0.002700, 0.016000},
    {-0.016660,  1.003800, 0.008900},
    {-0.014700, -0.001200, 0.975100}
};

static void
Deinit(ExtImgDevice *device)
{
    NvU32 i;

    if(!device)
        return;

    for(i = 0; i < device->sensorsNum; i++) {
        if(device->iscSerializer[i])
            NvMediaISCDeviceDestroy(device->iscSerializer[i]);
        if(device->iscSensor[i])
            NvMediaISCDeviceDestroy(device->iscSensor[i]);
    }

    if(device->iscBroadcastSerializer)
        NvMediaISCDeviceDestroy(device->iscBroadcastSerializer);
    if(device->iscBroadcastSensor)
        NvMediaISCDeviceDestroy(device->iscBroadcastSensor);
    if(device->iscDeserializer)
        NvMediaISCDeviceDestroy(device->iscDeserializer);
    if(device->iscRoot)
        NvMediaISCRootDeviceDestroy(device->iscRoot);

    free(device);

    return;
}


static
NvMediaStatus
SetupConfigLink(
    ExtImgDevParam *configParam,
    ExtImgDevice *device
)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    ConfigureInputModeMAX9271 inputModeMAX9271 = { .bits = { // default value is for OV10640 (12bit single)
                              .dbl = ISC_INPUT_MODE_MAX9271_SINGLE_INPUT_MODE,
                              .drs = ISC_INPUT_MODE_MAX9271_HIGH_DATA_RATE_MODE,
                              .bws = ISC_INPUT_MODE_MAX9271_BWS_24_BIT_MODE,
                              .es = ISC_INPUT_MODE_MAX9271_PCLKIN_RISING_EDGE,
                              .reserved = 0,
                              .hven = ISC_INPUT_MODE_MAX9271_HVEN_ENCODING_ENABLE,
                              .edc = ISC_INPUT_MODE_MAX9271_EDC_1_BIT_PARITY}};
    ConfigurePixelInfoMAX9286 pixelInfoMAX9286 = { .bits = { // default value is for raw12
                              .type = ISC_DATA_TYPE_MAX9286_RAW11_RAW12,
                              .dbl = ISC_WORD_MODE_MAX9286_SINGLE,
                              .csi_dbl = ISC_CSI_MODE_MAX9286_SINGLE,
                              .lane_cnt = 3}};
    WriteParametersParamMAX9286 paramsMAX9286;

    if(!configParam)
        return NVMEDIA_STATUS_ERROR;
    if(!device)
        return NVMEDIA_STATUS_ERROR;

    // Disable forward/reverse channel to not propagate I2C transaction over the aggregator
    LOG_DBG("%s: Disable forward/reverse channel\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                        ISC_CONFIG_MAX9286_DISABLE_ALL_CONTROL_CHANNEL);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to disable aggregator forward/reverse channel\n", __func__);
        return status;
    }

    // Check if the aggregator is present
    LOG_DBG("%s: Check the aggregator is present\n", __func__);
    status = NvMediaISCCheckPresence(device->iscDeserializer);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_MSG("Warning: Due to a hardware limitation with MAXIM 96799 chip on the DRIVE PX 2 platform,\n");
        LOG_MSG("intermittent errors messages, similar to the following, are displayed during simultaneous capture\n");
        LOG_MSG("with infrequent functional quality impact\n");
        LOG_MSG("Intermittent ERROR messages :\n");
        LOG_MSG("    FlushCacheRegister - Error -7\n");
        LOG_MSG("    ISCThreadFunc: NvMediaISCSetWBGain failed\n");
        LOG_MSG("    captureGetErrorStatus: CsimuxFrameError\n");
        LOG_MSG("    tegra-vi4 15700000.vi: error notifier set to 1\n");
        LOG_MSG("For further details, consult the Implementation Notes section of the Release Notes\n");
        LOG_MSG("and the NvMedia Sample Applications in the PDK.\n");
    }

    if(!configParam->slave) {
        // Power up reverse channel transmitter
        LOG_DBG("%s: Power up reverse channel transmitter\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                            ISC_CONFIG_MAX9286_POWER_UP_REVERSE_CHANNEL_TRANSMITTER);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to power up reverse channel transmitter\n", __func__);
            return status;
        }

        // Set aggregator defaults
        LOG_DBG("%s: Set aggregator device defaults\n", __func__);
        status = NvMediaISCSetDefaults(device->iscDeserializer);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set aggregator defaults\n", __func__);
            return status;
        }

        usleep(2000);
    }

    if(!configParam->slave) {
        // Enable all of reverse channel for broadcasting
        LOG_DBG("%s: Enable all of reverse channels\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                        ISC_CONFIG_MAX9286_ENABLE_REVERSE_CHANNEL_0123);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable aggregator reverse channels\n", __func__);
            return status;
        }
        // 4ms needed by MAX9286 to lock the link
        usleep(4000);
    }

    if(!configParam->slave) {
        LOG_DBG("%s: Enable Deserializer reverse channel\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                        ISC_CONFIG_MAX9286_REVERSE_CHANNEL_ENABLE);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable Deserializer reverse channel\n", __func__);
            return status;
        }
        usleep(2000);  //wait 2ms
    }

    if(!configParam->slave) {
        LOG_DBG("%s: Set Des reverse channel amplitude\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                              ISC_CONFIG_MAX9286_REVERSE_CHANNEL_AMPL_L);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set Des reverse channel amplitude\n", __func__);
            return status;
        }
        usleep(2000);  //wait 2ms
    }

    if(device->iscBroadcastSerializer){
        // enable config channel
        LOG_DBG("%s: Enable reverse channel\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                ISC_CONFIG_MAX9271_ENABLE_REVERSE_CHANNEL);
        if(status != NVMEDIA_STATUS_OK)
            LOG_INFO("%s: Can return error while enabling reverse channel\n", __func__);

        // wait for configuration link to establish
        usleep(5000);

        LOG_DBG("%s: Set serielizer device defaults\n", __func__);
        status = NvMediaISCSetDefaults(device->iscBroadcastSerializer);
        if(status != NVMEDIA_STATUS_OK)
            LOG_ERR("%s: Failed to set serializer defaults\n", __func__);
        // wait after any change to reverse channel settings
        usleep(2000);
    }

    LOG_DBG("%s: Increase Des reverse channel amplitude\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                        ISC_CONFIG_MAX9286_REVERSE_CHANNEL_AMPL_H);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to increase Des reverse channel amplitude\n", __func__);
        return status;
    }
    // wait after any change to reverse channel settings
    usleep(2000);

    LOG_DBG("%s: Disable csi out\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                          ISC_CONFIG_MAX9286_DISABLE_CSI_OUT);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to disable csi out\n", __func__);
        return status;
    }

    LOG_DBG("%s: Set aggregator's data type\n", __func__);
    paramsMAX9286.SetPixelInfo.pixelInfo = &pixelInfoMAX9286;
    status = NvMediaISCWriteParameters(device->iscDeserializer,
             ISC_WRITE_PARAM_CMD_MAX9286_SET_PIXEL_INFO,
             sizeof(paramsMAX9286.SetPixelInfo),
             &paramsMAX9286);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set data type\n", __func__);
        return status;
    }

    if(configParam->enableExtSync) {
        LOG_MSG("Warning: Due to a hardware limitation with OV10640,\n");
        LOG_MSG("         the external synchronization is not supported for OV10640.\n");
        LOG_MSG("         Internal frames sync will be used on the deserializer by default.\n");
    }

    if(!configParam->slave) {
        paramsMAX9286.SetFsyncMode.syncMode = ISC_SET_FSYNC_MAX9286_FSYNC_SEMI_AUTO;
        paramsMAX9286.SetFsyncMode.k_val = 1;
    } else {
        paramsMAX9286.SetFsyncMode.syncMode = ISC_SET_FSYNC_MAX9286_DISABLE_SYNC;
    }

    // Set sync mode for ov10640
    LOG_DBG("%s: Set aggregator's fsync mode\n", __func__);
    status = NvMediaISCWriteParameters(device->iscDeserializer,
                        ISC_WRITE_PARAM_CMD_MAX9286_SET_SYNC_MODE,
                        sizeof(paramsMAX9286.SetFsyncMode),
                        &paramsMAX9286);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set fsync mode\n", __func__);
        return status;
    }

    LOG_DBG("%s: Enabling 4 links: 0, 1, 2 and 3\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                       ISC_CONFIG_MAX9286_ENABLE_LINKS_0123);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enable all links\n", __func__);
        return status;
    }
    usleep(2000);

    if(device->iscBroadcastSerializer){
        WriteReadParametersParamMAX9271 paramsMAX9271;
        paramsMAX9271.inputmode = &inputModeMAX9271;

        LOG_DBG("%s: Set serializer input mode\n", __func__);
        status = NvMediaISCWriteParameters(device->iscBroadcastSerializer,
                         ISC_WRITE_PARAM_CMD_MAX9271_CONFIG_INPUT_MODE,
                         sizeof(paramsMAX9271.inputmode),
                         &paramsMAX9271);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set serializer input mode\n", __func__);
            return status;
        }

        // wait after changing DBL
        usleep(2000);
    }

    if(device->iscBroadcastSerializer){
        unsigned char gpio_value;
        unsigned char gpio_enable = 0x52;

        // set GPIO
        LOG_DBG("%s: Enable GPIO\n", __func__);
        status = NvMediaISCWriteRegister(device->iscBroadcastSerializer,
                                0x0e, 1, &gpio_enable);
        if(status != NVMEDIA_STATUS_OK)
            LOG_INFO("%s: Can't enable gpio\n", __func__);

        gpio_value = 0xee;
        LOG_DBG("%s: Set GPIO %x\n", __func__, gpio_value);
        status = NvMediaISCWriteRegister(device->iscBroadcastSerializer,
                                0x0f, 1, &gpio_value);
        if(status != NVMEDIA_STATUS_OK)
            LOG_INFO("%s: Can't set gpio %x\n", __func__, gpio_value);

        usleep(10000);
        gpio_value = 0xfe;
        LOG_DBG("%s: Set GPIO\n", __func__);
        status = NvMediaISCWriteRegister(device->iscBroadcastSerializer,
                                0x0f, 1, &gpio_value);
        if(status != NVMEDIA_STATUS_OK)
            LOG_INFO("%s: Can't set gpio %x\n", __func__, gpio_value);

        usleep(30000);
    }


    // Enable aggregator auto ack
    LOG_DBG("%s: Enable aggregator auto ack\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                        ISC_CONFIG_MAX9286_ENABLE_AUTO_ACK);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enable aggregator auto ack\n", __func__);
        return status;
    }
    return status;
}

static
NvMediaStatus
SetupVideoLink (
    ExtImgDevParam *configParam,
    ExtImgDevice *device,
    NvU32 *remapIdx
)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU32 i;
    WriteParametersParamMAX9286 paramsMAX9286;
    NvS32 config;
    ConfigInfoOV10640 configInfo;
    WriteReadParametersParamOV10640 paramOV10640;
    paramOV10640.configInfo = &configInfo;

    if(device->iscBroadcastSerializer && !configParam->slave) {
        // Check link status to set up video link
        for(i = 0; i < configParam->sensorsNum; i++) {
            status = NvMediaISCCheckLink(device->iscDeserializer, remapIdx[i]);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Can't detect config link(%d)\n", __func__, remapIdx[i]);
                return status;
            }
        }
    }

    // Disable aggregator auto ack
    LOG_DBG("%s: Disable aggregator auto ack\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                        ISC_CONFIG_MAX9286_DISABLE_AUTO_ACK);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set aggregator configuration\n", __func__);
        return status;
    }

    // Disable reverse channel for checking individual sensor presence
    LOG_DBG("%s: Disable reverse channel\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                        ISC_CONFIG_MAX9286_DISABLE_REVERSE_CHANNEL_0123);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to disable aggregator reverse channel\n", __func__);
        return status;
    }
    // 3ms needed by MAX9286 to lock the link
    usleep(3000);

    if(device->iscBroadcastSensor && !configParam->slave) {
        for(i = 0; i < configParam->sensorsNum; i++) {
            paramsMAX9286.EnableReverseChannel.id = remapIdx[i];
            // Enable reverse channel
            status = NvMediaISCWriteParameters(device->iscDeserializer,
                            ISC_WRITE_PARAM_CMD_MAX9286_ENABLE_REVERSE_CHANNEL,
                            sizeof(paramsMAX9286.EnableReverseChannel),
                            &paramsMAX9286);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to disable aggregator reverse channels\n", __func__);
                return status;
            }
            // 4ms needed by MAX9286 to lock the link
            usleep(4000);
            if(!configParam->slave) {
                // Check sensor is present
                LOG_DBG("%s: Check sensor is present\n", __func__);
                status = NvMediaISCCheckPresence(device->iscBroadcastSensor);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Image sensor(%d) device is not present\n", __func__, remapIdx[i]);
                    return status;
                }
            }

            if(device->iscSensor[i]) {
                // Set address translation for the sensor to control individual sensor
                if(configParam->sensorAddr[i] && device->iscBroadcastSerializer) {
                    WriteReadParametersParamMAX9271 paramsMAX9271;
                    // Set address translation for the sensor
                    paramsMAX9271.Translator.source = configParam->sensorAddr[i];
                    paramsMAX9271.Translator.destination = configParam->brdcstSensorAddr;
                    LOG_INFO("%s: Translate image sensor device addr %x to %x\n", __func__,
                        paramsMAX9271.Translator.source, paramsMAX9271.Translator.destination);
                    status = NvMediaISCWriteParameters(device->iscBroadcastSerializer,
                                    ISC_WRITE_PARAM_CMD_MAX9271_SET_TRANSLATOR_B,
                                    sizeof(paramsMAX9271.Translator),
                                    &paramsMAX9271);
                    if(status != NVMEDIA_STATUS_OK) {
                        LOG_ERR("%s: Address translation setup failed\n", __func__);
                        return status;
                    }

                    if (configParam->selfTestFlag) {
                        LOG_DBG("Test ID6 \n");
                        status = NvMediaISCWriteParameters(device->iscSensor[i],
                                        ISC_WRITE_PARAM_CMD_OV10640_TEST_ID6,
                                        (unsigned int)(sizeof(paramOV10640.configInfo)),
                                        &paramOV10640);
                        if(status != NVMEDIA_STATUS_OK) {
                            LOG_ERR("%s: Test ID6 failed\n", __func__);
                            return status;
                        }
                        LOG_DBG("Test ID6 successful \n");
                    }
                }
            }

            // Set address translation for the serializer to control individual serializer
            if(configParam->serAddr[i] &&
                device->iscBroadcastSerializer &&
                device->iscSerializer[i]) {
                WriteReadParametersParamMAX9271 paramsMAX9271;

                // Set unique address with broadcast address
                paramsMAX9271.DeviceAddress.address = configParam->serAddr[i];
                status = NvMediaISCWriteParameters(device->iscBroadcastSerializer,
                            ISC_WRITE_PARAM_CMD_MAX9271_SET_DEVICE_ADDRESS,
                            sizeof(paramsMAX9271.DeviceAddress),
                            &paramsMAX9271);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to set serializer device %d address\n", __func__,
                            configParam->serAddr[i]);
                    return status;
                }
                // Set address translation for the serializer
                paramsMAX9271.Translator.source = configParam->brdcstSerAddr;
                paramsMAX9271.Translator.destination = configParam->serAddr[i];
                status = NvMediaISCWriteParameters(device->iscSerializer[i],
                                ISC_WRITE_PARAM_CMD_MAX9271_SET_TRANSLATOR_A,
                                sizeof(paramsMAX9271.Translator),
                                &paramsMAX9271);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Address translation setup failed\n", __func__);
                    return status;
                }
            }

            paramsMAX9286.DisableReverseChannel.id = remapIdx[i];
            // Disable reverse channel
            status = NvMediaISCWriteParameters(device->iscDeserializer,
                            ISC_WRITE_PARAM_CMD_MAX9286_DISABLE_REVERSE_CHANNEL,
                            sizeof(paramsMAX9286.EnableReverseChannel),
                            &paramsMAX9286);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to disable aggregator reverse channel\n", __func__);
                return status;
            }
        }
    }

    if(!configParam->slave) {
        // Enable all of reverse channel for broadcasting
        LOG_DBG("%s: Enable all of reverse channels\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                        ISC_CONFIG_MAX9286_ENABLE_REVERSE_CHANNEL_0123);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable aggregator reverse channels\n", __func__);
            return status;
        }
        // 4ms needed by MAX9286 to lock the link
        usleep(4000);
    }

    status = GetOV10640ConfigSet(configParam->resolution,
                                 configParam->inputFormat,
                                 &config);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to get config set\n", __func__);
        return status;
    }

    if(device->iscBroadcastSensor) {
        // Set sensor defaults after software reset
        LOG_DBG("%s: Set OV10640 defaults\n", __func__);
        status = NvMediaISCSetDefaults(device->iscBroadcastSensor);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set OV10640 defaults\n", __func__);
            return status;
        }

        LOG_DBG("%s: Set sensor configuration for enabling sync mode\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                            ISC_CONFIG_OV10640_ENABLE_FSIN);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set sensor configuration for enabling sync mode\n", __func__);
            return status;
        }

        // Set sensor configuration
        LOG_DBG("%s: Set sensor configuration (%u)\n", __func__, config);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                            config);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set sensor configuration\n", __func__);
            return status;
        }
    }

    for(i = 0; i < configParam->sensorsNum; i++) {
        if(device->iscSensor[i]) {
            WriteReadParametersParamOV10640 paramOV10640;
            ShortChannelPreMatrix matrix;

            matrix.enable = s_channel_pre_matrix_enable;
            memcpy(matrix.arr, s_channel_pre_matrix, sizeof(s_channel_pre_matrix));

            paramOV10640.matrix = &matrix;
            status = NvMediaISCWriteParameters(device->iscSensor[i], ISC_WRITE_PARAM_CMD_OV10640_PRE_MATRIX,
                                               sizeof(paramOV10640.matrix),
                                               &paramOV10640);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to set short channel pre-matrix\n", __func__);
                return status;
            }
        }
    }

    // Enable aggregator's auto ack
    LOG_DBG("%s: Enable aggregator's auto ack\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                       ISC_CONFIG_MAX9286_ENABLE_AUTO_ACK);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enable aggregator's auto ack\n", __func__);
        return status;
    }

    switch(configParam->sensorsNum) {
        case 1:
            LOG_DBG("%s: Enabling link: 0\n", __func__);
            status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                    ISC_CONFIG_MAX9286_ENABLE_LINK_0);
            break;
        case 2:
            LOG_DBG("%s: Enabling 2 links: 0 and 1\n", __func__);
            status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                    ISC_CONFIG_MAX9286_ENABLE_LINKS_01);
            break;
        case 3:
            LOG_DBG("%s: Enabling 3 links: 0, 1 and 2\n", __func__);
            status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                    ISC_CONFIG_MAX9286_ENABLE_LINKS_012);
            break;
        case 4:
            LOG_DBG("%s: Enabling 4 links: 0, 1, 2 and 3\n", __func__);
            status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                    ISC_CONFIG_MAX9286_ENABLE_LINKS_0123);
            break;
        default:
            LOG_ERR("%s: Failed to set aggregator configuration\n", __func__);
            return NVMEDIA_STATUS_ERROR;
    }
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: NvMediaISCSetDeviceConfig failed on enabling links\n", __func__);
        return status;
    }

    // Set camera Mapping in Desrializer if new mapping needed
    if(ExtImgDevCheckReMap(configParam->camMap)) {
        LOG_DBG("%s: Set camera mapping\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                  ISC_CONFIG_MAX9286_CAMERA_MAPPING);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set camera mapping\n", __func__);
            return status;
        }
    }

    if(configParam->enableVirtualChannels) {
        // Enable virtual channel
        LOG_DBG("%s: Enable virtual channel\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                           ISC_CONFIG_MAX9286_ENABLE_VC);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable virtual channel\n", __func__);
            return status;
        }
    }

    if(device->iscBroadcastSerializer) {
        // Set PreEmphasis
        WriteReadParametersParamMAX9271 paramsMAX9271;
        paramsMAX9271.preemp = ISC_SET_PREEMP_MAX9271_PLU_6_0DB; /* Bug 1850534 */

        LOG_DBG("%s: Set all serializer Preemphasis setting\n", __func__);
        status = NvMediaISCWriteParameters(device->iscBroadcastSerializer,
                         ISC_WRITE_PARAM_CMD_MAX9271_SET_PREEMP,
                         sizeof(paramsMAX9271.preemp),
                         &paramsMAX9271);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set Preemphasis setting\n", __func__);
            return status;
        }

        // Enable each serial link
        LOG_DBG("%s: Enable serial link\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                           ISC_CONFIG_MAX9271_ENABLE_SERIAL_LINK);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable serial link\n", __func__);
            return status;
        }
        /* wait for GMSL lock to be stable */
        usleep(5000);
    }

    return status;
}

static ExtImgDevice *
Init(ExtImgDevParam *configParam)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU32 i, remap;
    ExtImgDevice *device;
    NvU32 remapIdx[MAX_AGGREGATE_IMAGES] = {0};
    NvMediaISCAdvancedConfig advConfig;
    ContextOV10640 ctxOV10640;
    ContextMAX9286 ctxMAX9286;

    memset(&ctxMAX9286, 0, sizeof(ContextMAX9286));
    memset(&ctxOV10640, 0, sizeof(ContextOV10640));

    if(!configParam)
        return NULL;

    device = calloc(1, sizeof(ExtImgDevice));
    if(!device) {
        LOG_ERR("%s: out of memory\n", __func__);
        return NULL;
    }

    // remap CSI link indexes if CSI remapping bitmask is given
    for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
        // get remapped index of link i
        remap = (configParam->camMap) ? EXTIMGDEV_MAP_LINK_CSIOUT(configParam->camMap->csiOut, i) : i;
        remapIdx[remap] = i;
    }

    LOG_INFO("%s: Set image device property\n", __func__);
    status = ImgDevSetProperty(GetDriver_c_max9286_9271_ov10640lsoff(),
                                        configParam,
                                        device);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: doesn't support the given property, check input param and image property\n",
                 __func__);
        goto failed;
    }

    LOG_INFO("%s: Create root device\n", __func__);
    device->iscRoot = NvMediaISCRootDeviceCreate(
                             ISC_ROOT_DEVICE_CFG_EX(configParam->slave,
                                 device->property.interface,
                                 configParam->enableSimulator?
                                     NVMEDIA_ISC_I2C_SIMULATOR :
                                     configParam->i2cDevice), // port
                             32,                     // queueElementsNumber
                             256,                    // queueElementSize
                             NVMEDIA_FALSE);         // enableExternalTransactions
    if(!device->iscRoot) {
        LOG_ERR("%s: Failed to create NvMedia ISC root device\n", __func__);
        goto failed;
    }

    // Delay required after sensor is powered on. However amount of delay is
    // currently unknown, so using the same delay as maxim camera (50 ms)
    usleep(50000);

    if(configParam->desAddr) {
        ctxMAX9286.reverseChannelAmp = 150; /* reverse channel amp: 150mV */
        ctxMAX9286.reverseChannelTrf = 300; /* transition time: 300ns */
        /* Do not configure modules behind the aggregator when slave mode enabled */
        ctxMAX9286.disableBackChannelCtrl = (configParam->slave) ? NVMEDIA_TRUE : NVMEDIA_FALSE;

        // check if camMap valid
        if(configParam->camMap != NULL) {
            status = ExtImgDevCheckValidMap(configParam->camMap);
            if(status != NVMEDIA_STATUS_OK)
                goto failed;
            memcpy(&ctxMAX9286.camMap,configParam->camMap,sizeof(CamMap));
        }

        // Create the aggregator device
        LOG_INFO("%s: Create aggregator device on address 0x%x\n", __func__, configParam->desAddr);
        ADV_CONFIG_INIT(advConfig, &ctxMAX9286);
        device->iscDeserializer = NvMediaISCDeviceCreate(
                            device->iscRoot,     // rootDevice
                            NULL,                   // parentDevice
                            0,                      // instanceNumber
                            configParam->desAddr,   // deviceAddress
                            GetMAX9286Driver(),     // deviceDriver
                            &advConfig);            // advancedConfig
        if(!device->iscDeserializer) {
            LOG_ERR("%s: Failed to create aggregator device\n", __func__);
            goto failed;
        }
    }

    if(configParam->brdcstSerAddr) {
        // Create broadcast serializer device
        LOG_INFO("%s: Create broadcast serializer device on address 0x%x\n", __func__,
                          configParam->brdcstSerAddr);
        device->iscBroadcastSerializer = NvMediaISCDeviceCreate(
                          device->iscRoot,
                          device->iscDeserializer,
                          0,
                          configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                               configParam->brdcstSerAddr,
                          GetMAX9271Driver(),
                          NULL);
        if(!device->iscBroadcastSerializer) {
            LOG_ERR("%s: Failed to create broadcase serializer device\n", __func__);
            goto failed;
        }
    }

    status = ImgDevGetModuleConfig(&ctxOV10640.moduleConfig, configParam->moduleName);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to get camera module config file name\n", __func__);
    }

    ctxOV10640.oscMHz = 25;
    if(configParam->brdcstSensorAddr) {
        ADV_CONFIG_INIT(advConfig, &ctxOV10640);
        // Create the image sensor device
        LOG_INFO("%s: Create broadcast sensor device on address 0x%x\n", __func__,
                         configParam->brdcstSensorAddr);
        device->iscBroadcastSensor = NvMediaISCDeviceCreate(
                                        device->iscRoot,
                                        device->iscBroadcastSerializer,
                                        0,
                                        configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                                             configParam->brdcstSensorAddr,
                                        GetOV10640Driver(),
                                        &advConfig);
        if(!device->iscBroadcastSensor) {
            LOG_ERR("%s: Failed to create broadcast sensor device\n", __func__);
            goto failed;
        }
    }

    for(i = 0; i < configParam->sensorsNum; i++) {
        if(configParam->serAddr[i]) {
            // Create the serializer device
            LOG_INFO("%s: Create serializer device %u on address 0x%x\n", __func__, remapIdx[i],
                        configParam->serAddr[i]);
            device->iscSerializer[i] = NvMediaISCDeviceCreate(
                        device->iscRoot,
                        device->iscDeserializer,
                        remapIdx[i],
                        configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                             configParam->serAddr[i],
                        GetMAX9271Driver(),
                        NULL);
            if(!device->iscSerializer[i]) {
                LOG_ERR("%s: Failed to create serializer device\n", __func__);
                goto failed;
            }
        }

        if(configParam->sensorAddr[i]) {
            // Create the image sensor device
            LOG_INFO("%s: Create image sensor device %u on address 0x%x\n", __func__, remapIdx[i],
                        configParam->sensorAddr[i]);
            device->iscSensor[i] = NvMediaISCDeviceCreate(
                                        device->iscRoot,
                                        device->iscSerializer[i] ?
                                            device->iscSerializer[i] :
                                            device->iscBroadcastSerializer,
                                        remapIdx[i],
                                        configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                                             configParam->sensorAddr[i],
                                        GetOV10640Driver(),
                                        &advConfig);
            if(!device->iscSensor[i]) {
                LOG_ERR("%s: Failed to create image sensor device\n", __func__);
                goto failed;
            }
        }
    }

    if(configParam->initialized || configParam->enableSimulator) {
        ConfigInfoOV10640 configInfo;
        WriteReadParametersParamOV10640 paramOV10640;
        paramOV10640.configInfo = &configInfo;

        status = NvMediaISCReadParameters(device->iscBroadcastSensor,
                        ISC_READ_PARAM_CMD_OV10640_CONFIG_INFO,
                        sizeof(paramOV10640.configInfo),
                        &paramOV10640);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get config\n", __func__);
            goto failed;
        }

        status = GetOV10640ConfigSet(configParam->resolution,
                                     configParam->inputFormat,
                                     &configInfo.enumeratedDeviceConfig);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get config set\n", __func__);
            goto failed;
        }
        status = NvMediaISCReadParameters(device->iscBroadcastSensor,
                        ISC_READ_PARAM_CMD_OV10640_EXP_LINE_RATE,
                        sizeof(paramOV10640.configInfo),
                        &paramOV10640);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get line rate\n", __func__);
            goto failed;
        }

        LOG_INFO("%s: Update config info for boardcastSensorDevice\n", __func__);

        status = NvMediaISCWriteParameters(device->iscBroadcastSensor,
                        ISC_WRITE_PARAM_CMD_OV10640_CONFIG_INFO,
                        sizeof(paramOV10640.configInfo),
                        &paramOV10640);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set config\n", __func__);
            goto failed;
        }

        goto init_done;
    }

    status = SetupConfigLink(configParam, device);
    if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to setup config link\n", __func__);
            goto failed;
    }

    status = SetupVideoLink(configParam, device, remapIdx);
    if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to setup video link\n", __func__);
            goto failed;
    }

init_done:
    // update frame id for embedded data
    if(device->iscBroadcastSensor) {
        // if individual sensor device handle exists, it needs to copy the config info
        // for each sensor from braodcast device to control expose time and to get embedded data.
        ConfigInfoOV10640 configInfo;
        WriteReadParametersParamOV10640 paramOV10640;
        paramOV10640.configInfo = &configInfo;

        status = NvMediaISCReadParameters(device->iscBroadcastSensor,
                        ISC_READ_PARAM_CMD_OV10640_CONFIG_INFO,
                        sizeof(paramOV10640.configInfo),
                        &paramOV10640);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get config\n", __func__);
            goto failed;
        }

        for(i = 0; i < configParam->sensorsNum; i++) {
            if(device->iscSensor[i]) {
                LOG_DBG("%s: Set sensor[%d] config\n", __func__, i);
                status = NvMediaISCWriteParameters(device->iscSensor[i],
                                ISC_WRITE_PARAM_CMD_OV10640_CONFIG_INFO,
                                sizeof(paramOV10640.configInfo),
                                &paramOV10640);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to set config\n", __func__);
                    goto failed;
                }
            }
        }

        LOG_DBG("%s: Set sensor frame\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                              ISC_CONFIG_OV10640_RESET_FRAME_ID);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set sensor frame id\n", __func__);
            goto failed;
        }
    }

    // Disable lens shading
    LOG_DBG("%s: Disable lens shading\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                           ISC_CONFIG_OV10640_DISABLE_LENS_SHADING);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to disable lens shading\n", __func__);
        goto failed;
    }

    memcpy(device->remapIdx, remapIdx, sizeof(device->remapIdx));
    device->simulator = configParam->enableSimulator;

    return device;

failed:
    Deinit(device);

    return NULL;
}

static NvMediaStatus
Start(ExtImgDevice *device)
{
    NvMediaStatus status;
    NvU32 i;

    if(!device)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    // Enable csi out
    LOG_DBG("%s: Enable streaming\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                       ISC_CONFIG_OV10640_ENABLE_STREAMING);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enable sensor streaming\n", __func__);
        return status;
    }

    // TODO: Without this sleep, sometimes aggregator can't detect video link,
    // once we find the way the timing, will remove this delay
    if(!device->simulator) {
        for(i = 0; i < device->sensorsNum; i++) {
            NvU32 timeout = 2000;
            LOG_DBG("%s: Get Link(%d) Status\n", __func__, device->remapIdx[i]);
            do {
                // Check Video Link
                usleep(10);
                status = NvMediaISCGetLinkStatus(device->iscDeserializer, device->remapIdx[i]);
                timeout--;
            } while ((status != NVMEDIA_STATUS_OK) && (timeout));

            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Video Link(%d) is not detected\n", __func__, device->remapIdx[i]);
                return status;
            }
        }
    }

    // If no need to get sensor temperature, disable temp calibration
    // to avoid 25 ms delay required for OTP memory read
    if (TEMP_CALIBRATION_ENABLE) {
        for(i = 0; i < device->sensorsNum; i++) {
            if(device->iscSensor[i]) {
                LOG_DBG("Temperature Calibration \n");
                status = NvMediaISCSetDeviceConfig(device->iscSensor[i],
                                ISC_CONFIG_OV10640_ENABLE_TEMP_CALIBRATION);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Temperature Calibration failed \n", __func__);
                    return status;
                }
                LOG_DBG("Temperature Calibration successful \n");
            }
        }
    }

    // Enable csi out
    LOG_DBG("%s: Enable csi out\n", __func__);
    return NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                     ISC_CONFIG_MAX9286_ENABLE_CSI_OUT);
}

static NvMediaStatus
RegisterCallback(
    ExtImgDevice *device,
    NvU32 sigNum,
    void (*cb)(void *),
    void *context
)
{
    return NvMediaISCRootDeviceRegisterCallback(device->iscRoot,
                                                sigNum, cb, context);
}

static NvMediaStatus
GetError(
    ExtImgDevice *device,
    NvU32 *link,
    ExtImgDevFailureType *errorType
)
{
    return _GetError_max9286(device->iscDeserializer, link, errorType);
}

static ImgProperty properties[] = {
                   /* resolution, oscMHz, fps, pclk,    embTop, embBottom, inputFormat, pixelOrder */
    IMG_PROPERTY_ENTRY(1280x1080,     25,  30, 50002260,     2,         2,       raw12,       bggr),
    IMG_PROPERTY_ENTRY(1280x800,      25,  30, 36005340,     2,         2,       raw12,       bggr),
};

static ImgDevDriver device = {
    .name = "c_max9286_9271_ov10640lsoff",
    .Init = Init,
    .Deinit = Deinit,
    .Start = Start,
    .RegisterCallback = RegisterCallback,
    .GetError = GetError,
    .properties = properties,
    .numProperties = sizeof(properties) / sizeof(properties[0]),
};

ImgDevDriver *
GetDriver_c_max9286_9271_ov10640lsoff(void)
{
    return &device;
}
