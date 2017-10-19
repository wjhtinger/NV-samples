/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _CMDLINE_H_
#define _CMDLINE_H_

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

int
ParseArgs(int argc, char *argv[], TestArgs *args);

void
PrintUsage(void);

void
PrintRuntimeUsage(void);

#ifdef __cplusplus
}
#endif

#endif // _CMDLINE_H_

