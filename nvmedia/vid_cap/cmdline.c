/*
 * Copyright (c) 2013-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cmdline.h"
#include "config_parser.h"
#include "log_utils.h"
#include "misc_utils.h"

#define MAX_CONFIG_SECTIONS    128

typedef struct _NvmCaptureConfig {
  char name[MAX_STRING_SIZE];
  char description[MAX_STRING_SIZE];
  char board[MAX_STRING_SIZE];
  char inputDevice[MAX_STRING_SIZE];
  char inputFormat[MAX_STRING_SIZE];
  char surfaceFormat[MAX_STRING_SIZE];
  char resolution[MAX_STRING_SIZE];
  char interface[MAX_STRING_SIZE];
  int  i2cDevice;
  unsigned int csiLanes;
  unsigned int extraLines;
} NvmCaptureConfig;

static NvmCaptureConfig sCaptureConfigSetsCollection[MAX_CONFIG_SECTIONS];

SectionMap sectionsMap[] = {
    {SECTION_CAPTURE, "capture-params-set", 0, sizeof(NvmCaptureConfig)},
    {SECTION_NONE,    "",                   0, 0} // Specifies end of array
};

ConfigParamsMap paramsMap[] = {
  //{paramName, mappedLocation, type, defaultValue, paramLimits, minLimit, maxLimit, stringLength, stringLengthAddr, sectionType}
    {"name",               &sCaptureConfigSetsCollection[0].name,           TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"description",        &sCaptureConfigSetsCollection[0].description,    TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"board",              &sCaptureConfigSetsCollection[0].board,          TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"input_device",       &sCaptureConfigSetsCollection[0].inputDevice,    TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"input_format",       &sCaptureConfigSetsCollection[0].inputFormat,    TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"surface_format",     &sCaptureConfigSetsCollection[0].surfaceFormat,  TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"resolution",         &sCaptureConfigSetsCollection[0].resolution,     TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"csi_lanes",          &sCaptureConfigSetsCollection[0].csiLanes,       TYPE_UINT,      2, LIMITS_MIN,  0, 0, 0, 0, SECTION_CAPTURE},
    {"interface",          &sCaptureConfigSetsCollection[0].interface,      TYPE_CHAR_ARR,  0, LIMITS_NONE, 0, 0, MAX_STRING_SIZE, 0, SECTION_CAPTURE},
    {"extra_lines",        &sCaptureConfigSetsCollection[0].extraLines,     TYPE_UINT,      0, LIMITS_MIN,  0, 0, 0, 0, SECTION_CAPTURE},
    {"i2c_device",         &sCaptureConfigSetsCollection[0].i2cDevice,      TYPE_INT,      -1, LIMITS_NONE, 0, 0, 0, 0, SECTION_CAPTURE},
    {NULL} // Specifies the end of the array
};

void
PrintUsage()
{
    NvMediaVideoOutputDeviceParams videoOutputs[MAX_OUTPUT_DEVICES];
    NvMediaStatus rt;
    int outputDevicesNum, i;

    LOG_MSG("Usage: nvmvid_cap [options]\n");
    LOG_MSG("Options:\n");
    LOG_MSG("-h                 Print usage\n");
    LOG_MSG("-info              Detect the board connected and display info\n");
    LOG_MSG("                   the capture modules present thereon\n");
    LOG_MSG("-cf  [config file] Capture configuration file used.\n");
    LOG_MSG("                   Default:\n");
    LOG_MSG("                   NVMEDIA_TEST_CAPTURE_CONFIG environment variable value\n");
    LOG_MSG("                   or \"configs/capture.conf\" if not set\n");
    LOG_MSG("-lc                List all the available capture configuration sets\n");
    LOG_MSG("-c   [config set]  Capture configuration set name to be used\n");
    LOG_MSG("-m   [cb/live]     Mode of operation. Default: live\n");
    LOG_MSG("                   Ignored for board = 1688 (only live is valid)\n");
    LOG_MSG("                   Options:\n");
    LOG_MSG("                   cb: test mode (color bars)\n");
    LOG_MSG("                   live: capture mode\n");
    LOG_MSG("-crc [checksum]    Verify CRC checksum for every frame.\n");
    LOG_MSG("                   Valid in test mode only (m = cb)\n");
    LOG_MSG("-n   [frames]      Number of frames to be captured. Default: -1\n");
    LOG_MSG("                   Use count = -1 for endless capture\n");
    LOG_MSG("-t   [seconds]     Capture duration\n");
    LOG_MSG("-i   [deinterlace] De-interlacing mode.\n");
    LOG_MSG("                   0(Off)\n");
    LOG_MSG("                   1(BOB)\n");
    LOG_MSG("                   2(Advanced-Frame Rate)\n");
    LOG_MSG("                   3(Advanced-Field Rate)\n");
    LOG_MSG("-ia  [algorithm]   Deinterlacing algorithm\n");
    LOG_MSG("                   Used only for advanced deinterlacing modes\n");
    LOG_MSG("                   1(Advanced1)\n");
    LOG_MSG("                   2(Advanced2)\n");
    LOG_MSG("-it                Use inverse telecine\n");
    LOG_MSG("-of  [output file] Dump captured frames to file. Default: off\n");
    LOG_MSG("                   File name can include \"%d\" for the frame numbering.\n");
    LOG_MSG("                   Example: F%d.yuv will produce the files: F1.yuv, F2.yuv...\n");
    LOG_MSG("                   If %d not present in the file name,\n");
    LOG_MSG("                   _n will be appended to the file name for each frame\n");
    LOG_MSG("-ot  [output type] Output type. Options: [kd/o/orgb/oyuv]. Default: o (overlay)\n");
    LOG_MSG("-w   [id]          Window ID. Default: 1.\n");
    LOG_MSG("-p   [position]    Window position. Default: full screen size.\n");
    LOG_MSG("-z   [depth]       Window depth. Default: 1.\n");
    LOG_MSG("-d   [id]          Display ID. Default: none.\n");
    LOG_MSG("-e                 Enable external buffer allocation.\n");

    rt = GetAvailableDisplayDevices(&outputDevicesNum, &videoOutputs[0]);
    if(rt != NVMEDIA_STATUS_OK) {
        LOG_ERR("PrintUsage: Failed retrieving available video output devices\n");
        return;
    }

    LOG_MSG("                   Available display devices: (%d)\n", outputDevicesNum);
    for(i = 0; i < outputDevicesNum; i++) {
        LOG_MSG("                        Display ID: %d\n", videoOutputs[i].displayId);
    }
    LOG_MSG("-v   [level]       Logging Level = 0(Errors), 1(Warnings), 2(Info), 3(Debug)\n");
}

static int
GetCaptureParamsSectionID(
    NvmCaptureConfig *captureConfigSetsList,
    int paramSets,
    char* paramSetName)
{
    int i;

    for(i = 0; i < paramSets; i++) {
        if(!strcmp(captureConfigSetsList[i].name, paramSetName)) {
            LOG_DBG("%s: Param set ID found (%d)\n", __func__, i);
            return i;
        }
    }

    return -1;
}


static int
ParseConfigFile(TestArgs *args)
{
  NvMediaStatus result;

  ConfigParser_InitParamsMap(paramsMap);
  result = ConfigParser_ParseFile(paramsMap,
                                  MAX_CONFIG_SECTIONS,
                                  sectionsMap,
                                  &args->configFileName[0]);
  if(result != NVMEDIA_STATUS_OK) {
    LOG_ERR("%s: ConfigParser_ParseFile failed\n", __func__);
    return -1;
  }

  return 0;
}

static int
SetConfigParams(
     TestArgs *args,
     NvmCaptureConfig *config)
{
    int width = 0, height = 0;
    char *separateToken;
    char resolutionSplit[2][5];
    char boardSplit[2][MAX_STRING_SIZE];
    BoardVersion boardVersion = BOARD_VERSION_A00;
    ModuleType moduleType = MODULE_TYPE_NONE;
    int i2c = -1;

    /* App defaults */
    args->captureDeviceInUse = AnalogDevices_ADV7180;
    args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB; //default
    args->csiInputFormat = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422;
    LOG_DBG("%s: Set resolution %s\n", __func__, config->resolution);
    separateToken = strchr (config->resolution, 'x');
    strncpy(resolutionSplit[0],
            config->resolution,
            separateToken - config->resolution);
    resolutionSplit[0][separateToken - config->board] = '\0';
    strcpy(resolutionSplit[1], separateToken + 1);
    width = atoi(resolutionSplit[0]);
    height = atoi(resolutionSplit[1]);
    args->inputWidth = width;
    args->inputHeight = height;

    args->csiInterfaceLaneCount = config->csiLanes;
    if(args->csiInterfaceLaneCount != 1 && args->csiInterfaceLaneCount != 2 && args->csiInterfaceLaneCount != 4) {
        LOG_ERR("%s: Bad # CSI interface lanes specified in config file: %s.using lanes - 2 as default\n",
                __func__, config->csiLanes);
        args->csiInterfaceLaneCount = 2;
    }

    LOG_DBG("%s: Set board-type\n", __func__);
    separateToken = strchr (config->board, '-');
    strncpy (boardSplit[0], config->board, separateToken - config->board);
    boardSplit[0][separateToken - config->board] = '\0';
    strcpy (boardSplit[1], separateToken + 1);

    args->boardType = BOARD_TYPE_NONE;
    if(!strcmp(boardSplit[0], "E1688"))
        args->boardType = BOARD_TYPE_E1688;
    else if(!strcmp(boardSplit[0], "E1853"))   //...jettson (vip)
        args->boardType = BOARD_TYPE_E1853;
    else if(!strcmp(boardSplit[0], "E1861"))   //...jettson (csi-hdmi, csi-cvbs)
        args->boardType = BOARD_TYPE_E1861;
    else if(!strcmp(boardSplit[0], "E1611"))         //...dalmore
        args->boardType = BOARD_TYPE_E1611;
    else if(!strcmp(boardSplit[0], "PM358"))         //...laguna
        args->boardType = BOARD_TYPE_PM358;
    else {
        LOG_INFO("%s: no board type needed for %s. Using no board type.\n", __func__, boardSplit[0]);
        args->boardType = BOARD_TYPE_NONE;
    }

    LOG_DBG("%s: Set board version\n", __func__);
    if(!strcmp(boardSplit[1], "a00"))
        boardVersion = BOARD_VERSION_A00;
    else if(!strcmp(boardSplit[1], "a01"))
        boardVersion = BOARD_VERSION_A01;
    else if(!strcmp(boardSplit[1], "a02"))
        boardVersion = BOARD_VERSION_A02;
    else if(!strcmp(boardSplit[1], "a03"))
        boardVersion = BOARD_VERSION_A03;
    else if(!strcmp(boardSplit[1], "a04"))
        boardVersion = BOARD_VERSION_A04;
    else if(!strcmp(boardSplit[1], "b00"))
        boardVersion = BOARD_VERSION_B00;
    else {
        LOG_ERR("%s: Bad board version specified: %s. Using default.\n",
                __func__, boardSplit[1]);
        boardVersion = BOARD_VERSION_A02;
    }

    LOG_DBG("%s: Set module-type\n", __func__);
    if(!strcmp(config->inputDevice, "tc358743")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_H2C;
        args->captureDeviceInUse = Toshiba_TC358743;
    } else if(!strcmp(config->inputDevice, "tc358791")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_H2C;
        args->captureDeviceInUse = Toshiba_TC358791;
    } else if(!strcmp(config->inputDevice, "tc358791_cvbs")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_C2C;
        args->captureDeviceInUse = Toshiba_TC358791_CVBS;
    } else if(!strcmp(config->inputDevice, "ds90uh940")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_F2C;
        args->captureDeviceInUse = TI_DS90UH940;
    } else if(!strcmp(config->inputDevice, "adv7281")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_C2C;
        args->captureDeviceInUse = AnalogDevices_ADV7281;
    } else if(!strcmp(config->inputDevice, "adv7282")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_C2C;
        args->captureDeviceInUse = AnalogDevices_ADV7282;
    } else if(!strcmp(config->inputDevice, "adv7180")) {
        moduleType = MODULE_TYPE_CAPTURE_VIP;
        args->captureDeviceInUse = AnalogDevices_ADV7180;
    } else if(!strcmp(config->inputDevice, "adv7182")) {
        moduleType = MODULE_TYPE_CAPTURE_VIP;
        args->captureDeviceInUse = AnalogDevices_ADV7182;
    } else if(!strcmp(config->inputDevice, "adv7481c")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_C2C;
        args->captureDeviceInUse = AnalogDevices_ADV7481C;
    } else if(!strcmp(config->inputDevice, "adv7481h")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_H2C;
        args->captureDeviceInUse = AnalogDevices_ADV7481H;
    } else if(!strcmp(config->inputDevice, "null")) {
        moduleType = MODULE_TYPE_CAPTURE_CSI_H2C;
        args->captureDeviceInUse = CapureInputDevice_NULL;
    } else {
        LOG_ERR("%s: Bad capture interface: %s\n", __func__, config->inputDevice);
        moduleType = MODULE_TYPE_NONE;
    }

    LOG_DBG("%s: Check board type and capture module combination\n", __func__);
    if(args->boardType && testutil_board_module_query(args->boardType, boardVersion, moduleType, 0))
    {
        LOG_ERR("%s: Invalid board and capture interface combination. Capture modules on the selected board are given below.\n", __func__);
        LOG_ERR("%s:  VIP:\n", __func__);
        testutil_board_module_query(args->boardType, boardVersion, MODULE_TYPE_CAPTURE_VIP, 1);
        LOG_ERR("%s:  CSI-H2C:\n", __func__);
        testutil_board_module_query(args->boardType, boardVersion, MODULE_TYPE_CAPTURE_CSI_H2C, 1);
        LOG_ERR("%s:  CSI-C2C:\n", __func__);
        testutil_board_module_query(args->boardType, boardVersion, MODULE_TYPE_CAPTURE_CSI_C2C, 1);
        return -1;
    }

    if (config->i2cDevice >= 0) {
        LOG_DBG("%s: Set i2c device from config file (%d)\n", __func__, config->i2cDevice);
        args->i2cDevice = config->i2cDevice;
    } else if(!testutil_board_module_get_i2c(args->boardType, boardVersion, moduleType, &i2c)) {
        LOG_DBG("%s: Set default i2c device found by testutil_board_module_get_i2c (%d)\n", __func__, i2c);
        args->i2cDevice = i2c;
    }

    if(moduleType == MODULE_TYPE_CAPTURE_VIP)
        args->captureType = CAPTURE_VIP;
    else if(moduleType == MODULE_TYPE_NONE)
        args->captureType = CAPTURE_NONE;
    else
        args->captureType = CAPTURE_CSI;

    if(args->captureType == CAPTURE_CSI) {
        LOG_DBG("%s: Check input_std. Valid only for csi-cvbs\n", __func__);
        if(moduleType == MODULE_TYPE_CAPTURE_CSI_C2C)
            if(args->inputWidth != 720 || (args->inputHeight != 480 && args->inputHeight != 576))
                LOG_ERR("%s: csi-cvbs capture supports only 480i and 576i input resolutions. Bad resolution specified: %s.\n", __func__, config->resolution);

        // Input format
        if(moduleType == MODULE_TYPE_CAPTURE_CSI_C2C) {
            args->csiInterfaceLaneCount = 1;
            if(!strcmp(config->inputFormat, "422i")) {
                args->csiInputFormat = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422;
                args->csiCaptureInterlaced = 1;
                args->inputVideoStd = (args->inputHeight == 480) ? 0 : 1;
            } else
                LOG_ERR("%s: csi-cvbs capture supports only 422i input format.Bad format specified: %s.\n", __func__, config->inputFormat);
        } else { //MODULE_TYPE_CAPTURE_CSI_H2C
            if(!strcmp(config->inputFormat, "422i")) {
                args->csiInputFormat = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422;
                args->csiCaptureInterlaced = 1;
            } else if(!strcmp(config->inputFormat, "422p")) {
                args->csiInputFormat = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422;
            } else if(!strcmp(config->inputFormat, "rgb")) {
                args->csiInputFormat = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_RGB888;
            }
            else
                LOG_ERR("%s: csi-hdmi capture supports only 422p/422i/rgb input format.Bad format specified: %s.\n", __func__, config->inputFormat);
        }
        LOG_DBG("%s: CSI input format: %s, interlaced: %d, \n",
                __func__, args->csiInputFormat == NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422 ? "YUV422" : "RGB888", args->csiCaptureInterlaced);

        // Interface type
        if(!strcmp(config->interface, "csi-a"))
            args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_A;
        else if(!strcmp(config->interface, "csi-b"))
            args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_B;
        else if(!strcmp(config->interface, "csi-ab"))
            args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB;
        else if(!strcmp(config->interface, "csi-cd"))
            args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_CD;
        else if(!strcmp(config->interface, "csi-e"))
            args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_E;
        else if(!strcmp(config->interface, "csi-ef"))
            args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_EF;
        else {
            LOG_ERR("%s: Bad interface-type specified: %s.Using csi-ab as default\n", __func__, config->interface);
            args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_AB;
        }
        LOG_DBG("%s: CSI input port: %d\n", __func__, args->csiPortInUse);
    } else if(args->captureType == CAPTURE_VIP) {
        //input_std
        if(moduleType == MODULE_TYPE_CAPTURE_VIP)
            if(args->inputWidth != 720 || (args->inputHeight != 480 && args->inputHeight != 576))
                LOG_ERR("%s: vip capture supports only 480i and 576i input resolutions. Bad resolution specified: %s.\n", __func__, config->resolution);

        // Interface type
        if(!strcmp(config->interface, "vip")) {
            if(args->inputHeight == 480)
                args->inputVideoStd = NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_VIP_NTSC;
            else
                args->inputVideoStd = NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_VIP_PAL;
        }
   }

   args->csiExtraLines = config->extraLines;

   if(!strcmp(config->surfaceFormat, "nv24")) {
       LOG_ERR("%s: obsolete surface format, please use yuyv_i\n", __func__);
       args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_YUYV_422_I;
       args->outputSurfaceType = NvMediaSurfaceType_VideoCapture_YUYV_422;
   } else if (!strcmp(config->surfaceFormat, "yv12x2")) {
       LOG_ERR("%s: obsolete surface format, please use yuyv_i\n", __func__);
       args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_YUYV_422_I;
       args->outputSurfaceType = NvMediaSurfaceType_VideoCapture_YUYV_422;
   } else if (!strcmp(config->surfaceFormat, "yv12")) {
       LOG_ERR("%s: obsolete surface format, please use yv16\n", __func__);
       args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_422;
   } else if (!strcmp(config->surfaceFormat, "yuyv_i")) {
        args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_YUYV_422_I;
        args->outputSurfaceType = NvMediaSurfaceType_VideoCapture_YUYV_422;
   } else if (!strcmp(config->surfaceFormat, "yv16x2")) {
       args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_Y_V_U_422;
       args->outputSurfaceType = NvMediaSurfaceType_VideoCapture_422;
   } else if (!strcmp(config->surfaceFormat, "yv16")) {
       args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_422;
   } else if (!strcmp(config->surfaceFormat, "rgb")) {
       args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_R8G8B8A8;
       args->outputSurfaceType = NvMediaSurfaceType_R8G8B8A8;
   } else {
        LOG_WARN("%s: Bad CSI capture surface format: %s. Using yv16 as default\n", __func__, config->surfaceFormat);
        args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_422;
   }

   LOG_DBG("%s: Capture device: %s, resolution: (%u,%u), extra lines: %u\n", __func__,
           (args->captureDeviceInUse ==  Toshiba_TC358743) ? "Toshiba_TC358743" :
           ((args->captureDeviceInUse ==  Toshiba_TC358791) ? "Toshiba_TC358791" :
           ((args->captureDeviceInUse ==  AnalogDevices_ADV7281) ? "AnalogDevices_ADV7281" :
           ((args->captureDeviceInUse ==  AnalogDevices_ADV7282) ? "AnalogDevices_ADV7282" :
           ((args->captureDeviceInUse ==  AnalogDevices_ADV7481C || args->captureDeviceInUse ==  AnalogDevices_ADV7481H) ? "AnalogDevices_ADV7481" :
           "null")))), args->inputWidth, args->inputHeight, args->csiExtraLines);

   return 0;
}

