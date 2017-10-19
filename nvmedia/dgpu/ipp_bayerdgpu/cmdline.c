/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "log_utils.h"
#include "misc_utils.h"
#include "nvmedia_idp.h"
#include "config_parser.h"
#include "cmdline.h"

#define MAX_CONFIG_SECTIONS 128

static CaptureConfigParams sCaptureSetsCollection[MAX_CONFIG_SECTIONS];
static NvMediaBool sConfigFileParsingDone = NVMEDIA_FALSE;

SectionMap sectionsMap[] = {
    {SECTION_CAPTURE,     "capture-params-set", 0, sizeof(CaptureConfigParams)},
    {SECTION_NONE,        "",                   0, 0} // Specifies end of array
};

ConfigParamsMap paramsMap[] = {
  //{paramName, mappedLocation, type, defaultValue, paramLimits, minLimit, maxLimit, stringLength, stringLengthAddr, sectionType}
    {"capture-name",       &sCaptureSetsCollection[0].name,           TYPE_CHAR_ARR,       0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"capture-description",&sCaptureSetsCollection[0].description,    TYPE_CHAR_ARR,       0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"board",              &sCaptureSetsCollection[0].board,          TYPE_CHAR_ARR,       0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"input_device",       &sCaptureSetsCollection[0].inputDevice,    TYPE_CHAR_ARR,       0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"input_format",       &sCaptureSetsCollection[0].inputFormat,    TYPE_CHAR_ARR,       0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"surface_format",     &sCaptureSetsCollection[0].surfaceFormat,  TYPE_CHAR_ARR,       0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"resolution",         &sCaptureSetsCollection[0].resolution,     TYPE_CHAR_ARR,       0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"csi_lanes",          &sCaptureSetsCollection[0].csiLanes,       TYPE_UINT,           2, LIMITS_MIN,  0, 0, 0,               0, SECTION_CAPTURE},
    {"interface",          &sCaptureSetsCollection[0].interface,      TYPE_CHAR_ARR,       0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"embedded_lines_top",    &sCaptureSetsCollection[0].embeddedDataLinesTop,    TYPE_UINT,     0,  LIMITS_MIN, 0, 0, 0,         0, SECTION_CAPTURE},
    {"embedded_lines_bottom", &sCaptureSetsCollection[0].embeddedDataLinesBottom, TYPE_UINT,     0,  LIMITS_MIN, 0, 0, 0,         0, SECTION_CAPTURE},
    {"i2c_device",            &sCaptureSetsCollection[0].i2cDevice,        TYPE_UINT,       0,   LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"serializer_address",    &sCaptureSetsCollection[0].brdcstSerAddr,    TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"serializer_address_0",  &sCaptureSetsCollection[0].serAddr[0],       TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"serializer_address_1",  &sCaptureSetsCollection[0].serAddr[1],       TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"serializer_address_2",  &sCaptureSetsCollection[0].serAddr[2],       TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"serializer_address_3",  &sCaptureSetsCollection[0].serAddr[3],       TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"deserializer_address",  &sCaptureSetsCollection[0].desAddr,          TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"sensor_address",        &sCaptureSetsCollection[0].brdcstSensorAddr, TYPE_UINT_HEX,   0x30,LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"sensor_address_0",      &sCaptureSetsCollection[0].sensorAddr[0],    TYPE_UINT_HEX,   0,   LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"sensor_address_1",      &sCaptureSetsCollection[0].sensorAddr[1],    TYPE_UINT_HEX,   0,   LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"sensor_address_2",      &sCaptureSetsCollection[0].sensorAddr[2],    TYPE_UINT_HEX,   0,   LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"sensor_address_3",      &sCaptureSetsCollection[0].sensorAddr[3],    TYPE_UINT_HEX,   0,   LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    //All parameters below are maintained for compatability reasons only and will be obsoleted eventually
    {"max9271_address",       &sCaptureSetsCollection[0].brdcstSerAddr,    TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"max9271_address_0",     &sCaptureSetsCollection[0].serAddr[0],       TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"max9271_address_1",     &sCaptureSetsCollection[0].serAddr[1],       TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"max9271_address_2",     &sCaptureSetsCollection[0].serAddr[2],       TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"max9271_address_3",     &sCaptureSetsCollection[0].serAddr[3],       TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {"max9286_address",       &sCaptureSetsCollection[0].desAddr,          TYPE_UINT_HEX,   0x0, LIMITS_NONE,    0, 0, 0,         0, SECTION_CAPTURE},
    {NULL} // Specifies the end of the array
};

static NvMediaStatus
ParseConfigFile(TestArgs *args)
{
    NvU32 id = 0;

    if (!sConfigFileParsingDone) {
        memset(&sCaptureSetsCollection[0],
                0,
                sizeof(CaptureConfigParams) * MAX_CONFIG_SECTIONS);

        if (IsFailed(ConfigParser_ParseFile(paramsMap,
                                            MAX_CONFIG_SECTIONS,
                                            sectionsMap,
                                            &args->configFile[0]))) {
            LOG_ERR("ParseConfigFile: ConfigParser_ParseFile failed\n");
            return NVMEDIA_STATUS_ERROR;
        }

        ConfigParser_GetSectionIndexByType(sectionsMap, SECTION_CAPTURE, &id);
        args->numConfigs = sectionsMap[id].lastSectionIndex + 1;
        args->captureConfigs = &sCaptureSetsCollection[0];
        sConfigFileParsingDone = NVMEDIA_TRUE;
    }

    return NVMEDIA_STATUS_OK;
}

static int
GetParamSetID(void *configSetsList,
              SectionType type,
              int paramSets,
              char* paramSetName)
{
    int i;
    char *name = NULL;

    for (i = 0; i < paramSets; i++) {
        if (type == SECTION_CAPTURE)
            name = ((CaptureConfigParams *)configSetsList)[i].name;
        else
            return -1;

        if (!strcasecmp(name, paramSetName)) {
            LOG_DBG("%s: Param set found (%d)\n", __func__, i);
            return i;
        }
    }

    return -1;
}

static void
PrintCaptureParamSets(TestArgs *args)
{
    NvU32 i;
    LOG_MSG("Capture parameter sets (%d):\n", args->numConfigs);
    for (i = 0; i < args->numConfigs; i++) {
        LOG_MSG("\n%s: ", args->captureConfigs[i].name);
        LOG_MSG("%s\n", args->captureConfigs[i].description);
    }
    LOG_MSG("\n");
}

int
ParseArgs(int argc, char *argv[], TestArgs *args)
{
    NvMediaBool lastArg = NVMEDIA_FALSE;
    NvMediaBool dataAvailable = NVMEDIA_FALSE;
    int i;

    // Default parameters
    strcpy(args->configFile, "configs/default.conf");
    args->imagesNum = 1;
    args->usevc = NVMEDIA_FALSE;
    args->saveEnabled = NVMEDIA_FALSE;
    args->checkcrc = NVMEDIA_FALSE;

    if(argc >= 2) {
        for (i = 0; i < argc; i++) {
            lastArg = ((argc - i) == 1);
            dataAvailable = (!lastArg) && !(argv[i + 1][0] == '-');

            if (!strcasecmp(argv[i], "-h")) {
                PrintUsage();
                return NVMEDIA_STATUS_ERROR;
            }

            else if (!strcasecmp(argv[i], "-cf")) {
                if (argv[i + 1] && argv[i + 1][0]  != '-') {
                    strncpy(args->configFile, argv[++i], MAX_STRING_SIZE);
                }
                else {
                    LOG_ERR("-cf must be followed by configuration file name\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            }

            else if (!strcasecmp(argv[i], "-v")) {
                args->logLevel = LEVEL_DBG;
                if (dataAvailable) {
                    if ((sscanf(argv[++i], "%u", &args->logLevel) != 1)) {
                        LOG_ERR("Bad logging level: %s\n", argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                    if (args->logLevel < LEVEL_ERR || args->logLevel > LEVEL_DBG) {
                        printf("Bad logging level: %d. Using default logging level 0\n", args->logLevel);
                        args->logLevel = LEVEL_ERR;
                    }
                }
                SetLogLevel(args->logLevel);
            }
        }
    }

    if (IsFailed(ParseConfigFile(args))) {
        LOG_ERR("Failed to parse config file %s\n", args->configFile);
        return NVMEDIA_STATUS_ERROR;
    }

    ConfigParser_ValidateParams(paramsMap, sectionsMap);

    if (argc >= 2) {
        for (i = 1; i < argc; i++) {
            lastArg = ((argc - i) == 1);
            dataAvailable = (!lastArg) && (argv[i + 1][0] != '-');

            if (!strcasecmp(&argv[i][1], "-h")) {
                PrintUsage();
                return NVMEDIA_STATUS_ERROR;
            }

            else if (!strcasecmp(argv[i], "-v")) {
                if (dataAvailable) {
                    ++i;
                }
            }

            else if (!strcasecmp(argv[i], "-cf")) {
                ++i;
            }

            else if (!strcasecmp(argv[i], "-c")) {
                if (dataAvailable) {
                    ++i;
                    int paramSetId = 0;
                    paramSetId = GetParamSetID(args->captureConfigs,
                                               SECTION_CAPTURE,
                                               args->numConfigs,
                                               argv[i]);
                    if (paramSetId == -1) {
                        paramSetId = 0;
                        LOG_ERR("Params set name '%s' wasn't found\n", argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                    args->configId = paramSetId;
                    LOG_INFO("Using params set: %s for capture\n",
                             args->captureConfigs[paramSetId].name);
                }
                else {
                    LOG_ERR("-c must be followed by capture parameters set name\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            }


            else if (!strcasecmp(argv[i], "-lc")) {
                PrintCaptureParamSets(args);
                return 1;
            }

            else if (!strcasecmp(argv[i], "-d")) {
                if (dataAvailable) {
                    if ((sscanf(argv[++i], "%u", &args->displayId) != 1)) {
                        LOG_ERR("Bad display ID: %s\n", argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                    args->displayEnabled = NVMEDIA_TRUE;
                }
                else {
                    LOG_ERR("-d must be followed by display ID\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            }

            else if (!strcasecmp(argv[i], "-f")) {
                if (dataAvailable) {
                    args->saveEnabled = NVMEDIA_TRUE;
                    strncpy(args->filename, argv[++i], MAX_STRING_SIZE - 1);
                    args->filename[MAX_STRING_SIZE - 1] = '\0';
                }
                else {
                    LOG_ERR("-f must be followed by file name\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            }

            else if (!strcasecmp(argv[i], "-w")) {
                if (dataAvailable) {
                    if ((sscanf(argv[++i], "%u", &args->windowId) != 1)) {
                        LOG_ERR("Bad window ID: %s\n", argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                }
                else {
                    LOG_ERR("-w must be followed by window ID\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                if (args->windowId < 0 || args->windowId > 2) {
                    printf("Bad window ID: %d. Using default window ID 0\n",
                            args->windowId);
                    args->windowId = 0;
                }
            }

            else if (!strcasecmp(argv[i], "-z")) {
                if (dataAvailable) {
                    if ((sscanf(argv[++i], "%u", &args->depth) != 1)) {
                        LOG_ERR("Bad depth value: %s\n", argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                }
                else {
                    LOG_ERR("-z must be followed by depth value\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                if (args->depth < 0 || args->depth > 255) {
                    printf("Bad depth value: %d. Using default value: 1\n",
                             args->depth);
                    args->depth = 1;
                }
            }

            else if (!strcasecmp(argv[i], "--show-timestamp")) {
                args->showTimeStamp = NVMEDIA_TRUE;
            }
            else if (!strcasecmp(argv[i], "--vc_enable")) {
                args->usevc = NVMEDIA_TRUE;
            }
            else if (!strcasecmp(argv[i], "--checkcrc")) {
                args->checkcrc = NVMEDIA_TRUE;
            }
            else if (!strcasecmp(argv[i], "--aggregate")) {
                if (dataAvailable) {
                    if ((sscanf(argv[++i], "%u", &args->imagesNum) != 1)) {
                        LOG_ERR("Bad siblings number: %s\n", argv[i]);
                        return NVMEDIA_STATUS_ERROR;
                    }
                }
                else {
                    LOG_ERR("--aggregate must be followed by number of images to aggregate\n");
                    return NVMEDIA_STATUS_ERROR;
                }
                if (args->imagesNum < 1 || args->imagesNum > 4) {
                    printf("Bad aggregate images number: %d. Using default aggregate images number 1\n", args->imagesNum);
                    args->imagesNum = 1;
                }
            }
            else if(!strcasecmp(argv[i], "--ext_sync")) {
                if(dataAvailable) {
                    args->enableExtSync = NVMEDIA_TRUE;
                    if((sscanf(argv[++i], "%f", &args->dutyRatio) != 1)) {
                        LOG_ERR("Bad duty ratio: %s\n", argv[i]);
                        return -1;
                    }
                    // check the range. If it's not in the range, set 0.25 by default
                    if((args->dutyRatio <= 0.0) || (args->dutyRatio >= 1.0)) {
                        args->dutyRatio = 0.25;
                    }
                } else {
                    LOG_ERR("--ext_sync must be followed by number of duty ratio\n");
                    return -1;
                }
            }
            else {
                LOG_ERR("Unsupported option encountered: %s\n", argv[i]);
                return NVMEDIA_STATUS_ERROR;
            }
        }
    }
    if(args->checkcrc) {
        if( !(args->usevc && args->saveEnabled) && !(args->imagesNum == 1 && args->saveEnabled)) {
            args->checkcrc = NVMEDIA_FALSE;
            LOG_MSG("WARNING: Check crc works with -f enabled and for mutiple camera only with --vc_enable\n");
       }
    }
    if(!args->saveEnabled) {
        LOG_MSG("Output file name is not passed. Skipping File write..\n");
        LOG_MSG("To dump output pass -f <FILE NAME> \n");
    }
    else {
        LOG_MSG("The file save option has been selected.\n");
        LOG_MSG("Please note that this option is not guaranteed to save frames reliably without frame drops.\n");
    }
    return NVMEDIA_STATUS_OK;
}

void PrintUsage(void)
{
    LOG_MSG("Usage: nvmipp_bayerdgpu -cf [file] -c [parametersetname] [n]\n");
    LOG_MSG("\nAvailable command line options:\n");
    LOG_MSG("-h                 Print usage\n");
    LOG_MSG("-v [level] Logging level. Default = 0\n");
    LOG_MSG("                   0: Errors, 1: Warnings, 2: Info, 3: Debug\n");
    LOG_MSG("-cf [file]         Set configuration file.\n");
    LOG_MSG("                   Default: configs/default.conf\n");
    LOG_MSG("-c [name]          Parameters set name to be used for capture configuration.\n");
    LOG_MSG("-lc                List available configuration parameter sets\n");
    LOG_MSG("--vc_enable        Use Virtual Channels\n");
    LOG_MSG("--checkcrc         Images are compared by matching CRC at producer and consumer(Works only with --vc_enable)\n");
    LOG_MSG("--show-timestamp   Show timestamp information. Needs -v option.\n");
    LOG_MSG("--aggregate [n]    Treat captured frames as aggregated image with <n> siblings.\n");
    LOG_MSG("                   Default: n = 1\n");
    LOG_MSG("--ext_sync [n]     Enable the external synchronization with <n> duty ratio; 0.0 < n < 1.0\n");
    LOG_MSG("                   If n is out of the range, set 0.25 to the duty ratio by default\n");
    LOG_MSG("                   If this option is not set, the synchronization will be handled by the aggregator\n");
    LOG_MSG("-f [file]          Output file name. [file] is appended with _cam<idx> \n");
    LOG_MSG("                   If -f option is not passed, file write is skipped \n");
}

void PrintRuntimeUsage(void)
{
    LOG_MSG("Usage: Run time commands\n");
    LOG_MSG("\nAvailable commands: \n");
    LOG_MSG("-h         Print command usage\n");
    LOG_MSG("-q(uit)    Quit the application\n");
    LOG_MSG("-c(ycle)   Cycle camera on display\n");
}
