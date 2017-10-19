/* Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "parser.h"

static CaptureConfigParams sCaptureSetsCollection[MAX_CONFIG_SECTIONS];
static NvMediaBool sConfigFileParsingDone = NVMEDIA_FALSE;

SectionMap sectionsMap[] = {
    {SECTION_CAPTURE,     "capture-params-set", 0, sizeof(CaptureConfigParams)},
    {SECTION_NONE,        "",                   0, 0} // Specifies end of array
};

ConfigParamsMap captureParamsMap[] = {
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
    {"i2c_device",            &sCaptureSetsCollection[0].i2cDevice,       TYPE_UINT,          0, LIMITS_NONE, 0, 0, 0,            0, SECTION_CAPTURE},
    {"max9271_address",      &sCaptureSetsCollection[0].brdcstSerAddr,  TYPE_UINT_HEX, 0x0, LIMITS_NONE, 0, 0, 0,    0, SECTION_CAPTURE},
    {"max9271_address_0",    &sCaptureSetsCollection[0].serAddr[0],  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"max9271_address_1",    &sCaptureSetsCollection[0].serAddr[1],  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"max9271_address_2",    &sCaptureSetsCollection[0].serAddr[2],  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"max9271_address_3",    &sCaptureSetsCollection[0].serAddr[3],  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"max9286_address",      &sCaptureSetsCollection[0].desAddr,     TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"serializer_address",      &sCaptureSetsCollection[0].brdcstSerAddr,  TYPE_UINT_HEX, 0x0, LIMITS_NONE, 0, 0, 0,    0, SECTION_CAPTURE},
    {"serializer_address_0",    &sCaptureSetsCollection[0].serAddr[0],  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"serializer_address_1",    &sCaptureSetsCollection[0].serAddr[1],  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"serializer_address_2",    &sCaptureSetsCollection[0].serAddr[2],  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"serializer_address_3",    &sCaptureSetsCollection[0].serAddr[3],  TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"deserializer_address",    &sCaptureSetsCollection[0].desAddr,     TYPE_UINT_HEX,   0x0, LIMITS_NONE, 0, 0, 0,          0, SECTION_CAPTURE},
    {"sensor_address",       &sCaptureSetsCollection[0].brdcstSensorAddr, TYPE_UINT_HEX,  0x30, LIMITS_NONE, 0, 0, 0,  0, SECTION_CAPTURE},
    {"sensor_address_0",     &sCaptureSetsCollection[0].sensorAddr[0],   TYPE_UINT_HEX,  0, LIMITS_NONE, 0, 0, 0,     0, SECTION_CAPTURE},
    {"sensor_address_1",     &sCaptureSetsCollection[0].sensorAddr[1],   TYPE_UINT_HEX,  0, LIMITS_NONE, 0, 0, 0,     0, SECTION_CAPTURE},
    {"sensor_address_2",     &sCaptureSetsCollection[0].sensorAddr[2],   TYPE_UINT_HEX,  0, LIMITS_NONE, 0, 0, 0,     0, SECTION_CAPTURE},
    {"sensor_address_3",     &sCaptureSetsCollection[0].sensorAddr[3],   TYPE_UINT_HEX,  0, LIMITS_NONE, 0, 0, 0,     0, SECTION_CAPTURE},
    {NULL} // Specifies the end of the array
};

NvMediaStatus
ParseConfigFile(char *configFile,
                NvU32 *captureConfigSetsNum,
                CaptureConfigParams **captureConfigCollection)
{
    NvU32 id = 0;

    if (!sConfigFileParsingDone) {
        memset(&sCaptureSetsCollection[0],
               0,
               sizeof(CaptureConfigParams) * MAX_CONFIG_SECTIONS);

        ConfigParser_InitParamsMap(captureParamsMap);
        if (IsFailed(ConfigParser_ParseFile(captureParamsMap,
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

