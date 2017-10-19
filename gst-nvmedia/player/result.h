/*
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __RESULT_H__
#define __RESULT_H__

#include <stdio.h>

typedef enum {
    GST_NVM_RESULT_OK = 0,               // Success
    GST_NVM_RESULT_NOOP,                 // Success, but nothing was done
    GST_NVM_RESULT_FAIL = 0x80000000,    // General failure
    GST_NVM_RESULT_INVALID_POINTER,      // Invalid (e.g. null) pointer
    GST_NVM_RESULT_INVALID_ARGUMENT,     // Invalid argument
    GST_NVM_RESULT_OUT_OF_MEMORY,        // Out of memory
    GST_NVM_RESULT_FILE_NOT_FOUND,       // File was not found
    GST_NVM_RESULT_INVALID_FILE,         // File is invalid
    GST_NVM_RESULT_NOT_IMPLEMENTED,      // Unimplemented functionality
    GST_NVM_RESULT_FILE_NOT_OPENED,      // File was not opened
    GST_NVM_RESULT_MAX_HANDLES_EXCEEDED, // Too many handles already opened
    GST_NVM_RESULT_GST_NOT_INITIALIZED   // GST has not been initialized.
} GstNvmResult;

#endif
