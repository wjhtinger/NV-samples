/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "ref_max9286_96705_ar0231_rccb.h"
#include "isc_max96705.h"
#include "isc_max9286.h"
#include "isc_ar0231_rccb.h"
#include "log_utils.h"
#include "error_max9286.h"
#include "dev_property.h"
#include "dev_map.h"

#define TEMPERATURE_SENSOR_ENABLE       1

#define OSC_MHZ 27
#define MAX_GAIN (3.0 * 8 * (3 + 511 / 512.0))   //v4 max gain = 8

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
    if(device->iscRoot) {
        if(device->property.enableExtSync) {
            NvMediaISCRootDeviceEnableSync(
                             device->iscRoot,
                             NVMEDIA_FALSE);
        }
        NvMediaISCRootDeviceDestroy(device->iscRoot);
    }

    free(device);

    return;
}

static
NvMediaStatus
SetupConfigLink(
    ExtImgDevParam *configParam,
    ExtImgDevice *device)
{
    NvMediaStatus status;

    if((!configParam) || (!device))
        return NVMEDIA_STATUS_ERROR;

    ConfigureInputModeMAX96705 inputModeMAX96705 = { .bits = { // DBL=1, HIBW=1, rising edge
                               .dbl  = ISC_INPUT_MODE_MAX96705_DOUBLE_INPUT_MODE,
                               .hibw = ISC_INPUT_MODE_MAX96705_HIGH_BANDWIDTH_MODE,
                               .bws = ISC_INPUT_MODE_MAX96705_BWS_22_BIT_MODE,
                               .es = ISC_INPUT_MODE_MAX96705_PCLKIN_RISING_EDGE,
                               .reserved = 0,
                               .hven = ISC_INPUT_MODE_MAX96705_HVEN_ENCODING_ENABLE,
                               .edc = ISC_INPUT_MODE_MAX96705_EDC_1_BIT_PARITY}};
    ConfigurePixelInfoMAX9286  pixelInfoMAX9286 = { .bits = { // DBL=1, CSI_DBL=1 raw12
                               .type = ISC_DATA_TYPE_MAX9286_RAW11_RAW12,
                               .dbl = ISC_WORD_MODE_MAX9286_DOUBLE,
                               .csi_dbl = ISC_CSI_MODE_MAX9286_DOUBLE,
                               .lane_cnt = 3}};

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

        // wait after any change to reverse channel settings
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
       LOG_DBG("%s: Enable Serializer reverse channel\n", __func__);
       status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                               ISC_CONFIG_MAX96705_ENABLE_REVERSE_CHANNEL);
       if(status != NVMEDIA_STATUS_OK)
           LOG_INFO("%s: Failed to enable Serializer reverse channel failed\n", __func__);
       usleep(5000);  //wait 5ms

       LOG_DBG("%s: Set Serielizer device defaults\n", __func__);
       status = NvMediaISCSetDefaults(device->iscBroadcastSerializer);
       if(status != NVMEDIA_STATUS_OK) {
           LOG_ERR("%s: Failed to set Serializer defaults\n", __func__);
           return status;
       }
       usleep(2000);  //wait 2ms

       /*
        * set the max remote-i2c-master timeout in MAX96705 to prevent timeout in remote-i2c-master
        * while transferring i2c data from the actual i2c master (Bug 1802338)
        */
       status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                          ISC_CONFIG_MAX96705_SET_MAX_REMOTE_I2C_MASTER_TIMEOUT);
       if(status != NVMEDIA_STATUS_OK) {
           LOG_ERR("%s: Failed to set the max remote-i2c-master timeout\n", __func__);
           return status;
       }
    }

    LOG_DBG("%s: Increase Des reverse channel amplitude\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                              ISC_CONFIG_MAX9286_REVERSE_CHANNEL_AMPL_H);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to increase Des reverse channel amplitude\n", __func__);
        return status;
    }
    usleep(2000);  //wait 2ms

    LOG_DBG("%s: Disable csi out\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                              ISC_CONFIG_MAX9286_DISABLE_CSI_OUT);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to disable csi out\n", __func__);
        return status;
    }

    if(device->iscDeserializer) {
        //set deserializer input mode
        WriteParametersParamMAX9286 paramsMAX9286;
        paramsMAX9286.SetPixelInfo.pixelInfo = &pixelInfoMAX9286;

        LOG_DBG("%s: Set Deserializer input mode %x\n", __func__, pixelInfoMAX9286);
        status = NvMediaISCWriteParameters(device->iscDeserializer,
                         ISC_WRITE_PARAM_CMD_MAX9286_SET_PIXEL_INFO,
                         sizeof(paramsMAX9286.SetPixelInfo),
                         &paramsMAX9286);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set Deserializer input mode\n", __func__);
            return status;
        }

        // disable overlap window
        LOG_DBG("%s: Disable Deserializer overlap window\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                               ISC_CONFIG_MAX9286_DISABLE_OVLP_WINDOW);
        if(status != NVMEDIA_STATUS_OK) {
             LOG_ERR("%s: Failed to disable Deserializer overlap window\n", __func__);
             return status;
        }

        LOG_DBG("%s: Set Deserializer HIBW\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                               ISC_CONFIG_MAX9286_ENABLE_HIBW);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set Deserializer HIBW\n", __func__);
            return status;
        }
    }

    if(device->iscBroadcastSerializer) {
        // Set Serializer input mode
        WriteReadParametersParamMAX96705 paramsMAX96705;
        paramsMAX96705.inputmode = &inputModeMAX96705;

        LOG_DBG("%s: Set Serializer input mode\n", __func__);
        status = NvMediaISCWriteParameters(device->iscBroadcastSerializer,
                         ISC_WRITE_PARAM_CMD_MAX96705_CONFIG_INPUT_MODE,
                         sizeof(paramsMAX96705.inputmode),
                         &paramsMAX96705);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set Serializer input mode\n", __func__);
            return status;
        }
        usleep(2000);  //wait 2ms

        // Enable Ser VSYNC re-generation
        LOG_DBG("%s: Enable Serializer VSYNC re-generation\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                               ISC_CONFIG_MAX96705_REGEN_VSYNC);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable Serializer VSYNC re-generation\n", __func__);
            return status;
        }
    }

    if(device->iscDeserializer) {
        /* E2580 specific code */
        if(configParam->board && (strcasecmp(configParam->board, "e2580") == 0) &&
            (device->property.interface == NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_A ||
             device->property.interface == NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB)) {
            LOG_DBG("%s: Swap deserializer's CSI data lanes\n", __func__);
            status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                               ISC_CONFIG_MAX9286_SWAP_DATA_LANES);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to swap deserializer data lanes\n", __func__);
                return status;
            }
        }
    }

    if(device->iscBroadcastSerializer) {
        // Set PreEmphasis
        WriteReadParametersParamMAX96705 paramsMAX96705;
        paramsMAX96705.preemp = ISC_SET_PREEMP_MAX96705_PLU_6_0DB; /* Bug 1850534 */

        LOG_DBG("%s: Set all serializer Preemphasis setting\n", __func__);
        status = NvMediaISCWriteParameters(device->iscBroadcastSerializer,
                         ISC_WRITE_PARAM_CMD_MAX96705_SET_PREEMP,
                         sizeof(paramsMAX96705.preemp),
                         &paramsMAX96705);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set Preemphasis setting\n", __func__);
            return status;
        }
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
    unsigned char data[8];
    unsigned int pclk;

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

    // Disable deserializer auto ack
    LOG_DBG("%s: Disable deserializer auto ack\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                        ISC_CONFIG_MAX9286_DISABLE_AUTO_ACK);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set deserializer configuration\n", __func__);
        return status;
    }

    // Disable reverse channel for checking individual sensor presence
    LOG_DBG("%s: Disable reverse channel\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                        ISC_CONFIG_MAX9286_DISABLE_REVERSE_CHANNEL_0123);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to disable deserializer reverse channel\n", __func__);
        return status;
    }
    // 3ms needed by MAX9286 to lock the link
    usleep(3000);

    // TBD: add delay before accessing sensor at very first time
    usleep(40000);

    if(device->iscBroadcastSensor && !configParam->slave) {
        for(i = 0; i < configParam->sensorsNum; i++) {
            paramsMAX9286.EnableReverseChannel.id = remapIdx[i];
            // Enable reverse channel
            status = NvMediaISCWriteParameters(device->iscDeserializer,
                            ISC_WRITE_PARAM_CMD_MAX9286_ENABLE_REVERSE_CHANNEL,
                            sizeof(paramsMAX9286.EnableReverseChannel),
                            &paramsMAX9286);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to disable deserializer reverse channels\n", __func__);
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
                    WriteReadParametersParamMAX96705 paramsMAX96705;
                    ConfigInfoAR0231 configInfo;
                    WriteReadParametersParamAR0231 paramAR0231;
                    paramAR0231.configInfo = &configInfo;

                    // Set address translation for the sensor
                    paramsMAX96705.Translator.source = configParam->sensorAddr[i];
                    paramsMAX96705.Translator.destination = configParam->brdcstSensorAddr;
                    LOG_INFO("%s: Translate image sensor device addr %x to %x\n", __func__,
                        paramsMAX96705.Translator.source, paramsMAX96705.Translator.destination);
                    status = NvMediaISCWriteParameters(device->iscBroadcastSerializer,
                                    ISC_WRITE_PARAM_CMD_MAX96705_SET_TRANSLATOR_B,
                                    sizeof(paramsMAX96705.Translator),
                                    &paramsMAX96705);
                    if(status != NVMEDIA_STATUS_OK) {
                        LOG_ERR("%s: Address translation setup failed\n", __func__);
                        return status;
                    }

                    //  Get fuseId for each sensor
                    status = NvMediaISCReadParameters(device->iscSensor[i],
                                    ISC_READ_PARAM_CMD_AR0231_FUSE_ID,
                                    (unsigned int)(sizeof(paramAR0231.configInfo)),
                                    &paramAR0231);
                    if(status != NVMEDIA_STATUS_OK) {
                        LOG_ERR("%s: Get sensor fuseId failed\n", __func__);
                        return status;
                    }
                }
            }

            // Set address translation for the serializer to control individual serializer
            if(configParam->serAddr[i] &&
                device->iscBroadcastSerializer &&
                device->iscSerializer[i]) {
                WriteReadParametersParamMAX96705 paramsMAX96705;

                // Set unique address with broadcase address
                paramsMAX96705.DeviceAddress.address = configParam->serAddr[i];
                status = NvMediaISCWriteParameters(device->iscBroadcastSerializer,
                            ISC_WRITE_PARAM_CMD_MAX96705_SET_DEVICE_ADDRESS,
                            sizeof(paramsMAX96705.DeviceAddress),
                            &paramsMAX96705);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to set serializer device %d address\n", __func__,
                            configParam->serAddr[i]);
                    return status;
                }
                // Set address translation for the serializer
                paramsMAX96705.Translator.source = configParam->brdcstSerAddr;
                paramsMAX96705.Translator.destination = configParam->serAddr[i];
                status = NvMediaISCWriteParameters(device->iscSerializer[i],
                                ISC_WRITE_PARAM_CMD_MAX96705_SET_TRANSLATOR_A,
                                sizeof(paramsMAX96705.Translator),
                                &paramsMAX96705);
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
                LOG_ERR("%s: Failed to disable deserializer reverse channel\n", __func__);
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
            LOG_ERR("%s: Failed to enable deserializer reverse channels\n", __func__);
            return status;
        }
        // 4ms needed by MAX9286 to lock the link
        usleep(4000);
    }

    status = GetAR0231RccbConfigSet(configParam->resolution,
                                    configParam->inputFormat,
                                    &config,
                                    device->property.frameRate);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to get config set\n", __func__);
        return status;
    }

    if(device->iscBroadcastSensor) {
        // Set sensor defaults after software reset
        LOG_DBG("%s: Set AR0231 rccb defaults\n", __func__);
        status = NvMediaISCSetDefaults(device->iscBroadcastSensor);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set AR0231 rccb defaults\n", __func__);
            return status;
        }

        // Enable bottom emb stats

        LOG_DBG("%s: Enable bottom emb\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                           ISC_CONFIG_AR0231_ENABLE_BOTTOM_EMB);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable bottom emb\n", __func__);
            return status;
        }

        //read sensor AR0231 rccb PLL registers to get pclk
        status = NvMediaISCReadRegister(device->iscBroadcastSensor,
                                   AR0231_REG_PLL_VT_PIXDIV, 8, data);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get sensor PCLK\n", __func__);
            return status;
        }
        // pre_pll_clk_div:data[5]; pll_muliplier:data[7];
        // vt_sys_clk_div_CLK_DIV:data[3]; vt_pix_clk_div:data[1]
        pclk = OSC_MHZ / data[5] * data[7] / data[3] / data[1]  * 1000000;
        LOG_DBG("%s: Get sensor pclk: %d Hz\n", __func__, pclk);

        // Only to be set by master Tegra when capturing with two or more Tegras
        if(!configParam->slave) {
            switch(configParam->enableExtSync) {
                case 1:
                    LOG_DBG("%s: Eanble external synchronization", __func__);
                    paramsMAX9286.SetFsyncMode.syncMode = ISC_SET_FSYNC_MAX9286_EXTERNAL_FROM_ECU;
                    paramsMAX9286.SetFsyncMode.k_val = (int)(pclk/device->property.frameRate + 0.5);  //pclk per frame
                    break;
                case 0:
                default :
                    //set Ders fsync mode to manual and each periord has k_val pclk
                    paramsMAX9286.SetFsyncMode.syncMode = ISC_SET_FSYNC_MAX9286_FSYNC_MANUAL;
                    paramsMAX9286.SetFsyncMode.k_val = (int)(pclk/device->property.frameRate + 0.5);  //pclk per frame
            }
        } else {
            paramsMAX9286.SetFsyncMode.syncMode = ISC_SET_FSYNC_MAX9286_DISABLE_SYNC;
        }

        // Set sync mode for Deserializer
        LOG_DBG("%s: Set deserializer's fsync mode\n", __func__);
        status = NvMediaISCWriteParameters(device->iscDeserializer,
                            ISC_WRITE_PARAM_CMD_MAX9286_SET_SYNC_MODE,
                            sizeof(paramsMAX9286.SetFsyncMode),
                            &paramsMAX9286);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set fsync mode\n", __func__);
            return status;
        }

        // Enables streamming
        LOG_DBG("%s: Set sensor configuration (%u)\n", __func__, config);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                            config);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set sensor configuration\n", __func__);
            return status;
        }
    }

    // Enable deserializer's auto ack
    LOG_DBG("%s: Enable deserializer's auto ack\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                       ISC_CONFIG_MAX9286_ENABLE_AUTO_ACK);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enable deserializer's auto ack\n", __func__);
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
            LOG_ERR("%s: Failed to set deserializer configuration\n", __func__);
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

    return status;
}

