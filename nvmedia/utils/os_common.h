/*
 * Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVMEDIA_OS_COMMON_H_
#define _NVMEDIA_OS_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include "nvcommon.h"
#include <errno.h>

#if NVOS_IS_INTEGRITY
#include <INTEGRITY.h>
#else
#include <malloc.h>
#endif


#if !defined(__GNUC__) || NVOS_IS_INTEGRITY
#define NVM_PREFETCH(ptr)  \
    __asm__ __volatile__( "pld [%[memloc]]     \n\t"  \
    : : [memloc] "r" (ptr) : "cc" );
#else
#define NVM_PREFETCH(ptr)  \
        __builtin_prefetch(ptr, 1, 1);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _NVMEDIA_OS_COMMON_H_ */

