/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __CMD_LINE_H__
#define __CMD_LINE_H__

#include "nvcommon.h"
#include "nvmedia.h"
#include "nvmedia_image.h"
#include "log_utils.h"
#include "usb_utils.h"
#include "misc_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_STRING_SIZE                 256
#define MAX_CONFIG_SECTIONS             128

typedef struct {
    NvMediaSurfaceType          outputSurfType;
    NvU32                       outputSurfAttributes;
    NvMediaImageAdvancedConfig  outputSurfAdvConfig;
    NvU32                       logLevel;

    NvU32                       width;
    NvU32                       height;
    char                        inpFmt[256];
    char                        surfFmt[256];
    char                        devPath[256];
    int                         windowOffset[2];
    NvU32                       displayId;
    NvMediaVideoOutputDevice    displayDevice;
    char                       *outFileName;
    NvMediaBool                 saveOutFile;
} TestArgs;

int
ParseArgs (
    int argc,
    char **argv,
    TestArgs *testArgs);

void
PrintUsage (void);

#ifdef __cplusplus
}
#endif

#endif
