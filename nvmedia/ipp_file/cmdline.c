/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>

#include "cmdline.h"
#include "config_parser.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvmedia_image.h"

static int    s_section_start = 0;
static int    s_section_size  = 0;
static char **s_section_ptr   = NULL;

#define MAX_CONFIG_SECTIONS             128
static CaptureConfigParams sCaptureSetsCollection[MAX_CONFIG_SECTIONS];

static NvMediaBool sConfigFileParsingDone = NVMEDIA_FALSE;

SectionMap sectionsMap[] = {
    {SECTION_CAPTURE,     "capture-params-set", 0, sizeof(CaptureConfigParams)},
    {SECTION_NONE,        "",                   0, 0} // Specifies end of array
};

ConfigParamsMap ippFileProcessingParamsMap[] = {
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
    {"embedded_lines_top",    &sCaptureSetsCollection[0].embeddedDataLinesTop, TYPE_UINT,     0, LIMITS_MIN,  0, 0, 0,            0, SECTION_CAPTURE},
    {"embedded_lines_bottom", &sCaptureSetsCollection[0].embeddedDataLinesBottom, TYPE_UINT,  0, LIMITS_MIN,  0, 0, 0,            0, SECTION_CAPTURE},
    {"i2c_device",         &sCaptureSetsCollection[0].i2cDevice,        TYPE_INT,         -1, LIMITS_NONE, 0, 0, 0,               0, SECTION_CAPTURE},
    {"max9271_address",    &sCaptureSetsCollection[0].max9271_address,  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,               0, SECTION_CAPTURE},
    {"max9286_address",    &sCaptureSetsCollection[0].max9286_address,  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,               0, SECTION_CAPTURE},
    {"sensor_address",     &sCaptureSetsCollection[0].sensor_address,   TYPE_UINT_HEX,  0x30, LIMITS_NONE, 0, 0, 0,               0, SECTION_CAPTURE},
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

static int
ParseConfigFile(TestArgs *args)
{
    NvU32 id = 0;

    if(!sConfigFileParsingDone) {
        memset(&sCaptureSetsCollection[0],
               0,
               sizeof(CaptureConfigParams) * MAX_CONFIG_SECTIONS);

        if(IsFailed(ConfigParser_ParseFile(ippFileProcessingParamsMap,
                                           MAX_CONFIG_SECTIONS,
                                           sectionsMap,
                                           &args->configFile[0]))) {
            LOG_ERR("ParseConfigFile: ConfigParser_ParseFile failed\n");
            return -1;
        }

        ConfigParser_GetSectionIndexByType(sectionsMap, SECTION_CAPTURE, &id);
        args->captureConfigSetsNum = sectionsMap[id].lastSectionIndex + 1;
        args->captureConfigCollection = &sCaptureSetsCollection[0];
        sConfigFileParsingDone = NVMEDIA_TRUE;
    }

    return 0;
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
    char *configFileName = NULL;
    NvMediaBool bLastArg = NVMEDIA_FALSE;
    NvMediaBool bDataAvailable = NVMEDIA_FALSE;

    // Default params
    args->imagesNum = 1;
    args->ispSelect = NVMEDIA_ISP_SELECT_ISP_A;
    args->ispOutType = NvMediaSurfaceType_Image_YUV_420;
    args->pluginFlag = NVMEDIA_NOACPLUGIN;
    args->disableInteractiveMode = NVMEDIA_TRUE;
    args->useAggregationFlag = NVMEDIA_TRUE;
    args->inputPixelOrder = NVMEDIA_RAW_PIXEL_ORDER_BGGR;

    // Default config file
    configFileName = getenv("NVMEDIA_CAPTURE2D_CONFIG");
    if(configFileName)
        strncpy(args->configFile, configFileName, MAX_STRING_SIZE);
    else
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

    // Parse config file
    if(ParseConfigFile(args)) {
        LOG_ERR("Failed to parse config file %s\n", args->configFile);
        return -1;
    }
    ConfigParser_ValidateParams(ippFileProcessingParamsMap, sectionsMap);

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
            } else if(!strcasecmp(argv[i], "-i")) {
                if(bDataAvailable) {
                    args->inputFileName = argv[++i];
                } else {
                    LOG_ERR("-i must be followed by file name\n");
                    return -1;
                }
            } else if(!strcasecmp(argv[i], "-o")) {
                if(bDataAvailable) {
                    args->outputFilePrefix = argv[++i];
                } else {
                    LOG_ERR("-o must be followed by a prefix\n");
                    return -1;
                }
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
                } else {
                    args->imagesNum = 4; // default
                }
            } else if(!strcasecmp(argv[i], "--isp-mv")) {
                args->ispMvFlag = NVMEDIA_TRUE;
                if(bDataAvailable) {
                    i++;
                    if(!strcasecmp(argv[i], "yuv420")) {
                        args->ispMvSurfaceType = NvMediaSurfaceType_Image_YUV_420;
                    } else if(!strcasecmp(argv[i], "y10")) {
                        args->ispMvSurfaceType = NvMediaSurfaceType_Image_Y10;
                    } else {
                        LOG_ERR("Unsupported surface type %s for ISP machine vision\n", argv[i]);
                        return -1;
                    }
                } else {
                    LOG_ERR("--isp-mv must be followed by surface type\n");
                    return -1;
                }
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
            } else if(!strcasecmp(argv[i], "--interactive")) {
                args->disableInteractiveMode = NVMEDIA_FALSE;
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
            } else if(!strcasecmp(argv[i], "--save-ispmv")) {
                if(bDataAvailable) {
                    ++i;
                    args->saveIspMvFlag = NVMEDIA_TRUE;
                    args->saveIspMvPrefix = argv[i];
                }
            } else if(!strcasecmp(argv[i], "--save-metadata")) {
                if(bDataAvailable) {
                    ++i;
                    args->saveMetadataFlag = NVMEDIA_TRUE;
                    args->saveMetadataPrefix = argv[i];
                }
            } else if(!strcasecmp(argv[i], "--bayer-format")) {
                if(bDataAvailable) {
                    ++i;
                    if(!strcasecmp(argv[i], "bggr"))
                        args->inputPixelOrder = NVMEDIA_RAW_PIXEL_ORDER_BGGR;
                    else if(!strcasecmp(argv[i], "rggb"))
                        args->inputPixelOrder = NVMEDIA_RAW_PIXEL_ORDER_RGGB;
                    else if(!strcasecmp(argv[i], "grbg"))
                        args->inputPixelOrder = NVMEDIA_RAW_PIXEL_ORDER_GRBG;
                    else if(!strcasecmp(argv[i], "gbrg"))
                        args->inputPixelOrder = NVMEDIA_RAW_PIXEL_ORDER_GBRG;
                    else {
                        LOG_ERR("Unknown input Bayer type encountered: %s\n",
                                argv[i]);
                        return -1;
                    }
                } else {
                    LOG_ERR("--bayer-format must be followed by Bayer type\n");
                    return -1;
                }
            } else {
                LOG_ERR("Unsupported option encountered: %s\n", argv[i]);
                return -1;
            }
        }
    }

    if(args->timedRun && !args->disableInteractiveMode) {
        LOG_ERR("%s: Timeout is only available when interactive mode is disabled\n", __func__);
    }

    if(!args->inputFileName || !args->outputFilePrefix) {
        LOG_ERR("Input file or output file prefix was not provided.\n");
        PrintUsage ();
        return -1;
    }

    return 0;
}

