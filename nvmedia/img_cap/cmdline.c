/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "cmdline.h"

static void
_PrintCaptureParamSets(TestArgs *args)
{
    NvU32 j;

    LOG_MSG("Capture parameter sets (%d):\n", args->captureConfigSetsNum);
    for (j = 0; j < args->captureConfigSetsNum; j++) {
        LOG_MSG("\n%s: ", args->captureConfigCollection[j].name);
        LOG_MSG("%s\n", args->captureConfigCollection[j].description);
    }
    LOG_MSG("\n");
}

static int
_GetParamSetID(void *configSetsList,
               int paramSets,
               char* paramSetName)
{
    int i;
    char *name = NULL;

    for (i = 0; i < paramSets; i++) {
        name = ((CaptureConfigParams *)configSetsList)[i].name;

        if (!strcasecmp(name, paramSetName)) {
            LOG_DBG("%s: Param set found (%d)\n", __func__, i);
            return i;
        }
    }
    return -1;
}

void
PrintUsage(void)
{
    NvMediaIDPDeviceParams outputs[MAX_OUTPUT_DEVICES];
    char displayName[MAX_STRING_SIZE];
    int outputDevicesNum = 0, i;

    NvMediaIDPQuery(&outputDevicesNum, outputs);

    LOG_MSG("Usage: nvmimg_cap [options]\n");
    LOG_MSG("\nAvailable command line options:\n");
    LOG_MSG("-h                Print usage\n");
    LOG_MSG("-v [level]        Logging Level. Default = 0\n");
    LOG_MSG("                  0: Errors, 1: Warnings, 2: Info, 3: Debug\n");
    LOG_MSG("                  Default: 0\n");
    LOG_MSG("-d [n]            Set display ID\n");
    LOG_MSG("                  Available display devices (%d):\n", outputDevicesNum);
    for (i = 0; i < outputDevicesNum; i++) {
        if (IsSucceed(GetImageDisplayName(outputs[i].type, displayName)))
            LOG_MSG("                  Display ID: %d (%s)\n", outputs[i].displayId, displayName);
    }
    LOG_MSG("-n [frames]       Number of frames to capture and quit automatically \n");
    LOG_MSG("-f [file-prefix]  Save frames to file. Provide filename pre-fix for each saved file\n");
    LOG_MSG("-w [n]            Set display window ID [0-2]\n");
    LOG_MSG("-z [n]            Set display window depth [0-255]\n");
    LOG_MSG("-p [position]     Window position. Default: full screen size\n");
    LOG_MSG("-cf [file]        Set configuration file.\n");
    LOG_MSG("                  Default: configs/default.conf\n");
    LOG_MSG("-c [name]         Parameters set name to be used for capture configuration.\n");
    LOG_MSG("                  Default: First config set (capture-params-set)\n");
    LOG_MSG("-lc               List all available config sets.\n");
    LOG_MSG("--aggregate [n]   Capture images from n camera sensors\n");
    LOG_MSG("                  Default: n = 1\n");
    LOG_MSG("--ext_sync [n]    Enable the external synchronization with <n> duty ratio; 0.0 < n < 1.0\n");
    LOG_MSG("                  If n is out of the range, set 0.25 to the duty ratio by default\n");
    LOG_MSG("                  If this option is not set, the synchronization will be handled by the aggregator\n");
    LOG_MSG("--cam_enable [n]  Enable or disable camera[3210]; enable:1, disable 0\n");
    LOG_MSG("                  Default: n = 0001\n");
    LOG_MSG("--cam_mask [n]    Mask or unmask camera[3210]; mask:1, unmask:0\n");
    LOG_MSG("                  Default: n = 0000\n");
    LOG_MSG("--csi_outmap [n]  Set csi out order for camera[3210]; 0,1,2 or 3\n");
    LOG_MSG("                  Default: n = 3210\n");
    LOG_MSG("--vc_enable       Enable virtual channels for capturing the frames\n");
    LOG_MSG("--slave           Application is being run on a slave Tegra\n");
    LOG_MSG("\nAvailable run-time commands:\n");
    LOG_MSG("h                 Print available commands\n");
    LOG_MSG("reset: <id>       Reset sensor with provided id\n");
    LOG_MSG("power off: <id>   Power off sensor with provided id\n");
    LOG_MSG("power on: <id>    Power on sensor with provided id\n");
    LOG_MSG("temperature: <id> Get temperature of sensor with provided id\n");
    LOG_MSG("q                 Quit the application\n");
}

