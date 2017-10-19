/* Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __CONFIG_PARSER_H__
#define __CONFIG_PARSER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gmain.h>
#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "result.h"

#define GST_NVM_MAX_ITEMS_TO_PARSE  10000
#define GST_NVM_MAX_CONFIG_SECTIONS 20

typedef enum {
    GST_NVM_TYPE_UINT = 0,
    GST_NVM_TYPE_UINT_HEX,
    GST_NVM_TYPE_INT,
    GST_NVM_TYPE_DOUBLE,
    GST_NVM_TYPE_UCHAR,
    GST_NVM_TYPE_ULLONG,
    GST_NVM_TYPE_USHORT,
    GST_NVM_TYPE_CHAR_ARR,
    GST_NVM_TYPE_UCHAR_ARR
} GstNvmParamType;

typedef struct {
    gchar           *param_name;
    void            *mapped_location;
    GstNvmParamType  type;
    gdouble          default_value;
    guint            string_length; // string param size
} GstNvmConfigParamsMap;

GstNvmResult
gst_nvm_config_parser_init_params_map (
    GstNvmConfigParamsMap *params_map);

GstNvmResult
gst_nvm_config_parser_parse_file (
    GstNvmConfigParamsMap *params_map,
    gint size_of_section,
    gchar *file_name,
    gint *param_sets);

#ifdef __cplusplus
}
#endif

#endif