int ParseArgs(int argc, char *argv[], TestArgs *args)
{
    NvMediaBool bDataAvailable = NVMEDIA_FALSE;
    NvMediaBool bLastArg = NVMEDIA_FALSE;
    unsigned int j;
    int i = 1, x = 0, y = 0, w = 0, h = 0;
    char *captureConfigFileName = NULL;
    NvMediaStatus rt;

    /* app defaults */
    captureConfigFileName = getenv("NVMEDIA_TEST_CAPTURE_CONFIG");
    if(captureConfigFileName) {
        strcpy(args->configFileName, captureConfigFileName);
    } else {
        // Default config file if environment variable not set and -cf option not used
        strcpy(args->configFileName, "configs/capture.conf");
    }
    args->i2cDevice = I2C1;
    args->isLiveMode = NVMEDIA_TRUE;
    args->checkCRC = NVMEDIA_FALSE;
    args->crcChecksum = 0;
    args->captureType = CAPTURE_CSI;
    args->captureDeviceInUse = Toshiba_TC358743;
    args->csiPortInUse = NVMEDIA_VIDEO_CAPTURE_CSI_INTERFACE_TYPE_CSI_A;
    args->csiInterfaceLaneCount = 2;
    args->inputVideoStd = 2; // Invalid value
    args->csiInputFormat = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_RGB888;
    args->csiCaptureInterlaced = NVMEDIA_FALSE;
    args->csiDeinterlaceEnabled = NVMEDIA_FALSE;
    args->csiDeinterlaceType = NVMEDIA_DEINTERLACE_TYPE_BOB;
    args->inputWidth = 720;
    args->inputHeight = 480;
    args->csiExtraLines = 0;
    args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_R8G8B8A8;
    args->mixerWidth = 0;
    args->mixerHeight = 0;
    args->aspectRatio = 0.0f;
    args->displayEnabled = NVMEDIA_FALSE;
    args->outputType = NvMediaVideoOutputType_Overlay;
    args->fileDumpEnabled = NVMEDIA_FALSE;
    args->captureTime = 0;
    args->captureCount = 0;
    args->inputVideoStd = NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_VIP_NTSC;
    args->timeout = 100;
    args->externalBuffer = 0;
    args->displaysList.depth = 1;
    args->displaysList.windowId = 1;

    while(i < argc && argv[i][0] == '-') {
        // check if this is the last argument
        bLastArg = ((argc - i) == 1);

        // check if there is data available to be parsed following the option
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if(!strcmp(argv[i], "-info")) {
            struct {
                BoardType type;
                char *name;
            } boardTypesList[] = {
                { BOARD_TYPE_E1688, "E1688" },
                { BOARD_TYPE_E1853, "E1853" },
                { BOARD_TYPE_E1861, "E1861" },
                { BOARD_TYPE_E1611, "E1611" },
                { BOARD_TYPE_PM358, "PM358" }};

            struct {
                BoardVersion version;
                char *name;
            } boardVersionsList[] = {
                { BOARD_VERSION_A00, "A00" },
                { BOARD_VERSION_A00, "A01" },
                { BOARD_VERSION_A00, "A02" },
                { BOARD_VERSION_A00, "A03" },
                { BOARD_VERSION_A00, "A04" },
                { BOARD_VERSION_B00, "B00" }};

            BoardType t;
            BoardVersion v;

            for(t = BOARD_TYPE_E1688; t <= BOARD_TYPE_PM358; t++) {
                for(v = BOARD_VERSION_A00; v <= BOARD_VERSION_B00; v++) {
                    if(!testutil_board_detect(t, v)) {
                        if(!testutil_board_module_query(t, v, MODULE_TYPE_NONE, 0))
                            LOG_MSG("%s: Detected board: %s %s\n", __func__,
                                     boardTypesList[t - 1].name,
                                     boardVersionsList[v - 1].name);
                        else
                            LOG_MSG("%s: Failed detecting board\n", __func__);

                        return 0;
                    }
                }
            }

            LOG_ERR("%s: Failed to detect the board connected\n", __func__);
            return 1;
        }
        else if(!strcmp(argv[i], "-h")) {
            PrintUsage();
            return 1;
        }
        else if(!strcmp(argv[i], "-lc")) {
          if(!ParseConfigFile(args)) {
              LOG_MSG("%s: parameter sets count: %d\n", __func__, sectionsMap[0].lastSectionIndex + 1);
              for (j = 0; j <= sectionsMap[0].lastSectionIndex; j++) {
                  LOG_MSG("%s\t", sCaptureConfigSetsCollection[j].name);
                  LOG_MSG("%s\n", sCaptureConfigSetsCollection[j].description);
              }
              LOG_MSG("\n");
              return 1;
          } else {
              LOG_ERR("%s: Failed to parse config file %s\n", __func__, args->configFileName);
              return -1;
          }
        }
        else if(!strcmp(argv[i], "-cf")) {
            if(bDataAvailable) {
                if(argv[++i] != NULL) {
                    memset(args->configFileName, '\n', sizeof(args->configFileName));
                    strcpy(args->configFileName, argv[i]);
                }
            }
        }
        else if(!strcmp(argv[i], "-c")) {
            if(++i < argc) {
                int paramSetId = 0;
                if(argv[i] != NULL)
                    args->paramSetName = argv[i];
                    LOG_DBG("%s: -c option encountered. Parsing config file %s.\n", __func__, args->configFileName);
                    if(!ParseConfigFile(args)) {
                        paramSetId = GetCaptureParamsSectionID(sCaptureConfigSetsCollection,
                                                               sectionsMap[0].lastSectionIndex + 1,
                                                               args->paramSetName);
                        if(paramSetId == -1) {
                            paramSetId = 0; // Params set name doesn't exist; use default
                            LOG_WARN("%s: Params set name '%s' wasn't found. Using param-set:1 as default.\n", __func__, args->paramSetName);
                        }
                        if(SetConfigParams(args, &sCaptureConfigSetsCollection[paramSetId])){
                            LOG_ERR("%s: Failed config params\n", __func__);
                            return -1;
                        }
                        LOG_INFO ("%s: Capture using params set index: %d, name: %s\n",
                                  __func__, paramSetId, sCaptureConfigSetsCollection[paramSetId].name);
                    } else {
                        LOG_ERR("%s: Failed to parse config file\n", __func__);
                        return -1;
                    }
            }
        }
        else if(!strcmp(argv[i], "-m")) {
            if(bDataAvailable) {
                if(!strcmp(argv[++i], "cb"))
                    args->isLiveMode = NVMEDIA_FALSE;
                else if(!strcmp(argv[i], "live"))
                    args->isLiveMode = NVMEDIA_TRUE;
                else {
                    LOG_ERR("%s: Bad test mode: %s\n", __func__, argv[i]);
                    return -1;
                }
                LOG_DBG("%s: -m option encountered. Setting to %s mode.\n", __func__, args->isLiveMode ? "live" : "cb");
            } else {
                LOG_ERR("%s: Missing test mode\n", __func__);
                return -1;
            }
        }
        else if(!strcmp(argv[i], "-crc")) {
            if(bDataAvailable) {
                if(sscanf(argv[++i], "%u", &args->crcChecksum) != 1) {
                    LOG_ERR("%s: Bad CRC checksum: %s\n", __func__, argv[i]);
                    return -1;
                }
                else
                    args->checkCRC = NVMEDIA_TRUE;
                LOG_DBG("%s: -crc option encountered. Enabling CRC checks.\n", __func__);
            } else {
                LOG_ERR("%s: Missing CRC checksum\n", __func__);
                return -1;
            }
        }
        else if(!strcmp(argv[i], "-n")) {
            if(argv[i+1]) {
                if(sscanf(argv[++i], "%d", &args->captureCount) != 1) {
                    LOG_ERR("%s: Bad capture frames count: %s\n", __func__, argv[i]);
                    return -1;
                }
                LOG_DBG("%s: -n option encountered. Setting number of frames to capture to %d.\n", __func__, args->captureCount);
            } else {
                LOG_ERR("%s: Missing capture count\n", __func__);
                return -1;
            }
        }
        else if(!strcmp(argv[i], "-t")) {
            if(bDataAvailable) {
                if(sscanf(argv[++i], "%u", &args->captureTime) != 1) {
                    LOG_ERR("%s: Bad capture duration: %s\n", __func__, argv[i]);
                    return -1;
                }
                LOG_DBG("%s: -t option encountered. Setting capture time to %u (sec).\n", __func__, args->captureTime);
            } else {
                LOG_ERR("%s: -t option expects time (seconds) as parameter\n", __func__);
                return -1;
            }
        }
        else if(!strcmp(argv[i], "-i")) {
            args->csiDeinterlaceEnabled = 1;
            if (bDataAvailable) {
                int deinterlace;
                if (sscanf(argv[++i], "%d", &deinterlace) && deinterlace > 0 && deinterlace < 4) {
                    args->csiDeinterlaceType = deinterlace;
                } else {
                    LOG_ERR("%s: Invalid deinterlace mode encountered (%s)\n", __func__, argv[i]);
                    return -1;
                }
            } else {
                LOG_ERR("%s: -i must be followed by deinterlacing mode\n", __func__);
                return -1;
            }
        }
        else if(!strcmp(argv[i], "-ia")) {
            if (bDataAvailable) {
                int deinterlaceAlgo;
                if (sscanf(argv[++i], "%d", &deinterlaceAlgo) && deinterlaceAlgo >= 0 && deinterlaceAlgo < 3) {
                    args->csiDeinterlaceAlgo = deinterlaceAlgo;
                } else {
                    LOG_ERR("%s: Invalid deinterlace algorithm encountered (%s)\n", __func__, argv[i]);
                    return -1;
                }
            } else {
                LOG_ERR("%s: -ia must be followed by deinterlacing algorithm\n", __func__);
                return -1;
            }
        }
        else if(!strcmp(argv[i], "-it")) {
            args->csiInverceTelecine = NV_TRUE;
        }
        else if(!strcmp(argv[i], "-timeout")) {
            if(++i < argc) {
                args->timeout = atoi(argv[i]);
                LOG_DBG("%s: -timeout option encountered. Setting timeout to %u.\n", __func__, args->timeout);
            } else
                LOG_ERR("%s: Missing timeout value; Using default of 100 ms\n", __func__);

        }
        else if(strcmp(argv[i], "-d") == 0) {
            if(bDataAvailable) {
                if((sscanf(argv[++i], "%u", &args->displaysList.displayId) != 1)) {
                    LOG_ERR("%s: Bad display id: %s\n", __func__, argv[i]);
                    return -1;
                }
                rt = CheckDisplayDeviceID(args->displaysList.displayId, &args->displaysList.isEnabled);
                if(rt != NVMEDIA_STATUS_OK) {
                    LOG_ERR("%s: Chosen display (%d) not available\n", __func__, args->displaysList.displayId);
                    return -1;
                }

                args->displayEnabled = NVMEDIA_TRUE;

                LOG_DBG("%s: -d option encountered. Chosen display: (%d) device enabled? %d\n", __func__,
                        args->displaysList.displayId, args->displaysList.isEnabled);
            } else {
                LOG_ERR("%s: -d must be followed by display id\n", __func__);
                return -1;
            }
        }
        else if(strcmp(argv[i], "-of") == 0) {
            if(bDataAvailable) {
                args->fileDumpEnabled = NVMEDIA_TRUE;
                args->outputFileName = argv[++i];
                LOG_DBG("%s: -of option encountered. Enabling file dump to file %s\n", __func__, args->outputFileName);
            } else {
                LOG_ERR("%s: -of must be followed by output file name\n", __func__);
                return -1;
            }
        }
        else if(strcmp(argv[i], "-ot") == 0) {
            if(bDataAvailable) {
                char *arg = argv[++i];
                LOG_DBG("%s: -ot option encountered. Setting output type to %s\n", __func__, arg);
                if (!strcmp(arg, "orgb"))
                    args->outputType = NvMediaVideoOutputType_OverlayRGB;
                else if (!strcmp(arg, "oyuv"))
                    args->outputType = NvMediaVideoOutputType_OverlayYUV;
                else if (!strcmp(arg, "o"))
                    args->outputType = NvMediaVideoOutputType_Overlay;
                else {
                    LOG_ERR("%s: Bad output type: %s\n", __func__, arg);
                    return -1;
                }
            } else {
                LOG_ERR("%s: -ot must be followed by output type\n", __func__);
                return -1;
            }
        }
        else if(strcmp(argv[i], "-w") == 0) {
            if(bDataAvailable) {
                char *arg = argv[++i];
                args->displaysList.windowId = atoi(arg);
                LOG_DBG("%s: -w option encountered. Chosen window ID: %u\n", __func__, args->displaysList.windowId);
            } else {
                LOG_ERR("%s: -w must be followed by window id\n", __func__);
                return -1;
            }
            if(args->displaysList.windowId > 2) {
                LOG_ERR("%s: Bad window ID: %d. Valid values are [0-2]. ", __func__, args->displaysList.windowId);
                LOG_ERR("%s: Using default window ID 0\n", __func__);
                args->displaysList.windowId = 0;
            }
        }
        else if(strcmp(argv[i], "-p") == 0) {
            if(bDataAvailable) {
                if((sscanf(argv[++i], "%u:%u:%u:%u", &x, &y, &w, &h) != 4)) {
                    LOG_ERR("%s: Bad resolution: %s\n", __func__, argv[i]);
                    return -1;
                }
                args->displaysList.position.x0 = x;
                args->displaysList.position.y0 = y;
                args->displaysList.position.x1 = x + w;
                args->displaysList.position.y1 = y + h;
                args->displaysList.isPositionSpecified = NV_TRUE;
                LOG_DBG("%s: -p option encountered. Chosen window position: [x:%d,y:%d,w:%d,h:%d]\n", __func__, x, y, w, h);
            } else {
                LOG_ERR("%s: -p must be followed by window position x0:x1:width:height\n", __func__);
                return -1;
            }
        }
        else if(strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "-z2") == 0) {
            if(bDataAvailable) {
                char *arg = argv[++i];
                args->displaysList.depth = atoi(arg);
                LOG_DBG("%s: -z option encountered. Chosen window depth: %u\n", __func__, args->displaysList.depth);
            } else {
                LOG_ERR("%s: -z must be followed by depth value\n", __func__);
                return -1;
            }
            if(args->displaysList.depth > 255) {
                LOG_ERR("%s: Bad depth value: %d. Valid values are [0-255]. ", __func__, args->displaysList.depth);
                LOG_ERR("%s: Using default depth value: 1\n", __func__);
                args->displaysList.depth = 1;
            }
        }
        else if(strcmp(argv[i], "-v") == 0) {
            if(bDataAvailable) {
                char *arg = argv[++i];
                args->logLevel = atoi(arg);
                if(args->logLevel < LEVEL_ERR || args->logLevel > LEVEL_DBG) {
                    LOG_ERR("%s: Invalid logging level chosen (%d). ", __func__, args->logLevel);
                    LOG_ERR("%s: Setting logging level to LEVEL_ERR (0)\n", __func__);
                }
            } else {
                args->logLevel = LEVEL_DBG; // Max logging level
            }
            SetLogLevel(args->logLevel);
        }
        else if(strcmp(argv[i], "-e") == 0) {
            args->externalBuffer = NVMEDIA_TRUE;
        }
        else {
            LOG_ERR("%s: %s is not a supported option\n", __func__, argv[i]);
            return -1;
        }

        i++;
    }

    if(i < argc) {
        LOG_ERR("%s: %s is not a supported option\n", __func__, argv[i]);
        return -1;
    }

    // Check for consistency
    if(args->captureType == CAPTURE_NONE) {
        LOG_ERR("%s: Capture input method/device wasn't found\n", __func__);
        return -1;
    } else if(args->captureType == CAPTURE_CSI) {
        if(!args->isLiveMode) {
            args->inputWidth = 640;
            args->inputHeight = 480;
            LOG_DBG("%s: CSI is in use in cb mode. Setting resolution to [%u:%u]\n", __func__, args->inputWidth, args->inputHeight);
        }

        if(args->captureDeviceInUse == NationalSemi_DS90UR910Q ||
           args->captureDeviceInUse == TI_DS90UH940) {
            args->csiInputFormat = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_RGB888;
            args->csiSurfaceFormat = NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_R8G8B8A8;
            LOG_DBG("%s: NationalSemi_DS90UR910Q or TI_DS90UH940 capture device is in use. Setting input format to RGB888 and surface format to R8G8B8A8\n", __func__);
        }

        if(args->captureDeviceInUse == AnalogDevices_ADV7282 ||
           args->captureDeviceInUse == AnalogDevices_ADV7281 ||
           args->captureDeviceInUse == AnalogDevices_ADV7481C) {
            args->csiInputFormat = NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422;
            args->inputWidth = 720;
            if(args->inputVideoStd == NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_VIP_PAL)
                args->inputHeight = 576;
            else {
                args->inputVideoStd = NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_VIP_NTSC;
                args->inputHeight = 480;
            }
            LOG_DBG("%s: AnalogDevices_ADV7281/7282/7481 capture device used. Setting input resolution to [%d,%d]\n", __func__,
                    args->inputWidth, args->inputHeight);
        }

        if(args->csiDeinterlaceEnabled) {
            LOG_DBG("%s: Check CSI deinterlacing parameters\n", __func__);
            if(!args->csiCaptureInterlaced) {
                LOG_ERR("%s: deinterlacing is not a supported option for progressive CSI capture\n", __func__);
                return -1;
            }
            else if(args->csiSurfaceFormat != NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_UV_420_I && args->csiSurfaceFormat != NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_Y_V_U_420 &&
                    args->csiSurfaceFormat != NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_YUYV_422_I && args->csiSurfaceFormat != NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_Y_V_U_422)
            {
                LOG_ERR("%s: deinterlacing is not a supported option for CSI capture surface formats other than yuyv_i and yv16x2\n", __func__);
                return -1;
            }
        }

        if(args->csiInputFormat == NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_RGB888 && args->outputType == NvMediaVideoOutputType_OverlayYUV) {
            LOG_ERR("%s: RGB -> YUV color space conversion is not supported for CSI\n", __func__);
            return -1;
        }

        if(args->csiDeinterlaceEnabled && !args->displayEnabled) {
            LOG_ERR("%s: deinterlacing is not a supported option with CSI display disabled\n", __func__);
            return -1;
        }

        if(args->externalBuffer) {
            switch(args->csiSurfaceFormat) {
                case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_Y_V_U_420:
                case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_420:
                case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_422:
                    LOG_ERR("%s: Surface(yv16, yv12, yv12x2)) doesn't support by NvMediaVideoSurface\n", __func__);
                    LOG_ERR("%s: Please use different capture surface\n", __func__);
                    return -1;
                default:
                    break;
            }
        }
    }

    if(!args->captureTime && !args->captureCount)
        args->captureCount = -1;
    else if(args->captureTime && args->captureCount)
        args->captureTime = 0;

    if(args->mixerWidth == 0 || args->mixerHeight == 0) {
        args->mixerWidth = args->inputWidth;
        args->mixerHeight = args->inputHeight;
    }

    if(args->aspectRatio == 0.0f)
        args->aspectRatio = args->inputWidth * 1.0 / args->inputHeight;

    if(!args->displayEnabled && !args->fileDumpEnabled)
        args->displayEnabled = NVMEDIA_TRUE;

    if(!args->isLiveMode) {
        if((args->captureType == CAPTURE_CSI && args->captureDeviceInUse == NationalSemi_DS90UR910Q) ||
           (args->captureType == CAPTURE_CSI && args->captureDeviceInUse == TI_DS90UH940) ||
           (args->captureType == CAPTURE_VIP && args->captureDeviceInUse != AnalogDevices_ADV7182)) {
            LOG_ERR("%s: m = cb is not a supported option for device NationalSemi_DS90UR910Q or AnalogDevices_ADV7182 or TI_DS90UH940\n", __func__);
            return -1;
        }

        if(args->captureType == CAPTURE_CSI && (args->captureDeviceInUse == Toshiba_TC358743 || args->captureDeviceInUse == Toshiba_TC358791) &&
           ((args->csiInputFormat == NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422 && !((args->inputWidth == 640 || args->inputWidth == 720) && args->inputHeight == 480)) ||
            (args->csiInputFormat == NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_RGB888 && !(args->inputWidth == 640 && args->inputHeight == 480))))
        {
            LOG_ERR("%s: %ux%u (%s) is not a supported resolution in test mode (m = cb) for ch = 5\n", __func__, args->inputWidth, args->inputHeight, args->csiInputFormat == NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422? "YUV422": "RGB");
            return -1;
        }
    }

    if(args->checkCRC && args->isLiveMode) {
        LOG_ERR("%s: Checking CRC checksum is not supported in live mode (m != cb)\n", __func__);
        return -1;
    }

    return 0;
}

