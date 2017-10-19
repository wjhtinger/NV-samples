/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVMEDIA_PLUGIN_H_
#define _NVMEDIA_PLUGIN_H_

#include "nvmedia_ipp.h"
#include "nvcommon.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    NvMediaBool enable;
    float gains[4];
    float matrix[4][4];
    unsigned int threshholds[2];
    int points[4];
} PluginConfigDataAWB;

typedef struct {
    NvMediaBool enable;
} PluginConfigDataAE;

typedef struct {
    PluginConfigDataAE ae;
    PluginConfigDataAWB awb;
} PluginConfigData;

typedef struct {
    NvMediaIPPComponent *controlAlgorithmHandle;
    NvMediaIPPPluginSupportFuncs supportFunctions;
    NvMediaIPPPropertyStatic staticProperties;
    float awb_Adpatgain2shortexp[4];
    NvMediaIPPPluginOutput runningPluginOutput;
    PluginConfigData configs;
    NvMediaIPPPluginOutputStreamSettings streamSettings[NVMEDIA_IPP_STREAM_MAX_TYPES];
    NvMediaIPPISPVersion ispVersion;
} PluginContext;


NvMediaStatus
NvSampleACPParseConfiguration(
    NvMediaIPPPlugin *pluginHandle,
    const char *configurationText);

NvMediaStatus
NvSampleACPCreate(
    NvMediaIPPComponent *parentControlAlgorithmHandle,
    NvMediaIPPPluginSupportFuncs *pSupportFunctions,
    NvMediaIPPPropertyStatic *pStaticProperties,
    void *clientContext,
    NvMediaIPPPlugin **pluginHandle,
    NvMediaIPPISPVersion ispVersion);

void
NvSampleACPDestroy(
    NvMediaIPPPlugin *pluginHandle);

NvMediaStatus
NvSampleACPProcess(
    NvMediaIPPPlugin *pluginHandle,
    NvMediaIPPPluginInput *pluginInput,
    NvMediaIPPPluginOutput *pluginOutput);

#ifdef __cplusplus
}
#endif

#endif // __PLUGIN_H__