static ExtImgDevice *
Init(
    ExtImgDevParam *configParam)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvU32 i, remap;
    ExtImgDevice *device = NULL;
    NvMediaISCAdvancedConfig advConfig;
    ContextAR0231 ctxAR0231;
    NvU32 remapIdx[MAX_AGGREGATE_IMAGES] = {0};
    ContextMAX9286 ctxMAX9286;

    if(!configParam)
        return NULL;

    for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
        // get remapped index of link i if CSI remapping bitmask is given
        remap = (configParam->camMap) ? EXTIMGDEV_MAP_LINK_CSIOUT(configParam->camMap->csiOut, i) : i;
        remapIdx[remap] = i;
    }

    memset(&ctxAR0231, 0, sizeof(ContextAR0231));
    memset(&ctxMAX9286,0,sizeof(ContextMAX9286));

    device = calloc(1, sizeof(ExtImgDevice));
    if(!device) {
        LOG_ERR("%s: out of memory\n", __func__);
        return NULL;
    }

    LOG_INFO("%s: Set image device property\n", __func__);
    status = ImgDevSetProperty(GetDriver_ref_max9286_96705_ar0231rccb(),
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

    if(configParam->enableExtSync) {
        status = NvMediaISCRootDeviceEnableSync(
                             device->iscRoot,
                             NVMEDIA_TRUE);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable Sync\n", __func__);
            goto failed;
        }

        status = NvMediaISCRootDeviceSetSyncConfig(
                             device->iscRoot,
                             device->property.frameRate,
                             ((device->property.dutyRatio <= 0.0) || (device->property.dutyRatio >= 1.0)) ?
                                 0.25 : device->property.dutyRatio);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to config Sync\n", __func__);
            goto failed;
        }
    }

    // Delay for 50 ms in order to let sensor power on
    usleep(50000);

    if(configParam->desAddr) {
        /* Do not configure modules behind the aggregator when slave mode enabled */
        ctxMAX9286.disableBackChannelCtrl = (configParam->slave) ? NVMEDIA_TRUE : NVMEDIA_FALSE;

        // check if camMap valid
        if(configParam->camMap != NULL) {
            status = ExtImgDevCheckValidMap(configParam->camMap);
            if(status != NVMEDIA_STATUS_OK)
                goto failed;
            memcpy(&ctxMAX9286.camMap,configParam->camMap,sizeof(CamMap));
        }

        /*
         * Set enabling bit order swap since bits are swapped between ar0231 and max96705
         * so we need to swap in the MAX9286
         */
        ctxMAX9286.enSwapBitOrder = NVMEDIA_TRUE;
        ADV_CONFIG_INIT(advConfig, &ctxMAX9286);

        // Create the deserializer device
        LOG_INFO("%s: Create deserializer device on address 0x%x\n", __func__, configParam->desAddr);
        device->iscDeserializer = NvMediaISCDeviceCreate(
                            device->iscRoot,     // rootDevice
                            NULL,                   // parentDevice
                            0,                      // instanceNumber
                            configParam->desAddr,   // deviceAddress
                            GetMAX9286Driver(),     // deviceDriver
                            &advConfig);  // advancedConfig
        if(!device->iscDeserializer) {
            LOG_ERR("%s: Failed to create deserializer device\n", __func__);
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
                          GetMAX96705Driver(),
                          NULL);
        if(!device->iscBroadcastSerializer) {
            LOG_ERR("%s: Failed to create broadcase serializer device\n", __func__);
            goto failed;
        }
    }

    ctxAR0231.oscMHz = OSC_MHZ;
    ctxAR0231.maxGain = MAX_GAIN;
    /* set frameRate from device property */
    ctxAR0231.frameRate = device->property.frameRate;
    /* set config set */
    ctxAR0231.configSetIdx = device->property.enableExtSync;
    status = ImgDevGetModuleConfig(&ctxAR0231.moduleConfig, configParam->moduleName);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to get camera module config file name\n", __func__);
    }

    if(configParam->brdcstSensorAddr) {
        ADV_CONFIG_INIT(advConfig, &ctxAR0231);
        // Create the image sensor device
        LOG_INFO("%s: Create broadcast sensor device on address 0x%x\n", __func__,
                         configParam->brdcstSensorAddr);
        device->iscBroadcastSensor = NvMediaISCDeviceCreate(
                                        device->iscRoot,
                                        device->iscBroadcastSerializer,
                                        0,
                                        configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                                             configParam->brdcstSensorAddr,
                                        GetAR0231RccbDriver(),
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
                        GetMAX96705Driver(),
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
            ADV_CONFIG_INIT(advConfig, &ctxAR0231);
            device->iscSensor[i] = NvMediaISCDeviceCreate(
                                        device->iscRoot,
                                        device->iscSerializer[i] ? device->iscSerializer[i] :
                                                                   device->iscBroadcastSerializer,
                                        remapIdx[i],
                                        configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                                             configParam->sensorAddr[i],
                                        GetAR0231RccbDriver(),
                                        &advConfig);
            if(!device->iscSensor[i]) {
                LOG_ERR("%s: Failed to create image sensor device\n", __func__);
                goto failed;
            }
        }
    }

    if(configParam->initialized || configParam->enableSimulator) {
        ConfigInfoAR0231 configInfo;
        WriteReadParametersParamAR0231 paramAR0231;
        paramAR0231.configInfo = &configInfo;

        status = NvMediaISCReadParameters(device->iscBroadcastSensor,
                        ISC_READ_PARAM_CMD_AR0231_CONFIG_INFO,
                        sizeof(paramAR0231.configInfo),
                        &paramAR0231);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get config\n", __func__);
            goto failed;
        }

        status = GetAR0231RccbConfigSet(configParam->resolution,
                                        configParam->inputFormat,
                                        &configInfo.enumeratedDeviceConfig,
                                        device->property.frameRate);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get config set\n", __func__);
            goto failed;
        }

        status = NvMediaISCReadParameters(device->iscBroadcastSensor,
                        ISC_READ_PARAM_CMD_AR0231_EXP_LINE_RATE,
                        sizeof(paramAR0231.configInfo),
                        &paramAR0231);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get line rate\n", __func__);
            goto failed;
        }

        LOG_INFO("%s: Update config info for boardcastSensorDevice\n", __func__);

        status = NvMediaISCWriteParameters(device->iscBroadcastSensor,
                        ISC_WRITE_PARAM_CMD_AR0231_CONFIG_INFO,
                        sizeof(paramAR0231.configInfo),
                        &paramAR0231);
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
        ConfigInfoAR0231 configInfo;
        WriteReadParametersParamAR0231 paramAR0231;
        paramAR0231.configInfo = &configInfo;

        status = NvMediaISCReadParameters(device->iscBroadcastSensor,
                        ISC_READ_PARAM_CMD_AR0231_CONFIG_INFO,
                        sizeof(paramAR0231.configInfo),
                        &paramAR0231);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to get config\n", __func__);
            goto failed;
        }

        for(i = 0; i < configParam->sensorsNum; i++) {
            if(device->iscSensor[i]) {
                LOG_DBG("%s: Set sensor[%d] config\n", __func__, i);
                status = NvMediaISCWriteParameters(device->iscSensor[i],
                                ISC_WRITE_PARAM_CMD_AR0231_CONFIG_INFO,
                                sizeof(paramAR0231.configInfo),
                                &paramAR0231);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Failed to set config\n", __func__);
                    goto failed;
                }
            }
        }

        LOG_DBG("%s: Reset sensor frame id\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                              ISC_CONFIG_AR0231_RESET_FRAME_ID);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to reset sensor frame id\n", __func__);
            goto failed;
        }

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
    NvU32 i;
    NvMediaStatus status;

    if(!device)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(device->iscBroadcastSensor) {
        if(device->property.enableExtSync) {
            // Wait for PLL locked
            usleep(50000);
        }

        LOG_DBG("%s: Enable sensor streaming\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                           ISC_CONFIG_AR0231_ENABLE_STREAMING);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable sensor streaming\n", __func__);
            return status;
        }
    }

    if(device->iscBroadcastSerializer) {
        // Enable each serial link
        LOG_DBG("%s: Enable serial link\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                           ISC_CONFIG_MAX96705_ENABLE_SERIAL_LINK);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to enable serial link\n", __func__);
            return status;
        }
        usleep(5000);  //wait 5ms
    }

    if(!device->simulator) {
        for(i = 0; i < device->sensorsNum; i++) {
            NvU32 timeout = 2000;  //test shows ok on 1st loop, may remove timeout
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

    if((TEMPERATURE_SENSOR_ENABLE) && (!(device->simulator))) {
        for(i = 0; i < device->sensorsNum; i++) {
            if(device->iscSensor[i]) {
                status = NvMediaISCSetDeviceConfig(device->iscSensor[i],
                                ISC_CONFIG_AR0231_ENABLE_TEMP_SENSOR);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Temperature enable failed \n", __func__);
                    return status;
                }

                LOG_DBG("%s: Temperature enabled \n", __func__);
            } else {
                continue;
            }
        }
    }

    if(device->property.enableExtSync) {
        // Wait for about 6 VSYNC for cameras to synchronize with extra 1 VSYNC
        usleep(7 * 1000000u / device->property.frameRate);
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
    void *context)
{
    return NvMediaISCRootDeviceRegisterCallback(device->iscRoot,
                                                sigNum, cb, context);
}

static NvMediaStatus
GetError(
    ExtImgDevice *device,
    NvU32 *link,
    ExtImgDevFailureType *errorType)
{
    return _GetError_max9286(device->iscDeserializer, link, errorType);
}

static ImgProperty properties[] = {
                   /* resolution, oscMHz, fps,      pclk,    embTop, embBottom, inputFormat, pixelOrder */
    IMG_PROPERTY_ENTRY(1920x1208, OSC_MHZ, 30, 88000000,       24,      4,       raw12,       grbg), /* default resolution and frame rate */
    IMG_PROPERTY_ENTRY(1920x1208, OSC_MHZ, 20, 88000000,       24,      4,       raw12,       grbg),
    IMG_PROPERTY_ENTRY(1920x1008, OSC_MHZ, 36, 88000000,       16,      4,       raw12,       grbg),
};

static ImgDevDriver device = {
    .name = "ref_max9286_96705_ar0231rccb",
    .Init = Init,
    .Deinit = Deinit,
    .Start = Start,
    .RegisterCallback = RegisterCallback,
    .GetError = GetError,
    .properties = properties,
    .numProperties = sizeof(properties) / sizeof(properties[0]),
};

ImgDevDriver *
GetDriver_ref_max9286_96705_ar0231rccb(void)
{
    return &device;
}