void PrintOptions(TestArgs *args)
{
    char *captureDevices[] = {"adv7180", "adv7182", "adv7281", "adv7282", "ds90ur910q", "tc358743", "", "", "ds90uh940", "tc358791", "tc358791_cvbs", "adv7480", "adv7481", "adv7481h", "null"};
    char *csiPorts[] = {"a", "b", "ab", "cd", "e", "ef"};
    char *csiInputFormats[] = {"420", "422", "444", "rgb"};

        LOG_MSG("Capturing from CSI:\n");
        LOG_MSG("Device in use = %s\n", captureDevices[args->captureDeviceInUse]);
        LOG_MSG("Mode of operation = %s\n", args->isLiveMode? "live": "test");
        LOG_MSG("Port in use = %s\n", csiPorts[args->csiPortInUse]);
        LOG_MSG("# interface lanes = %u\n", args->csiInterfaceLaneCount);
        LOG_MSG("Input video standard = %s\n", (args->captureDeviceInUse == 2 || args->captureDeviceInUse == 3)? ((args->inputVideoStd == NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_VIP_NTSC)? "NTSC": "PAL"): "NA");
        LOG_MSG("Input format = %s%s\n", csiInputFormats[args->csiInputFormat], (args->csiInputFormat == NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_YUV422)? (args->csiCaptureInterlaced? "i": ""): "");
        LOG_MSG("Interlaced = %s\n", args->csiCaptureInterlaced? "Yes": "No");
        LOG_MSG("Input frame resolution = %ux%u\n", args->inputWidth, args->inputHeight);
        LOG_MSG("# Extra lines  = %u\n", args->csiExtraLines);
        LOG_MSG("Capture surface format: ");
        switch(args->csiSurfaceFormat) {
            case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_YUYV_422_I:
                LOG_MSG("yuyv_i\n");
                break;
            case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_Y_V_U_422:
                LOG_MSG("yv16x2\n");
                break;
            case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_422:
                LOG_MSG("yv16\n");
                break;
            case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_R8G8B8A8:
                LOG_MSG("rgb\n");
                break;
            default:
                LOG_MSG("\n");
                break;
        }
        LOG_MSG("Aspect ratio: %f\n", args->aspectRatio);
        LOG_MSG("Mixer resolution: %ux%u\n", args->mixerWidth, args->mixerHeight);
        LOG_MSG("Output type = %s\n",((args->outputType == NvMediaVideoOutputType_OverlayYUV) ? "oyuv" :
                                     ((args->outputType == NvMediaVideoOutputType_Overlay)? "o" : "orgb")));
        LOG_MSG("Display id = %d\n", args->displaysList.displayId);
        LOG_MSG("Deinterlace type = ");

        if(args->csiDeinterlaceEnabled) {
            switch(args->csiDeinterlaceType) {
                case 0:
                    LOG_MSG("weave\n");
                    break;
                case 1:
                    LOG_MSG("bob\n");
                    break;
                case 2:
                    if (args->csiDeinterlaceAlgo == 2)
                        LOG_MSG("advanced frame-rate, advance2 algorithm\n");
                    else
                        LOG_MSG("advanced frame-rate, advance1 algorithm\n");
                    break;
                case 3:
                    if (args->csiDeinterlaceAlgo == 2)
                        LOG_MSG("advanced field-rate, advance2 algorithm\n");
                    else
                        LOG_MSG("advanced field-rate, advance1 algorithm\n");
                    break;
            }
        }
        else
            LOG_MSG("off\n");

        LOG_MSG("Output file name = %s\n", args->fileDumpEnabled ? args->outputFileName: "off");
        LOG_MSG("# frames to be captured = %d\n", args->captureCount);
        LOG_MSG("Capture duration (in seconds) = %u\n", args->captureTime);
}