NvMediaStatus
ParseArgs(int argc,
          char *argv[],
          TestArgs *allArgs)
{
    int i = 0;
    NvMediaBool bLastArg = NVMEDIA_FALSE;
    NvMediaBool bDataAvailable = NVMEDIA_FALSE;
    NvU32 x, y, w, h, j;

    // Default parameters
    allArgs->numSensors = 1;
    allArgs->numVirtualChannels = 1;
    allArgs->camMap.enable = CAM_ENABLE_DEFAULT;
    allArgs->camMap.mask   = CAM_MASK_DEFAULT;
    allArgs->camMap.csiOut = CSI_OUT_DEFAULT;

    if (argc < 2) {
        PrintUsage();
        return NVMEDIA_STATUS_ERROR;
    }

    if (argc >= 2) {
        for (i = 1; i < argc; i++) {
            // Check if this is the last argument
            bLastArg = ((argc - i) == 1);

            // Check if there is data available to be parsed
            bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

            if (!strcasecmp(argv[i], "-h")) {
                PrintUsage();
                return NVMEDIA_STATUS_ERROR;
            } else if (!strcasecmp(argv[i], "-v")) {
                allArgs->logLevel = LEVEL_DBG;
                if (bDataAvailable) {
                    allArgs->logLevel = atoi(argv[++i]);
                    if (allArgs->logLevel > LEVEL_DBG) {
                        printf("Invalid logging level chosen (%d)\n",
                               allArgs->logLevel);
                        printf("Setting logging level to LEVEL_ERR (0)\n");
                        allArgs->logLevel = LEVEL_ERR;
                    }
                }
                SetLogLevel((enum LogLevel)allArgs->logLevel);
            } else if (!strcasecmp(argv[i], "-cf")) {
                if (argv[i + 1] && argv[i + 1][0] != '-') {
                    strncpy(allArgs->configFile.stringValue, argv[++i],
                            MAX_STRING_SIZE);
                    allArgs->configFile.isUsed = NVMEDIA_TRUE;
                } else {
                    LOG_ERR("-cf must be followed by configuration file name\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            }
        }
    }

    // Default config file
    if (!allArgs->configFile.isUsed) {
        strcpy(allArgs->configFile.stringValue,
               "configs/default.conf");
        allArgs->configFile.isUsed = NVMEDIA_TRUE;
    }

    // Parse config file here
    if (IsFailed(ParseConfigFile(allArgs->configFile.stringValue,
                                &allArgs->captureConfigSetsNum,
                                &allArgs->captureConfigCollection))) {
        LOG_ERR("Failed to parse config file %s\n",
                allArgs->configFile.stringValue);
        return NVMEDIA_STATUS_ERROR;
    }

    if (argc >= 2) {
        for (i = 1; i < argc; i++) {
            // Check if this is the last argument
            bLastArg = ((argc - i) == 1);

            // Check if there is data available to be parsed
            bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

            if (!strcasecmp(argv[i], "-h")) {
                PrintUsage();
                return NVMEDIA_STATUS_ERROR;
            } else if (!strcasecmp(argv[i], "-v")) {
                if (bDataAvailable)
                    ++i;
            } else if (!strcasecmp(argv[i], "-cf")) {
                ++i; // Was already parsed at the beginning. Skipping.
            }  else if (!strcasecmp(argv[i], "-lc")) {
                _PrintCaptureParamSets(allArgs);
                return NVMEDIA_STATUS_ERROR;
            } else if (!strcasecmp(argv[i], "-f")) {
                if (argv[i + 1] && argv[i + 1][0] != '-') {
                    strncpy(allArgs->filePrefix.stringValue, argv[++i],
                            MAX_STRING_SIZE);
                } else {
                    LOG_ERR("-f must be followed by a file prefix string\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                allArgs->filePrefix.isUsed = NVMEDIA_TRUE;
            } else if (!strcasecmp(argv[i], "-d")) {
                if (bDataAvailable) {
                    allArgs->displayId.isUsed = NVMEDIA_TRUE;
                    if ((sscanf(argv[++i], "%u", &allArgs->displayId.uIntValue)
                       != 1)) {
                        LOG_ERR("Bad display id: %s\n", argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                }
                allArgs->displayEnabled = NVMEDIA_TRUE;
            }  else if (!strcasecmp(argv[i], "-w")) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    allArgs->windowId.uIntValue = atoi(arg);
                } else {
                    LOG_ERR("-w must be followed by window id\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                if (allArgs->windowId.uIntValue > 2) {
                    LOG_WARN("Bad window ID: %d. Using default window ID 0\n",
                             allArgs->windowId);
                    allArgs->windowId.uIntValue = 0;
                }
                allArgs->windowId.isUsed = NVMEDIA_TRUE;
            } else if (!strcasecmp(argv[i], "-z")) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    allArgs->depth.uIntValue = atoi(arg);
                } else {
                    LOG_ERR("-z must be followed by depth value\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                if (allArgs->depth.uIntValue > 255) {
                    LOG_WARN("Bad depth value: %d. Using default value: 1\n",
                             allArgs->depth);
                    allArgs->depth.uIntValue = 1;
                }
                allArgs->depth.isUsed = NVMEDIA_TRUE;
            } else if (!strcasecmp(argv[i], "-p")) {
                if (bDataAvailable) {
                    if ((sscanf(argv[++i], "%u:%u:%u:%u", &x, &y, &w, &h)
                       != 4)) {
                        LOG_ERR("Bad resolution: %s\n", argv[i]);
                        return NVMEDIA_STATUS_BAD_PARAMETER;
                    }
                    allArgs->position.x0 = x;
                    allArgs->position.y0 = y;
                    allArgs->position.x1 = x + w;
                    allArgs->position.y1 = y + h;
                    allArgs->positionSpecifiedFlag = NVMEDIA_TRUE;
                    LOG_INFO("Output position set to: %u:%u:%u:%u\n",
                             x, y, x + w, y + h);
                } else {
                    LOG_ERR("-p must be followed by window position x0:x1:W:H\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            } else if (!strcasecmp(argv[i], "-c")) {
                if (bDataAvailable) {
                    ++i;
                    int paramSetId = 0;
                    paramSetId = _GetParamSetID(allArgs->captureConfigCollection,
                                               allArgs->captureConfigSetsNum,
                                               argv[i]);
                    if (paramSetId == -1) {
                        LOG_ERR("Params set name '%s' wasn't found\n",
                                argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                    allArgs->config[0].isUsed = NVMEDIA_TRUE;
                    allArgs->config[0].uIntValue = paramSetId;
                    LOG_INFO("Using params set: %s for capture\n",
                             allArgs->captureConfigCollection[paramSetId].name);
                } else {
                    LOG_ERR("-c must be followed by capture parameters set name\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            } else if (!strcasecmp(argv[i], "--aggregate")) {
                allArgs->useAggregationFlag = NVMEDIA_TRUE;
                if (bDataAvailable) {
                    if ((sscanf(argv[++i], "%u", &allArgs->numSensors) != 1)) {
                        LOG_ERR("Bad siblings number: %s\n", argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                } else {
                    LOG_ERR("--aggregate must be followed by number of images to aggregate\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                allArgs->camMap.enable = EXTIMGDEV_MAP_N_TO_ENABLE(allArgs->numSensors);
            } else if (!strcasecmp(argv[i], "--slave")) {
                allArgs->slaveTegra = NVMEDIA_TRUE;
            } else if (!strcasecmp(argv[i], "--vc_enable")) {
                allArgs->useVirtualChannels= NVMEDIA_TRUE;
            } else if (!strcasecmp(argv[i], "--cam_enable")) {
                allArgs->useAggregationFlag = NVMEDIA_TRUE;
                if (bDataAvailable) {
                    if ((sscanf(argv[++i], "%x", &allArgs->camMap.enable) != 1)) {
                        LOG_ERR("%s: Invalid camera enable: %s\n", __func__, argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                    allArgs->numSensors = EXTIMGDEV_MAP_COUNT_ENABLED_LINKS(allArgs->camMap.enable);
                }
                LOG_INFO("%s: cam_enable %x\n", __func__, allArgs->camMap.enable);
            } else if (!strcasecmp(argv[i], "--cam_mask")) {
                if (bDataAvailable) {
                    if ((sscanf(argv[++i], "%x", &allArgs->camMap.mask) != 1)) {
                        LOG_ERR("%s: Invalid camera mask: %s\n", __func__, argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                }
                LOG_INFO("%s: cam_mask %x\n", __func__, allArgs->camMap.mask);
            } else if (!strcasecmp(argv[i], "--csi_outmap")) {
                if (bDataAvailable) {
                    if ((sscanf(argv[++i], "%x", &allArgs->camMap.csiOut) != 1)) {
                        LOG_ERR("%s: Invalid csi_outmap: %s\n", __func__, argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                }
                LOG_INFO("%s: csi_outmap %x\n", __func__, allArgs->camMap.csiOut);
            } else if (!strcasecmp(argv[i], "-n")) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    allArgs->numFrames.uIntValue = atoi(arg);
                    allArgs->numFrames.isUsed = NVMEDIA_TRUE;
                } else {
                    LOG_ERR("-n must be followed by number of frames to capture\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            } else if(!strcasecmp(argv[i], "--ext_sync")) {
                if(bDataAvailable) {
                    allArgs->enableExtSync = NVMEDIA_TRUE;
                    if((sscanf(argv[++i], "%f", &allArgs->dutyRatio) != 1)) {
                        LOG_ERR("Bad duty ratio: %s\n", argv[i]);
                        return -1;
                    }
                    // check the range. If it's not in the range, set 0.25 by default
                    if((allArgs->dutyRatio <= 0.0) || (allArgs->dutyRatio >= 1.0)) {
                        allArgs->dutyRatio = 0.25;
                    }
                } else {
                    LOG_ERR("--ext_sync must be followed by number of duty ratio\n");
                    return -1;
                }
             } else {
                LOG_ERR("Unsupported option encountered: %s\n", argv[i]);
                return NVMEDIA_STATUS_ERROR;
            }
        }
    }

    if (allArgs->numSensors > NVMEDIA_MAX_AGGREGATE_IMAGES) {
        LOG_WARN("Max aggregate images is: %u\n",
                 NVMEDIA_MAX_AGGREGATE_IMAGES);
        allArgs->numSensors = NVMEDIA_MAX_AGGREGATE_IMAGES;
    }

    // Set the same capture set for all virtual channels
    // TBD: Add unique capture set for each vc once there is hw support
    if (allArgs->useVirtualChannels) {
        allArgs->numVirtualChannels = allArgs->numSensors;
        for (j = 0; j < allArgs->numSensors; j++) {
            allArgs->config[j].isUsed = NVMEDIA_TRUE;
            allArgs->config[j].uIntValue = allArgs->config[0].uIntValue;
        }
    }

    return NVMEDIA_STATUS_OK;
}
