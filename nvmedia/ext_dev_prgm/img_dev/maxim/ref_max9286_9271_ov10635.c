/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "ref_max9286_9271_ov10635.h"
#include "isc_max9271.h"
#include "isc_max9286.h"
#include "isc_ov10635.h"
#include "log_utils.h"
#include "error_max9286.h"
#include "dev_property.h"
#include "dev_map.h"

static void
Deinit(ExtImgDevice *device)
{
    unsigned int i;

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
    ExtImgDevice *device
)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    ConfigureInputModeMAX9271 inputModeMAX9271 = { .bits = {
                              .dbl = ISC_INPUT_MODE_MAX9271_DOUBLE_INPUT_MODE,
                              .drs = ISC_INPUT_MODE_MAX9271_HIGH_DATA_RATE_MODE,
                              .bws = ISC_INPUT_MODE_MAX9271_BWS_24_BIT_MODE,
                              .es = ISC_INPUT_MODE_MAX9271_PCLKIN_RISING_EDGE,
                              .reserved = 0,
                              .hven = ISC_INPUT_MODE_MAX9271_HVEN_ENCODING_ENABLE,
                              .edc = ISC_INPUT_MODE_MAX9271_EDC_1_BIT_PARITY}};
    ConfigurePixelInfoMAX9286 pixelInfoMAX9286 = { .bits = {
                              .type = ISC_DATA_TYPE_MAX9286_YUV422_8BIT,
                              .dbl = ISC_WORD_MODE_MAX9286_DOUBLE,
                              .csi_dbl = ISC_CSI_MODE_MAX9286_DOUBLE,
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
        // enable reverse channel
        LOG_DBG("%s: Enable reverse channel\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSerializer,
                                ISC_CONFIG_MAX9271_ENABLE_REVERSE_CHANNEL);
        if(status != NVMEDIA_STATUS_OK)
            LOG_INFO("%s: Can return error while enabling reverse channel\n", __func__);

        // wait for configuration link to establish
        usleep(5000);

        LOG_DBG("%s: Set serielizer device defaults\n", __func__);
        status = NvMediaISCSetDefaults(device->iscBroadcastSerializer);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set serializer defaults\n", __func__);
            return status;
        }
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

    paramsMAX9286.SetPixelInfo.pixelInfo = &pixelInfoMAX9286;
    // Set data type
    LOG_DBG("%s: Set aggregator's data type\n", __func__);
    status = NvMediaISCWriteParameters(device->iscDeserializer,
                     ISC_WRITE_PARAM_CMD_MAX9286_SET_PIXEL_INFO,
                     sizeof(paramsMAX9286.SetPixelInfo),
                     &paramsMAX9286);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to set data type\n", __func__);
        return status;
    }

    if(!configParam->slave) {
        paramsMAX9286.SetFsyncMode.syncMode = ISC_SET_FSYNC_MAX9286_FSYNC_SEMI_AUTO;
        paramsMAX9286.SetFsyncMode.k_val = 0x1;
    } else {
        paramsMAX9286.SetFsyncMode.syncMode = ISC_SET_FSYNC_MAX9286_DISABLE_SYNC;
    }

    // Set sync mode for ov10635
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

    if(device->iscDeserializer) {
        /* E2580 specific */
        if(configParam->board && (strcasecmp(configParam->board, "e2580") == 0) &&
            (device->property.interface == NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_A ||
             device->property.interface == NVMEDIA_IMAGE_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB))
        {
            LOG_DBG("%s: Swap aggregator's CSI data lanes\n", __func__);
            status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                               ISC_CONFIG_MAX9286_SWAP_DATA_LANES);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to swap aggregator data lanes\n", __func__);
                return status;
            }
        }
    }

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

    // Check sensor presence and set up i2c translator
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
                }
            }

            // Set address translation for the serializer to control individual serializer
            if(configParam->serAddr[i] &&
                device->iscBroadcastSerializer &&
                device->iscSerializer[i]) {
                WriteReadParametersParamMAX9271 paramsMAX9271;

                // Set unique address with broadcase address
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

    if(device->iscBroadcastSensor) {
        LOG_DBG("%s: Set image sensor defaults\n", __func__);
        status = NvMediaISCSetDefaults(device->iscBroadcastSensor);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set image sensor defaults\n", __func__);
            return status;
        }

        // Set for sync mode
        LOG_DBG("%s: Set sensor configuration for enabling sync mode\n", __func__);
        status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                            ISC_CONFIG_OV10635_SYNC);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to set sensor configuration for enabling sync mode\n", __func__);
                return status;
        }
    }

    if(configParam->enableExtSync) {
        paramsMAX9286.SetFsyncMode.syncMode = ISC_SET_FSYNC_MAX9286_EXTERNAL_FROM_ECU;
        paramsMAX9286.SetFsyncMode.k_val = 1;

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
    }
    // Enable aggregator's auto ack
    LOG_DBG("%s: Enable aggregator's auto ack\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscDeserializer,
                                       ISC_CONFIG_MAX9286_ENABLE_AUTO_ACK);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enable aggregator's auto ack\n", __func__);
        return status;
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
    ContextMAX9286 ctxMAX9286;

    memset(&ctxMAX9286, 0, sizeof(ContextMAX9286));

    if(!configParam)
        return NULL;

    device = calloc(1, sizeof(ExtImgDevice));
    if(!device) {
            LOG_ERR("%s: out of memory\n", __func__);
            return NULL;
    }

    for(i = 0; i < MAX_AGGREGATE_IMAGES; i++) {
        // get remapped index of link i if CSI remapping bitmask is given
        remap = (configParam->camMap) ? EXTIMGDEV_MAP_LINK_CSIOUT(configParam->camMap->csiOut, i) : i;
        remapIdx[remap] = i;
    }

    LOG_INFO("%s: Set image device property\n", __func__);
    status = ImgDevSetProperty(GetDriver_ref_max9286_9271_ov10635(),
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
            ctxMAX9286.reverseChannelAmp = 0;  // default

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
                        (configParam->camMap == NULL) ? NULL : &advConfig);   // advancedConfig
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

    if(configParam->brdcstSensorAddr) {
        // Create the image sensor device
        LOG_INFO("%s: Create broadcast sensor device on address 0x%x\n", __func__,
                         configParam->brdcstSensorAddr);
        device->iscBroadcastSensor = NvMediaISCDeviceCreate(
                                        device->iscRoot,
                                        device->iscBroadcastSerializer,
                                        0,
                                        configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                                             configParam->brdcstSensorAddr,
                                        GetOV10635Driver(),
                                        NULL);
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
                                        device->iscSerializer[i] ? device->iscSerializer[i] :
                                                                   device->iscBroadcastSerializer,
                                        remapIdx[i],
                                        configParam->slave ? NVMEDIA_ISC_SIMULATOR_ADDRESS :
                                                             configParam->sensorAddr[i],
                                        GetOV10635Driver(),
                                        NULL);
            if(!device->iscSensor[i]) {
                LOG_ERR("%s: Failed to create image sensor device\n", __func__);
                goto failed;
            }
        }
    }

    if(configParam->initialized || configParam->enableSimulator)
        goto init_done;

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

    LOG_DBG("%s: Enable sensor streaming\n", __func__);
    status = NvMediaISCSetDeviceConfig(device->iscBroadcastSensor,
                                       ISC_CONFIG_OV10635_ENABLE_STREAMING);
    if (status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to enable sensor streaming\n", __func__);
        return status;
    }

    // TODO: Without this sleep, sometimes aggregator can't detect video link,
    // once we find the way the timing, will remove this delay
    if(!device->simulator) {
        for(i = 0; i < device->sensorsNum; i++) {
            unsigned int timeout = 2000;
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
    void *context
)
{
    if(!device)
        return NVMEDIA_STATUS_ERROR;

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
    if(!device)
        return NVMEDIA_STATUS_ERROR;

    return _GetError_max9286(device->iscDeserializer, link, errorType);
}

static ImgProperty properties[] = {
                   /* resolution, oscMHz, fps,   pclk,  embTop, embBottom, inputFormat, pixelOrder */
    IMG_PROPERTY_ENTRY(1280x800,     24,  30, 48006000,      0,         0,        422p,        yuv),
};


static ImgDevDriver device = {
    .name = "ref_max9286_9271_ov10635",
    .Init = Init,
    .Deinit = Deinit,
    .Start = Start,
    .RegisterCallback = RegisterCallback,
    .GetError = GetError,
    .properties = properties,
    .numProperties = sizeof(properties) / sizeof(properties[0]),
};

ImgDevDriver *
GetDriver_ref_max9286_9271_ov10635(void)
{
    return &device;
}
