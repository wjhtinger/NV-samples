/* Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __CMD_LINE_H__
#define __CMD_LINE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <glib/gstdio.h>
#include "device-map.h"
#include "player-core.h"
#include "result.h"

typedef struct {
    gboolean              default_ctx_capture_flag;
    gboolean              default_ctx_usb_flag;
    gboolean              default_ctx_avb_src_flag;
    gboolean              default_ctx_avb_sink_flag;
    gchar                 capture_config_file[GST_NVM_MAX_STRING_SIZE];
    gchar                 usb_device[GST_NVM_MAX_STRING_SIZE];
    gboolean              multi_inst_flag;
    gboolean              cmd_line_script_flag;
    gboolean              cmd_line_rscript_flag;
    gchar                 script_name[GST_NVM_MAX_STRING_SIZE];
} GstNvmPlayerTestArgs;

GstNvmResult
gst_nvm_parse_next_command (
    FILE *file,
    gchar *input_line,
    gchar **input_tokens,
    gint *tokens_num);

GstNvmResult
gst_nvm_parse_command_line (
    gint argc,
    gchar *argv[],
    GstNvmPlayerTestArgs *test_args);

void
gst_nvm_print_options (void);

#ifdef __cplusplus
}
#endif

#endif
