/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
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

#define MAX_CONFIG_SECTIONS             128

static int    s_section_start = 0;
static int    s_section_size  = 0;
static char **s_section_ptr   = NULL;

static CaptureConfigParams sCaptureSetsCollection[MAX_CONFIG_SECTIONS];

static NvMediaBool sConfigFileParsingDone = NVMEDIA_FALSE;

SectionMap sectionsMap[] = {
    {SECTION_CAPTURE,     "capture-params-set", 0, sizeof(CaptureConfigParams)},
    {SECTION_NONE,            "",                     0, 0} // Has to be the last item - specifies end of array
};

ConfigParamsMap applicationParamsMap[] = {
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
ParseConfigFile(TestArgs *args)
{
    NvU32               id = 0;


    if(!sConfigFileParsingDone) {
        memset(&sCaptureSetsCollection[0],
               0,
               sizeof(CaptureConfigParams) * MAX_CONFIG_SECTIONS);

        ConfigParser_InitParamsMap(applicationParamsMap);

        if(IsFailed(ConfigParser_ParseFile(applicationParamsMap,
                                           MAX_CONFIG_SECTIONS,
                                           sectionsMap,
                                           args->configFile))) {
            LOG_ERR("ParseConfigFile: ConfigParser_ParseFile failed\n");
            return NVMEDIA_STATUS_ERROR;
        }

        ConfigParser_GetSectionIndexByType(sectionsMap, SECTION_CAPTURE, &id);
        args->captureConfigSetsNum = sectionsMap[id].lastSectionIndex + 1;
        args->captureConfigCollection = sCaptureSetsCollection;

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
    NvMediaBool                 bLastArg = NVMEDIA_FALSE;
    NvMediaBool                 bDataAvailable = NVMEDIA_FALSE;
    NvMediaBool                 displayDeviceEnabled;
    NvMediaStatus               status;
    NvU32                       displayId, x, y, w, h;

    // Default params
    args->imagesNum = 1;
    args->camMap.enable = CAM_ENABLE_DEFAULT;
    args->camMap.mask = CAM_MASK_DEFAULT;
    args->camMap.csiOut = CSI_OUT_DEFAULT;
    args->slaveTegra = NVMEDIA_FALSE;
    args->pluginFlag = NVMEDIA_NOACPLUGIN;
    args->displayEnabled = NVMEDIA_FALSE;
    args->windowId = 0;
    args->depth = 0;
    args->displayId = -1;
    args->positionSpecifiedFlag = NVMEDIA_FALSE;

    args->frameRateDen = 1000;
    args->frameRateNum = 30*args->frameRateDen;
    args->enableEncode = NVMEDIA_FALSE;
    args->encodePreset = 0;
    args->skipInitialFramesCount = 0;
    args->cbrEncodedDataRateMbps = 0;
    args->losslessH265Compression = NVMEDIA_FALSE;

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
    if(!IsSucceed(ParseConfigFile(args))) {
        LOG_ERR("Failed to parse config file %s\n",
                args->configFile);
        return NVMEDIA_STATUS_ERROR;
    }

    ConfigParser_ValidateParams(applicationParamsMap, sectionsMap);

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
                        paramSetId = 0; // Use parameters set 0 as default
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
                    if((sscanf(argv[++i], "%u", &displayId) != 1)) {
                        LOG_ERR("Err: Bad display id: %s\n", argv[i]);
                        return -1;
                    }

                    status = CheckDisplayDeviceID(displayId, &displayDeviceEnabled);
                    if (status != NVMEDIA_STATUS_OK) {
                        LOG_ERR("Err: Chosen display (%d) not available\n", args->displayId);
                        return -1;
                    }
                    args->displayId = displayId;
                    LOG_DBG("ParseArgs: Chosen display: (%d) device enabled? %d\n",
                            args->displayId, displayDeviceEnabled);
                    args->displayEnabled = NVMEDIA_TRUE;
                } else {
                    LOG_ERR("Err: -d must be followed by display id\n");
                    return -1;
                }
            } else if (!strcasecmp(argv[i], "-w")) {
                if (bDataAvailable) {
                    args->windowId = atoi(argv[++i]);
                } else {
                    LOG_ERR("-w must be followed by window id\n");
                    return -1;
                }
                if (args->windowId > 2) {
                    LOG_ERR("Bad window ID: %d\n",
                             args->windowId);
                    return -1;
                }
            } else if (!strcasecmp(argv[i], "-z")) {
                if (bDataAvailable) {
                    args->depth = atoi(argv[++i]);
                } else {
                    LOG_ERR("-z must be followed by depth value\n");
                    return -1;
                }
                if (args->depth > 255) {
                    LOG_ERR("Bad depth value: %d\n",
                             args->depth);
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "-o")) {
                if(argv[i + 1] && argv[i + 1][0] != '-') {
                    strncpy(args->encodeOutputFileName, argv[++i], MAX_STRING_SIZE);
                } else {
                    LOG_ERR("-o must be followed by output file name\n");
                    return -1;
                }
            } else if (!strcasecmp(argv[i], "-f")) {
                if (bDataAvailable) {
                    float framerate;
                    if (sscanf(argv[++i], "%f", &framerate)) {
                        args->frameRateDen = 1000;  // work with frame rate upto three decimal places
                        args->frameRateNum = (NvU32)(framerate * args->frameRateDen);  // work with frame rate upto three decimal places
                    } else {
                        LOG_ERR("ParseArgs: Invalid frame rate encountered (%s)\n", argv[i]);
                    }
                } else {
                    LOG_ERR("-f must be followed by valid frame rate e.g. 30.0\n");
                    return -1;
                }
            } else if (!strcasecmp(argv[i], "-r")) {
                if (bDataAvailable) {
                    args->enableLimitedRGB = atoi(argv[++i]);
                } else {
                    LOG_ERR("-r must be followed by 1 or 0 to enable/disable limited RGB mode.\n");
                    return -1;
                }
            } else if (!strcasecmp(argv[i], "-e")) {
                if (bDataAvailable) {
                    args->videoCodec = atoi(argv[++i]);
                } else {
                    LOG_ERR("-e must be followed by encode type (0 for h264 and 1 for h265)\n");
                    return -1;
                }
                if ((args->videoCodec != 0) && (args->videoCodec != 1)) {
                    LOG_ERR("Bad encode type: %d\n",
                            args->videoCodec);
                    return -1;
                }
                args->enableEncode = NVMEDIA_TRUE;
            } else if (!strcasecmp(argv[i], "-t")) {
                if (bDataAvailable) {
                    args->encodePreset = atoi(argv[++i]);
                } else {
                    LOG_ERR("-t must be followed by preset 0(LOW_LATENCY_CONSTANT_BITRATE), 1(CONSTANT_QUALITY), 2(HIGH_QUALITY_CONSTANT_BITRATE)\n");
                    return -1;
                }
                if (args->encodePreset > 2) {
                    LOG_ERR("Bad encode preset: %d\n",
                            args->encodePreset);
                    return -1;
                }
                args->enableEncode = NVMEDIA_TRUE;
            } else if (!strcasecmp(argv[i], "-p")) {
                if (bDataAvailable) {
                    if ((sscanf(argv[++i], "%u:%u:%u:%u", &x, &y, &w, &h)
                       != 4)) {
                        LOG_ERR("Bad resolution: %s\n", argv[i]);
                        return NVMEDIA_STATUS_BAD_PARAMETER;
                    }
                    args->position.x0 = x;
                    args->position.y0 = y;
                    args->position.x1 = x + w;
                    args->position.y1 = y + h;
                    args->positionSpecifiedFlag = NVMEDIA_TRUE;
                    LOG_INFO("Output position set to: %u:%u:%u:%u\n",
                             x, y, x + w, y + h);
                } else {
                    LOG_ERR("-p must be followed by window position x0:x1:W:H\n");
                    return NVMEDIA_STATUS_ERROR;
                }
            } else if(!strcasecmp(argv[i], "--cam_enable")) {
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
                }
                LOG_INFO("%s: cam_mask %x\n", __func__, args->camMap.mask);
            } else if(!strcasecmp(argv[i], "--csi_outmap")) {
                if(bDataAvailable) {
                    if((sscanf(argv[++i], "%x", &args->camMap.csiOut) != 1)) {
                        LOG_ERR("%s: Invalid csi_outmap: %s\n", __func__, argv[i]);
                        return -1;
                    }
                }
                LOG_INFO("%s: csi_outmap %x\n", __func__, args->camMap.csiOut);
            } else if(!strcasecmp(argv[i], "--no-interactive")) {
                args->disableInteractiveMode = NVMEDIA_TRUE;
            } else if(!strcasecmp(argv[i], "--timeout")) {
                if(bDataAvailable) {
                    char *arg = argv[++i];
                    args->runningTime = atoi(arg);
                    if(args->runningTime <= 0) {
                        LOG_ERR("timeout must be greater than 0 seconds.\n");
                        return -1;
                    }
                    args->timedRun = NVMEDIA_TRUE;
                } else {
                    LOG_ERR("--timeout must be followed by time in seconds\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "--skip_initial_frames")) {
                if(bDataAvailable) {
                    char *arg = argv[++i];
                    args->skipInitialFramesCount = atoi(arg);
                } else {
                    LOG_ERR("--skip_initial_frames must be followed by frame count\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "--cbr")) {
                if(bDataAvailable) {
                    char *arg = argv[++i];
                    args->cbrEncodedDataRateMbps = atoi(arg);
                } else {
                    LOG_ERR("--cbr must be followed by integer data rate in Mbps\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "--qpi")) {
                if(bDataAvailable) {
                    char *arg = argv[++i];
                    args->qpI = atoi(arg);
                } else {
                    LOG_ERR("--qpi must be followed by integer QP value for I frame\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "--qpp")) {
                if(bDataAvailable) {
                    char *arg = argv[++i];
                    args->qpP = atoi(arg);
                } else {
                    LOG_ERR("--qpp must be followed by integer QP value for P frame\n");
                    return -1;
                }
            } else if (!strcasecmp(argv[i], "--lossless")) {
                args->losslessH265Compression = NVMEDIA_TRUE;
            } else if (!strcasecmp(argv[i], "--vc_enable")) {
                args->useVirtualChannels= NVMEDIA_TRUE;
            } else if(!strcasecmp(argv[i], "--plugin")) {
               if(args->pluginFlag == NVMEDIA_NOACPLUGIN)
                   args->pluginFlag = NVMEDIA_SIMPLEACPLUGIN;
               else
                   LOG_MSG("WARN: more than one plugin type is detected, last one is effective\n");
           } else if(!strcasecmp(argv[i], "--nvplugin")) {
               if(args->pluginFlag == NVMEDIA_NOACPLUGIN)
                   args->pluginFlag = NVMEDIA_NVACPLUGIN;
               else
                   LOG_MSG("WARN: more than one plugin type is detected, last one is effective\n");
            } else if(!strcasecmp(argv[i], "--slave")) {
                    args->slaveTegra = NVMEDIA_TRUE;
            } else {
                LOG_ERR("Unsupported option encountered: %s\n", argv[i]);
                return -1;
            }
        }
    }

    if ((NVMEDIA_TRUE == args->losslessH265Compression) && (args->videoCodec != 1)) {
        LOG_WARN("%s: lossless compression is only applicable to H265 encoding.\n",
                __func__);
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

    LOG_MSG("Usage: nvmipp_capture_encode -cf [file] -c [name] -d [n] -o [name] -e [n] --aggregate [n] --[plugintype]\n");
    LOG_MSG("\nAvailable command line options:\n");
    LOG_MSG("-h                         Print usage\n");
    LOG_MSG("-v [level]                 Logging Level. Default = 0\n");
    LOG_MSG("                           0: Errors, 1: Warnings, 2: Info, 3: Debug\n");
    LOG_MSG("                           Default: 0\n");
    LOG_MSG("-d [n]                     Set display ID\n");
    LOG_MSG("                           Available display devices: (%d)\n", outputDevicesNum);
    for(i = 0; i < outputDevicesNum; i++) {
        if(IsSucceed(GetImageDisplayName(outputs[i].type, displayName))) {
            LOG_MSG("                               Display ID: %d (%s)\n",
                    outputs[i].displayId, displayName);
        } else {
            LOG_MSG("                               Error Getting Display Name for ID (%d)\n",
                    outputs[i].displayId);
        }
    }
    LOG_MSG("-w [n]                     Set display window ID [0-2]\n");
    LOG_MSG("-z [n]                     Set display window depth [0-255]\n");
    LOG_MSG("-p [position]              Window position. Default: full screen size\n");
    LOG_MSG("-cf [file]                 Set capture configuration file.\n");
    LOG_MSG("                           Default: NVMEDIA_CAPTURE2D_CONFIG environment variable, if set\n");
    LOG_MSG("                           Otherwise: configs/default.conf\n");
    LOG_MSG("-c [name]                  Parameters set name to be used for capture configuration.\n");
    LOG_MSG("                           Default: First config set (capture-params-set)\n");
    LOG_MSG("-lps                       List available configuration parameter sets\n");
    LOG_MSG("-e [n]                     Specifies the encoder type. Valid values are 0 (h264) and 1 (h265).\n");
    LOG_MSG("-o [name]                  Specifies the output filename.\n");
    LOG_MSG("-f [n]                     Specifies the frame rate in floating point precision e.g. 30.0. Default is 30.0\n");
    LOG_MSG("-r [n]                     Set this to 1 for limited-RGB (16-235) input\n");
    LOG_MSG("-t [n]                     Encoding preset 0(LOW_LATENCY_CONSTANT_BITRATE), 1(CONSTANT_QUALITY)\n");
    LOG_MSG("                           and 2(MAIN_QUALITY_CONSTANT_BITRATE)\n");
    LOG_MSG("--aggregate [n]            Treat captured frames as aggregated image with <n> siblings\n");
    LOG_MSG("                           Default: n = 1\n");
    LOG_MSG("--ext_sync [n]             Enable the external synchronization with <n> duty ratio; 0.0 < n < 1.0\n");
    LOG_MSG("                           If n is out of the range, set 0.25 to the duty ratio by default\n");
    LOG_MSG("                           If this option is not set, the synchronization will be handled by the aggregator\n");
    LOG_MSG("--plugin                   Use IPP plugins for control algorithm components\n");
    LOG_MSG("--nvplugin                 Use Nvmedia IPP plugins for control algorithm components\n");
    LOG_MSG("--cam_enable [n]           Enable or disable camera[3210]; enable:1, disable:0\n");
    LOG_MSG("                           Default: n = 0001\n");
    LOG_MSG("--cam_mask [n]             Mask or unmask camera[3210]; mask:1, unmask:0\n");
    LOG_MSG("                           Default: n = 0000\n");
    LOG_MSG("--csi_outmap [n]           Set csi out order for camera[3210]; 0,1,2 or 3\n");
    LOG_MSG("                           Default: n = 3210\n");
    LOG_MSG("--vc_enable                Enable virtual channels for capturing the frames\n");
    LOG_MSG("--slave                    Application is being run on a slave Tegra\n");
    LOG_MSG("--skip_initial_frames [n]  Specify the number of initial frames to skip from encoding. Useful for ignoring\n");
    LOG_MSG("                           AE/AWB convergence period. Default is 0\n");
    LOG_MSG("--cbr [n]                  Specify to override the default average bit rate for LOW_LATENCY_CONSTANT_BITRATE\n");
    LOG_MSG("                           and MAIN_QUALITY_CONSTANT_BITRATE presets. e.g. --cbr 8 will override the average\n");
    LOG_MSG("                           bit rate to 8 Mbps.\n");
    LOG_MSG("--qpi [n]                  Specify to override the default QP value of I frame for CONSTANT_QUALITY preset.\n");
    LOG_MSG("--qpp [n]                  Specify to override the default QP value of P frame for CONSTANT_QUALITY preset.\n");
    LOG_MSG("--lossless [n]             Specify to use lossless compression. Applicable only for H265 encoding.\n");
}
