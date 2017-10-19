/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmdline.h"
#include "config_parser.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvmedia_image.h"

static int    s_section_start = 0;
static int    s_section_size  = 0;
static char **s_section_ptr   = NULL;

static CaptureConfigParams sCaptureSetsCollection[MAX_CONFIG_SECTIONS];

static NvMediaBool sConfigFileParsingDone = NVMEDIA_FALSE;

SectionMap sectionsMap[] = {
    {SECTION_CAPTURE,     "capture-params-set", 0, sizeof(CaptureConfigParams)},
    {SECTION_NONE,        "",                   0, 0} // Specifies end of array
};

ConfigParamsMap image2DProcessingParamsMap[] = {
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
static int
TokenizeLine (
    char *input,
    char **inputTokens,
    int *tokensNum)
{
    int inputLength = strlen(input);
    int i = 0, k;
    char strTemp[255];
    NvMediaBool quotedFlag;

    *tokensNum = 0;
    for(i = 0; i < inputLength; i++) {
        while(i < inputLength && isspace(input[i])) {
            i++;
        }
        quotedFlag = NVMEDIA_FALSE;
        if(i < inputLength && input[i] == '"') {
            quotedFlag = NVMEDIA_TRUE;
            i++;
        }
        k = 0;
        if(quotedFlag) {
            while(i < inputLength && input[i] != '"') {
                strTemp[k++] = input[i++];
            }
            if(i < inputLength && input[i] == '"') {
                i++;
            }
            strTemp[k] = '\0';
        } else {
            while(i < inputLength && !isspace(input[i])) {
                strTemp[k++] = input[i++];
            }
            strTemp[k]='\0';
        }
        if(k) {
            inputTokens[*tokensNum] = (char *) malloc(k + 1);
            strcpy(inputTokens[(*tokensNum)++], strTemp);
        }
    }

    return 0;
}

static int
ReadLine (
    FILE *file,
    char input[],
    NvMediaBool isValidCommand)
{
    if(!file) {
        fgets(input, MAX_STRING_SIZE, stdin);
    } else {
        fgets(input, MAX_STRING_SIZE, file);
        //fgets doesn't omit the newline character
        if(input[strlen(input) - 1] == '\n')
            input[strlen(input) - 1] = '\0';
        if(isValidCommand)
            LOG_MSG("%s\n",input);
    }

    return 0;
}

static int
IsSectionEnabled (
    char **tokens,
    NvMediaBool *isSectionEnabled)
{
    char section[MAX_STRING_SIZE] = {0};
    int  i = 0;

    if(!s_section_ptr)
        *isSectionEnabled = NVMEDIA_FALSE;

    /* check whether section is enabled in input string */
    for(i = s_section_start; i < s_section_start + s_section_size; i++) {
        LOG_MSG(section, "[%s]", s_section_ptr[i]);
        if(strstr(tokens[0], section)){
            *isSectionEnabled = NVMEDIA_TRUE;
        }
    }
    *isSectionEnabled = NVMEDIA_FALSE;

    return 0;
}

int
ParseNextCommand (
    FILE *file,
    char *inputLine,
    char **inputTokens,
    int *tokensNum)
{
    NvMediaBool section_done_flag;
    NvMediaBool is_section_enabled;

    ReadLine(file, inputLine, NVMEDIA_TRUE);
    TokenizeLine(inputLine, inputTokens, tokensNum);

    /* start processing sections, if there are any */
    if(s_section_size && *tokensNum) {
        /* get start of the section */
        section_done_flag = NVMEDIA_FALSE;
        if((inputTokens[0][0] == '[') &&
            (strcasecmp(inputTokens[0], "[/s]"))) {
            IsSectionEnabled(inputTokens, &is_section_enabled);
            if(is_section_enabled) {
                while(!section_done_flag) {
                    ReadLine(file, inputLine, NVMEDIA_FALSE);
                    TokenizeLine(inputLine,
                                           inputTokens,
                                           tokensNum);
                    if(*tokensNum) {
                        if(!strncmp(inputTokens[0], "[/s]", 4)) {
                            section_done_flag = NVMEDIA_TRUE;
                        }
                    }
                }
            } else {
                ReadLine(file, inputLine, NVMEDIA_TRUE);
                TokenizeLine(inputLine, inputTokens, tokensNum);
            }
        }
    }

    return 0;
}

static NvMediaStatus
ParseConfigFile(char *configFile,
                NvU32 *captureConfigSetsNum,
                CaptureConfigParams **captureConfigCollection)
{
    NvU32 id = 0;

    if(!sConfigFileParsingDone) {
        memset(&sCaptureSetsCollection[0],
               0,
               sizeof(CaptureConfigParams) * MAX_CONFIG_SECTIONS);

        ConfigParser_InitParamsMap(image2DProcessingParamsMap);
        if(IsFailed(ConfigParser_ParseFile(image2DProcessingParamsMap,
                                           MAX_CONFIG_SECTIONS,
                                           sectionsMap,
                                           configFile))) {
            LOG_ERR("ParseConfigFile: ConfigParser_ParseFile failed\n");
            return NVMEDIA_STATUS_ERROR;
        }

        ConfigParser_GetSectionIndexByType(sectionsMap, SECTION_CAPTURE, &id);
        *captureConfigSetsNum = sectionsMap[id].lastSectionIndex + 1;
        *captureConfigCollection = sCaptureSetsCollection;
        sConfigFileParsingDone = NVMEDIA_TRUE;
    }
    return NVMEDIA_STATUS_OK;
}

static int
GetParamSetID(
    void *configSetsList,
    SectionType type,
    int paramSets,
    char* paramSetName)
{
    int i;
    char *name = NULL;

    for(i = 0; i < paramSets; i++) {
        if (type == SECTION_CAPTURE)
            name = ((CaptureConfigParams *)configSetsList)[i].name;
        else
            return -1;

        if(!strcasecmp(name, paramSetName)) {
            LOG_DBG("%s: Param set found (%d)\n", __func__, i);
            return i;
        }
    }

    return -1;
}

static void
PrintCaptureParamSets (
    TestArgs *args)
{
    NvU32 j;

    LOG_MSG("Capture parameter sets (%d):\n", args->captureConfigSetsNum);
    for(j = 0; j < args->captureConfigSetsNum; j++) {
        LOG_MSG("\n%s: ", args->captureConfigCollection[j].name);
        LOG_MSG("%s\n", args->captureConfigCollection[j].description);
    }
    LOG_MSG("\n");
}

int
ParseArgs (
    int argc,
    char *argv[],
    TestArgs *args)
{
    int i = 0;
    NvMediaBool bLastArg = NVMEDIA_FALSE;
    NvMediaBool bDataAvailable = NVMEDIA_FALSE;
    NvMediaBool displayDeviceEnabled;
    NvMediaStatus status;

    // Default params
    args->imagesNum = 1;
    args->outputWidth = 0;
    args->outputHeight = 0;
    args->outputSurfType = NvMediaSurfaceType_Image_YUV_420;
    args->ispSelect = NVMEDIA_ISP_SELECT_ISP_A;
    args->ispOutType = NvMediaSurfaceType_Image_YUV_420;
    args->fifoMode = NVMEDIA_TRUE;
    args->camMap.enable = CAM_ENABLE_DEFAULT;
    args->camMap.mask = CAM_MASK_DEFAULT;
    args->camMap.csiOut = CSI_OUT_DEFAULT;

    // Default config file
    strcpy(args->configFile, "configs/default.conf");

    // First look for help request, logging level and configuration file name.
    if(argc >= 2) {
        for(i = 1; i < argc; i++) {
            // check if this is the last argument
            bLastArg = ((argc - i) == 1);

            // check if there is data available to be parsed
            bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

            if(!strcasecmp(argv[i], "-h")) {
                PrintUsage();
                return 1;
            } else if(!strcasecmp(argv[i], "-cf")) {
                if(argv[i + 1] && argv[i + 1][0] != '-') {
                    strncpy(args->configFile, argv[++i], MAX_STRING_SIZE);
                } else {
                    LOG_ERR("-cf must be followed by configuration file name\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "-v")) {
                args->logLevel = LEVEL_DBG;
                if(bDataAvailable) {
                    args->logLevel = atoi(argv[++i]);
                    if(args->logLevel < LEVEL_ERR ||
                       args->logLevel > LEVEL_DBG) {
                        printf("Invalid logging level chosen (%d)\n",
                               args->logLevel);
                        printf("Setting logging level to LEVEL_ERR (0)\n");
                        args->logLevel = LEVEL_ERR;
                    }
                }
                SetLogLevel(args->logLevel);
            }
        }
    }

    // Parse config file here
    if(!IsSucceed(ParseConfigFile(args->configFile,
                                &args->captureConfigSetsNum,
                                &args->captureConfigCollection))) {
        LOG_ERR("Failed to parse config file %s\n",
                args->configFile);
        return NVMEDIA_STATUS_ERROR;
    }

    ConfigParser_ValidateParams(image2DProcessingParamsMap, sectionsMap);

    // The rest
    if(argc >= 2) {
        for(i = 1; i < argc; i++) {
            // check if this is the last argument
            bLastArg = ((argc - i) == 1);

            // check if there is data available to be parsed
            bDataAvailable = (!bLastArg) && (argv[i+1][0] != '-');

            if(strcasecmp(&argv[i][1], "h") == 0) {
                PrintUsage();
                return 1;
            }  else if(!strcasecmp(argv[i], "-lps")) {
                PrintCaptureParamSets(args);
                return 1;
            } else if(strcasecmp(argv[i], "-v") == 0) {
                if(bDataAvailable)
                    ++i;
            } else if(!strcasecmp(argv[i], "-cf")) {
                ++i; // Was already parsed at the beginning. Skipping.
            } else if(!strcasecmp(argv[i], "-c")) {
                if(bDataAvailable) {
                    ++i;
                    int paramSetId = 0;
                    paramSetId = GetParamSetID(args->captureConfigCollection,
                                               SECTION_CAPTURE,
                                               args->captureConfigSetsNum,
                                               argv[i]);
                    if(paramSetId == -1) {
                        paramSetId = 0; // Use parametersa set 0 as default
                        LOG_ERR("Params set name '%s' wasn't found\n", argv[i]);
                        return -1;
                    }
                    args->configCaptureSetUsed = paramSetId;
                    LOG_INFO("Using params set: %s for capture\n",
                             args->captureConfigCollection[paramSetId].name);
                } else {
                    LOG_ERR("-c must be followed by capture parameters set name\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "--aggregate")) {
                args->useAggregationFlag = NVMEDIA_TRUE;
                if(bDataAvailable) {
                    if((sscanf(argv[++i], "%u", &args->imagesNum) != 1)) {
                        LOG_ERR("Bad siblings number: %s\n", argv[i]);
                        return -1;
                    }
                    args->camMap.enable = EXTIMGDEV_MAP_N_TO_ENABLE(args->imagesNum);
                } else {
                    LOG_ERR("--aggregate must be followed by number of images to aggregate\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "--ext_sync")) {
                if(bDataAvailable) {
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
            } else if (!strcasecmp(argv[i], "-d")) {
                if (bDataAvailable) {
                    if((sscanf(argv[++i], "%u", &args->displayId) != 1)) {
                        LOG_ERR("Err: Bad display id: %s\n", argv[i]);
                        return -1;
                    }
                    status = CheckDisplayDeviceID(args->displayId, &displayDeviceEnabled);
                    if (status != NVMEDIA_STATUS_OK) {
                        LOG_ERR("Err: Chosen display (%d) not available\n", args->displayId);
                        return -1;
                    }
                    LOG_DBG("ParseArgs: Chosen display: (%d) device enabled? %d\n",
                            args->displayId, displayDeviceEnabled);
                } else {
                    LOG_ERR("Err: -d must be followed by display id\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "--mailbox")) {
                args->fifoMode = NVMEDIA_FALSE;
            } else if(!strcasecmp(argv[i], "--show-timestamp")) {
                args->showTimeStamp = NVMEDIA_TRUE;
            } else if(!strcasecmp(argv[i], "--show-metadata")) {
                args->showMetadataFlag = NVMEDIA_TRUE;
            } else if(!strcasecmp(argv[i], "--slave")) {
                args->slaveTegra = NVMEDIA_TRUE;
            } else if(!strcasecmp(argv[i], "--cam_enable")) {
                args->useAggregationFlag = NVMEDIA_TRUE;
                if(bDataAvailable) {
                    if((sscanf(argv[++i], "%x", &args->camMap.enable) != 1)) {
                        LOG_ERR("%s: Invalid camera enable: %s\n", __func__, argv[i]);
                        return -1;
                    }
                    args->imagesNum = EXTIMGDEV_MAP_COUNT_ENABLED_LINKS(args->camMap.enable);
                }
                LOG_INFO("%s: cam_enable %x\n", __func__, args->camMap.enable);
            } else if(!strcasecmp(argv[i], "--cam_mask")) {
                if(bDataAvailable) {
                    if((sscanf(argv[++i], "%x", &args->camMap.mask) != 1)) {
                        LOG_ERR("%s: Invalid camera mask: %s\n", __func__, argv[i]);
                        return -1;
                    }
                    LOG_INFO("%s: cam_mask %x\n", __func__, args->camMap.mask);
                }
            } else if(!strcasecmp(argv[i], "--csi_outmap")) {
                if(bDataAvailable) {
                    if((sscanf(argv[++i], "%x", &args->camMap.csiOut) != 1)) {
                        LOG_ERR("%s: Invalid csi_outmap: %s\n", __func__, argv[i]);
                        return -1;
                    }
                }
                LOG_INFO("%s: csi_outmap %x\n", __func__, args->camMap.csiOut);
            } else {
                LOG_ERR("Unsupported option encountered: %s\n", argv[i]);
                return -1;
            }
        }
    }

    return 0;
}

void
PrintUsage ()
{
    NvMediaIDPDeviceParams outputs[MAX_OUTPUT_DEVICES];
    char displayName[MAX_STRING_SIZE];
    int outputDevicesNum = 0;
    int i;

    NvMediaIDPQuery(&outputDevicesNum, outputs);

    LOG_MSG("Usage: nvmipp_yuv -cf [file]  -c [name] -d [n] --aggregate [n]\n");
    LOG_MSG("\nAvailable command line options:\n");
    LOG_MSG("-h                Print usage\n");
    LOG_MSG("-v [level]        Logging Level. Default = 0\n");
    LOG_MSG("                  0: Errors, 1: Warnings, 2: Info, 3: Debug\n");
    LOG_MSG("                  Default: 0\n");
    LOG_MSG("-d [n]            Set display ID\n");
    LOG_MSG("                  Available display devices: (%d)\n", outputDevicesNum);
    for(i = 0; i < outputDevicesNum; i++) {
        if(IsSucceed(GetImageDisplayName(outputs[i].type, displayName))) {
            LOG_MSG("                       Display ID: %d (%s)\n",
                    outputs[i].displayId, displayName);
        } else {
            LOG_MSG("                       Error Getting Display Name for ID (%d)\n",
                    outputs[i].displayId);
        }
    }
    LOG_MSG("-cf [file]        Set configuration file.\n");
    LOG_MSG("                  Default: NVMEDIA_CAPTURE2D_CONFIG environment variable, if set\n");
    LOG_MSG("                  Otherwise: configs/default.conf\n");
    LOG_MSG("-c [name]        Parameters set name to be used for capture configuration.\n");
    LOG_MSG("                  Default: First config set (capture-params-set)\n");
    LOG_MSG("-lps              List available configuration parameter sets\n");
    LOG_MSG("--mailbox          Use mailbox mode. (Default = fifo)\n");
    LOG_MSG("--aggregate [n]   Treat captured frames as aggregated image with <n> siblings\n");
    LOG_MSG("                  Default: n = 1\n");
    LOG_MSG("--ext_sync [n]    Enable the external synchronization with <n> duty ratio; 0.0 < n < 1.0\n");
    LOG_MSG("                  If n is out of the range, set 0.25 to the duty ratio by default\n");
    LOG_MSG("                  If this option is not set, the synchronization will be handled by the aggregator\n");
    LOG_MSG("--show-timestamp  Show timestamp information. Needs -v option.\n");
    LOG_MSG("--show-metadata   Show metadata information. Needs -v option.\n");
    LOG_MSG("--slave           Application is being run on a slave Tegra\n");
    LOG_MSG("--cam_enable [n]   Enable or disable camera[3210]; enable:1, disable:0\n");
    LOG_MSG("                   Default: n = 0001\n");
    LOG_MSG("--cam_mask [n]     Mask or unmask camera[3210]; 0,1,2 or 3\n");
    LOG_MSG("                   Default: n = 0000\n");
    LOG_MSG("--csi_outmap [n]   Set csi out order for camera[3210]; 0,1,2 or 3\n");
    LOG_MSG("                   Default: n = 3210\n");
}