void
PrintUsage ()
{
    LOG_MSG("Usage: nvmipp_file -i [file] -o [prefix] -cf [file]  -c [name] --aggregate [n]\n");
    LOG_MSG("\nAvailable command line options:\n");
    LOG_MSG("-h                Print usage\n");
    LOG_MSG("-v [level]        Logging Level. Default = 0\n");
    LOG_MSG("                  0: Errors, : Warnings, 2: Info, 3: Debug\n");
    LOG_MSG("                  Default: 0\n");
    LOG_MSG("-i [file]         Input file for File Reader component.\n");
    LOG_MSG("-o [prefix]       Output file prefix. The output file of pipeline [n] follows:\n");
    LOG_MSG("                       [prefix]_out_[n]_[width]x[height].yuv\n");
    LOG_MSG("-cf [file]        Set configuration file.\n");
    LOG_MSG("                  Default: NVMEDIA_CAPTURE2D_CONFIG environment variable, if set\n");
    LOG_MSG("                  Otherwise: configs/default.conf\n");
    LOG_MSG("-c [name]        Parameters set name to be used for capture configuration.\n");
    LOG_MSG("                  Default: First config set (capture-params-set)\n");
    LOG_MSG("-lps              List available configuration parameter sets\n");
    LOG_MSG("--aggregate [n]   Treat captured frames as aggregated image with <n> siblings\n");
    LOG_MSG("                  Default: n = 4\n");
    LOG_MSG("--isp-mv          Enable ISP to generate machine vision images.\n");
    LOG_MSG("                  It must be followed by one of the supported surface \n");
    LOG_MSG("                  types: yuv420, y10\n");
    LOG_MSG("--plugin          Use IPP plugins for control algorithm components\n");
    LOG_MSG("--nvplugin        Use Nvmedia IPP plugins for control algorithm components\n");
    LOG_MSG("--save-ispmv [prefix]\n");
    LOG_MSG("       Save the machine vison output of the ISP component.\n");
    LOG_MSG("       The file name used to save surfaces of pipeline [n] has the follwing format:\n");
    LOG_MSG("           <prefix>_ispmv_<n>_<width>x<height>.yuv\n");
    LOG_MSG("       Existing file of the same name will be overwritten.\n");
    LOG_MSG("--save-metadata [prefix]\n");
    LOG_MSG("       Save metada to file for each pipeline.\n");
    LOG_MSG("       The file name used to save metadata of pipeline [n] has the follwing format:\n");
    LOG_MSG("           <prefix>_<n>.meta\n");
    LOG_MSG("       Existing file of the same name will be overwritten.\n");
    LOG_MSG("--bayer-format [format] Bayer format of the input file. Supported formats:\n");
    LOG_MSG("                    BGGR (default)\n");
    LOG_MSG("                    RGGB\n");
    LOG_MSG("                    GRBG\n");
    LOG_MSG("                    GBRG\n");
}
