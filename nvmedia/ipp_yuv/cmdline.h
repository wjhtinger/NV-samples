/* Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVMEDIA_CMD_LINE_H_
#define _NVMEDIA_CMD_LINE_H_

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

int
ParseArgs (
    int argc,
    char *argv[],
    TestArgs *testArgs);

void
PrintUsage (void);

int
ParseNextCommand (
    FILE *file,
    char  *inputLine,
    char **inputTokens,
    int   *tokensNum);

#ifdef __cplusplus
}
#endif

#endif
