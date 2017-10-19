/* Copyright (c) 2013-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gmain.h>
#include <gst/gst.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cmdline.h"
#include "device-map.h"
#include "player-core.h"
#include "player-core-priv.h"
#include "eglconsumer.h"
#include "result.h"

#define GST_NVM_MAIN_LOOP_TIMEOUT       5
#define GST_NVM_MAX_CONTEXTS            10
#define GST_NVM_MAX_WIDTH               4096
#define GST_NVM_MAX_HEIGHT              4096
#define GST_NVM_MAX_SURFACE_TYPE_VALUE  3

typedef struct {
    GstNvmContextHandle   handle;   // handle to GstNvmediaPlayer
    GstNvmContextType     type;     // context type
    gboolean              is_used;
    gchar                 name[GST_NVM_MAX_STRING_SIZE];
} GstNvmPlayerInstCtx;

typedef struct {
    GMainLoop            *main_loop;
    GstNvmPlayerInstCtx   player_contexts[GST_NVM_MAX_CONTEXTS];
    guint                 controled_ctx;
    GstNvmDevicesMap      device_map;
    gboolean              default_ctx_capture_flag;
    gboolean              default_ctx_usb_flag;
    gboolean              default_ctx_avb_src_flag;
    gboolean              default_ctx_avb_sink_flag;
    gchar                *capture_config_file;
    gchar                *usb_device;
    gboolean              multi_inst_flag;
    gboolean              cmd_line_script_flag;
    gboolean              cmd_line_rscript_flag;
    gchar                *script_name;
} GstNvmPlayerAppCtx;

typedef struct {
    gint64  last_pts;
    gchar status[];
} GstNvmPlayerDbgStruct;

GstClockTime _priv_gst_info_start_time;

static  gint _player_process (GstNvmPlayerAppCtx *ctx, FILE *file);

/* Quit flag. Out of application context for sig handling */
static gboolean quit_flag = FALSE;

static GstNvmPlayerAppCtx *gCtx;

static void
_player_app_quit (
    GstNvmPlayerAppCtx *ctx)
{
    guint ui;
    GstNvmParameter param;

    memset (&param, 0, sizeof (GstNvmParameter));
    for (ui = 0; ui < GST_NVM_MAX_CONTEXTS; ui++) {
        if (ctx->player_contexts[ui].is_used) {
            gst_nvm_player_quit (ctx->player_contexts[ui].handle);
            ctx->player_contexts[ui].is_used = FALSE;
        }
    }

    if (g_main_loop_is_running (ctx->main_loop))
        g_main_loop_quit (ctx->main_loop);
}

static gint
_run_script (
    GstNvmPlayerAppCtx *ctx,
    gchar *file_name,
    gint recursive)
{
    FILE *file;
    int loop_count = 0;

    do {
        if (recursive) {
          loop_count++;
          g_print ("Loop Count: %d\n", loop_count);
        }

        file = g_fopen (file_name, "rb");
        if (!file) {
            g_printf ("Error opening script file %s\n", file_name);
            return -1;
        }
        while (!feof (file) && !quit_flag) {
            _player_process (ctx, file);
        }
        fclose (file);
    } while (recursive && !quit_flag);

    if (quit_flag)
        _player_app_quit (ctx);

    return 0;
}

static gint
_set_controled_context (
    GstNvmPlayerAppCtx *ctx,
    gint ctx_id)
{
    g_return_val_if_fail (ctx_id >= 0 && ctx_id < GST_NVM_MAX_CONTEXTS, 1);
    g_return_val_if_fail (ctx->player_contexts[ctx_id].is_used, 1);

    g_print ("Setting control to context %d\n", ctx_id);
    ctx->controled_ctx = ctx_id;

    return 0;
}

static gint
_get_next_free_ctx_id (
    GstNvmPlayerAppCtx *ctx)
{
    gint i;

    for (i = 0; i < GST_NVM_MAX_CONTEXTS; i++) {
        if (!ctx->player_contexts[i].is_used)
            return i;
    }

    return -1;
}

static gint
_start_player_context (
    GstNvmPlayerAppCtx *ctx,
    GstNvmContextType ctx_type,
    gchar *name,
    gchar *audio_dev)
{
    gint ctx_id = -1, disp_id = -1;
    GstNvmResult result = GST_NVM_RESULT_OK;
    ctx_id = _get_next_free_ctx_id (ctx);
    disp_id = gst_nvm_device_map_get_available_display_id (&ctx->device_map);
    strcpy (ctx->device_map.audio_dev_map->device.name, audio_dev);

    if (disp_id == -1 && !audio_dev) {
        g_printf ("Cannot create new ctx. All audio and video devices are being used\n");
        return -1;
    }

    if (ctx_id != -1) {
        if (!strcmp (audio_dev, "default"))
            g_print ("Starting context #%d\nname: %s\ntype: %s\nDisplay device: %s\naudio device: %s (from asound.conf)\n",
                      ctx_id, name ? name : "", ctx_type == GST_NVM_PLAYER ? "MEDIA PLAYER" :
                      (ctx_type == GST_NVM_USB_AUDIO ? "USB" :
                      (ctx_type == GST_NVM_AVB_SRC ? "AVB_SRC" :
                      (ctx_type == GST_NVM_AVB_SINK ? "AVB_SINK" : "CAPTURE"))),
                      disp_id >= 0 ? ctx->device_map.displays_map[disp_id].device.name : "NONE", audio_dev);
        else
           g_print ("Starting context #%d\nname: %s\ntype: %s\nDisplay device: %s\naudio device: %s\n",
                      ctx_id, name ? name : "", ctx_type == GST_NVM_PLAYER ? "MEDIA PLAYER" :
                      (ctx_type == GST_NVM_USB_AUDIO ? "USB" :
                      (ctx_type == GST_NVM_AVB_SRC ? "AVB_SRC" :
                      (ctx_type == GST_NVM_AVB_SINK ? "AVB_SINK" : "CAPTURE"))),
                      disp_id >= 0 ? ctx->device_map.displays_map[disp_id].device.name : "NONE", audio_dev);

        result = gst_nvm_player_open_handle (&ctx->player_contexts[ctx_id].handle,
                                             ctx_type,
                                             disp_id >= 0 ? &ctx->device_map.displays_map[disp_id].device : NULL,
                                             ctx->device_map.audio_dev_map->device.name,
                                             ctx->capture_config_file,
                                             ctx->usb_device);
        if (result != GST_NVM_RESULT_OK) {
            g_print ("gst_nvm_player_open_handle failed\n");
            return -1;
        }

        ctx->player_contexts[ctx_id].is_used = TRUE;
        if (name) {
            strncpy (ctx->player_contexts[ctx_id].name, name, GST_NVM_MAX_STRING_SIZE);
            ctx->player_contexts[ctx_id].name[GST_NVM_MAX_STRING_SIZE - 1] = '\0';
        }

        if (disp_id != -1)
            ctx->device_map.displays_map[disp_id].used_by_ctx = ctx_id;

        ctx->player_contexts[ctx_id].type = ctx_type;
        _set_controled_context (ctx, ctx_id);
    } else
         g_printf ("Max number of instances is already running: %d\n",
         GST_NVM_MAX_CONTEXTS);

    return 0;
}

static void
_describe_current_context (
        GstNvmPlayerAppCtx *ctx)
{
    gint disp_id[GST_NVM_MAX_DISPLAYS_PER_CTX];
    gint  i, used_displays_num = 0;

    for (i = 0; i < GST_NVM_MAX_DISPLAYS_PER_CTX; i++)
        disp_id[i] = -1;

    used_displays_num = gst_nvm_device_map_get_display_used_by_ctx (&ctx->device_map,
                                                                    ctx->controled_ctx,
                                                                    disp_id);

    g_printf ("Current Context Description:\n\tID: %d\n\tName: %s\n\tType: %s\n\tUsed Audio Channel: %s\n",
              ctx->controled_ctx,
              ctx->player_contexts[ctx->controled_ctx].name,
              ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_PLAYER ? "Media Player" :
              (ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_CAPTURE ? "Capture" :
              (ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_AVB_SRC ? "AVB_SRC" :
              (ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_AVB_SINK ? "AVB_SINK" : "USB"))),
              ctx->device_map.audio_dev_map->device.name);

    g_printf ("\tUsed Display Devices: ");
    for (i = 0; i < used_displays_num; i++) {
        if (disp_id[i] != -1)
            g_printf ("\n\t\t%s (%s)", ctx->device_map.displays_map[disp_id[i]].device.name, ctx->device_map.displays_map[disp_id[i]].device.description);
    }
    g_printf ("\n");
}

static void
_list_active_contexts (
        GstNvmPlayerAppCtx *ctx)
{
    gint disp_id[GST_NVM_MAX_DISPLAYS_PER_CTX];
    gint used_displays_num, i, j;

    for (i = 0; i < GST_NVM_MAX_DISPLAYS_PER_CTX; i++)
        disp_id[i] = -1;

    g_printf ("Active Contexts:\n\n");
    for (i = 0; i < 10; i++) {
        if (ctx->player_contexts[i].is_used) {
            used_displays_num = gst_nvm_device_map_get_display_used_by_ctx (&ctx->device_map,
                                                                            i,
                                                                            disp_id);
            g_printf ("Context ID: %d\n\tName: %s\n\tType: %s\n\tUsed Audio Channel: %s\n",
                      i,
                      ctx->player_contexts[i].name,
                      ctx->player_contexts[i].type == GST_NVM_PLAYER ? "Media Player" :
                      (ctx->player_contexts[i].type == GST_NVM_CAPTURE ? "Capture" :
                      (ctx->player_contexts[i].type == GST_NVM_AVB_SRC ? "AVB_SRC" :
                      (ctx->player_contexts[i].type == GST_NVM_AVB_SINK ? "AVB_SINK" :"USB"))),
                      ctx->device_map.audio_dev_map->device.name);
            g_printf ("\tUsed Display Devices: ");
            for (j = 0; j < used_displays_num; j++) {
                if (disp_id[j] != -1)
                    g_printf ("\n\t\t%s (%s)", ctx->device_map.displays_map[disp_id[j]].device.name, ctx->device_map.displays_map[disp_id[j]].device.description);
            }
            g_printf ("\n");
        }
    }
}

static void
_set_display_device (
    GstNvmPlayerAppCtx *ctx,
    gchar *display_device)
{
    gint i;
    GstNvmParameter param;
    gint new_ids[GST_NVM_MAX_DISPLAYS_PER_CTX];

    for (i = 0; i < GST_NVM_MAX_DISPLAYS_PER_CTX; i++)
        new_ids[i] = -1;

    if (gst_nvm_device_map_set_display_device_to_ctx (&ctx->device_map,
                                                      display_device,
                                                      ctx->controled_ctx,
                                                      new_ids)) {
        g_printf ("Failed setting display device");
        return;
    }

    strncpy (param.display_desc_value.display_type, display_device, GST_NVM_MAX_STRING_SIZE);
    param.display_desc_value.window_id = ctx->device_map.displays_map[new_ids[0]].device.window_id;
    gst_nvm_player_send_command (ctx->player_contexts[ctx->controled_ctx].handle,
                                 GST_NVM_CMD_SET_VIDEO_DEVICE,
                                 param);
}

static void
_set_display_device_dynamic (
    GstNvmPlayerAppCtx *ctx,
    gchar *display_device)
{
    gint i;
    gint new_ids[GST_NVM_MAX_DISPLAYS_PER_CTX];

    for (i = 0; i < GST_NVM_MAX_DISPLAYS_PER_CTX; i++)
        new_ids[i] = -1;

    if (gst_nvm_device_map_set_display_device_to_ctx (&ctx->device_map,
                                                      display_device,
                                                      ctx->controled_ctx,
                                                      new_ids)) {
        g_printf ("Failed setting display device");
        return;
    }

    gst_nvm_player_set_display_dynamic (ctx->player_contexts[ctx->controled_ctx].handle,
                                        display_device,
                                        ctx->device_map.displays_map[new_ids[0]].device.window_id);
}

static void
_set_audio_channel (
    GstNvmPlayerAppCtx *ctx,
    gchar *audio_channel)
{
    GstNvmParameter param;

    strcpy (param.string_value, audio_channel);
    if (ctx->default_ctx_usb_flag)
        gst_nvm_player_send_command (ctx->player_contexts[ctx->controled_ctx].handle,
                                 GST_NVM_CMD_SET_AUDIO_DEVICE_USB,
                                 param);
    else
        gst_nvm_player_send_command (ctx->player_contexts[ctx->controled_ctx].handle,
                                 GST_NVM_CMD_SET_AUDIO_DEVICE,
                                 param);
}

static void
_set_audio_channel_dynamic (
    GstNvmPlayerAppCtx *ctx,
    gchar *audio_channel)
{
    gst_nvm_player_set_audio_channel_dynamic (ctx->player_contexts[ctx->controled_ctx].handle,
                                              audio_channel);
}

static void
_check_usb_specific_command (
    GstNvmPlayerAppCtx *ctx,
    gchar *command,
    gchar *param_str)
{
    GstNvmPlayerInstCtx player = ctx->player_contexts[ctx->controled_ctx];
    GstNvmContextHandle handle = player.handle;
    GstNvmParameter param;

    memset (&param, 0, sizeof (GstNvmParameter));

    if (!strcmp (command, "da") && param_str) {
        GstNvmContext *context = (GstNvmContext *) (handle);
        context->is_sink_file = TRUE;
        strcpy (context->file_name, param_str);
        context->play_video = FALSE;
    } else if (!strcmp (command, "da")) {
        GstNvmContext *context = (GstNvmContext *) (handle);
        context->is_sink_file = FALSE;
        context->play_video = FALSE;
    } else if (!strcmp (command, "pu") && param_str) {
        GstNvmContext *context = (GstNvmContext *) (handle);
        context->play_video = FALSE;
        strcpy (param.string_value, param_str);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_PLAY, param);
    } else if (!strcmp (command, "ad") && param_str) {
         _set_audio_channel (ctx, param_str);
    } else if (!strcmp (command, "ku")) {
        gst_nvm_player_send_command (handle, GST_NVM_CMD_STOP, param);
    } else if (!strcmp (command, "s") && param_str) {
        strcpy (param.string_value, param_str);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_USB_SWITCH, param);
    } else {
        g_print ("Invalid Command for usb context,\
        type \"h\" for help on usage \n");
    }
}

static void
_check_avb_src_specific_command (
    GstNvmPlayerAppCtx *ctx,
    gchar *command,
    gchar *input_param[])
{
    GstNvmPlayerInstCtx player = ctx->player_contexts[ctx->controled_ctx];
    GstNvmContextHandle handle = player.handle;
    GstNvmParameter param;

    memset (&param, 0, sizeof (GstNvmParameter));

    if (!strcmp (command, "pt") && input_param[1]) {
        strncpy (param.string_value, input_param[1], GST_NVM_MAX_STRING_SIZE);
        if ((strcmp (input_param[1], "eglstream")) == 0) {
            if(input_param[2] && input_param[3] && \
               input_param[4] && input_param[5] && input_param[6]) {
                param.avb_params.stream_width = atoi(input_param[2]);
                if (param.avb_params.stream_width > GST_NVM_MAX_WIDTH) {
                    g_print ("Invalid width: %d. Max supported width is %d.\n", \
                              param.avb_params.stream_width, GST_NVM_MAX_WIDTH);
                    return;
                }
                param.avb_params.stream_height = atoi(input_param[3]);
                if (param.avb_params.stream_height > GST_NVM_MAX_HEIGHT) {
                    g_print ("Invalid height: %d. Max supported height is %d.\n", \
                              param.avb_params.stream_height, GST_NVM_MAX_HEIGHT);
                    return;
                }
                strncpy (param.avb_params.socket_path, input_param[4], GST_NVM_MAX_STRING_SIZE);
                param.avb_params.surface_type = atoi(input_param[5]);
                if(param.avb_params.surface_type > GST_NVM_MAX_SURFACE_TYPE_VALUE) {
                    g_print ("Invalid surface type selection. type \"h\" for help on usage \n");
                    return;
                }
                if ((strcmp (input_param[6], "fifo")) == 0)
                    param.avb_params.fifo_mode = TRUE;
                else
                    // mailbox mode
                    param.avb_params.fifo_mode = FALSE;
                if(input_param[7]) {
                    if ((strcmp (input_param[7], "low-latency-enable")) == 0)
                        param.avb_params.low_latency = TRUE;
                    else
                        param.avb_params.low_latency = FALSE;
                }
                else {
                    //default: low-latency is disabled
                    param.avb_params.low_latency = FALSE;
                }
            }
            else {
                g_print ("Invalid option with pt eglstream command,\
                type \"h\" for help on usage \n");
                return;
            }
        }
        gst_nvm_player_send_command (handle, GST_NVM_CMD_PLAY, param);
    } else if (!strcmp (command, "ad") && input_param[1]) {
         _set_audio_channel (ctx, input_param[1]);
    }
    else if (!strcmp (command, "st") && input_param[1]) {
        strcpy (param.string_value, input_param[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_ETHERNET_INTERFACE, param);
    } else if (!strcmp (command, "priot") && input_param[1]) {
        param.int_value = atoi(input_param[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_VLAN_PRIORITY, param);
    } else if (!strcmp (command, "sst") && input_param[1]) {
        param.int_value = atoi(input_param[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_STREAM_ID, param);
    } else if (!strcmp (command, "kt")) {
        gst_nvm_player_send_command (handle, GST_NVM_CMD_STOP, param);
    } else {
        g_print ("Invalid Command for usb context,\
        type \"h\" for help on usage \n");
    }
}

static void
_check_avb_sink_specific_command (
    GstNvmPlayerAppCtx *ctx,
    gchar *command,
    gchar *input_param[])
{
    GstNvmPlayerInstCtx player = ctx->player_contexts[ctx->controled_ctx];
    GstNvmContextHandle handle = player.handle;
    GstNvmParameter param;

    memset (&param, 0, sizeof (GstNvmParameter));

    if (!strcmp (command, "pl") && input_param[1]) {
        GstNvmContext *context = (GstNvmContext *) (handle);
        strcpy (param.string_value, input_param[1]);
        context->play_video = TRUE;
        if(input_param[2]) {
            if ((strcmp (input_param[2], "low-latency-enable")) == 0)
                param.avb_params.low_latency = TRUE;
            else
                param.avb_params.low_latency = FALSE;
        }
        else {
           //default: low-latency is disabled
           param.avb_params.low_latency = FALSE;
        }
        gst_nvm_player_send_command (handle, GST_NVM_CMD_PLAY, param);
    } else if (!strcmp (command, "sl") && input_param[1]) {
        strcpy (param.string_value, input_param[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_ETHERNET_INTERFACE, param);
    } else if (!strcmp (command, "ssl") && input_param[1]) {
        strcpy (param.string_value, input_param[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_STREAM_ID, param);
    } else if (!strcmp (command, "kl")) {
        gst_nvm_player_send_command (handle, GST_NVM_CMD_STOP, param);
    } else {
        g_print ("Invalid Command for usb context,\
        type \"h\" for help on usage \n");
    }
}

static void
_check_capture_specific_command (
    GstNvmPlayerAppCtx *ctx,
    gchar *command, gchar *param_str)
{
    GstNvmParameter param;

    memset (&param, 0, sizeof (GstNvmParameter));

    if (!strcmp (command, "sc")) {
        if (param_str)
            strcpy (param.string_value, param_str);
        gst_nvm_player_send_command (
            ctx->player_contexts[ctx->controled_ctx].handle,
            GST_NVM_CMD_PLAY, param);
    } else if (!strcmp (command, "kc")) {
        gst_nvm_player_send_command (
            ctx->player_contexts[ctx->controled_ctx].handle,
            GST_NVM_CMD_STOP, param);
    } else if (!strcmp (command, "scm")) {
        if (param_str && (!strcasecmp (param_str, "live") || !strcasecmp (param_str, "cb"))) {
            strcpy (param.string_value, param_str);
            gst_nvm_player_send_command (
                ctx->player_contexts[ctx->controled_ctx].handle,
                GST_NVM_CMD_CAPTURE_MODE, param);
        } else {
            g_print ("cm command must be followed by valid capture mode [live/cb]\n");
        }
     } else if (!strcmp (command, "scrc")) {
        if (param_str) {
            strcpy (param.string_value, param_str);
            gst_nvm_player_send_command (
                ctx->player_contexts[ctx->controled_ctx].handle,
                GST_NVM_CMD_CAPTURE_CRC_FILE, param);
        } else {
            g_print ("scrc command must be followed by crc file location or NULL to stop crc checks\n");
        }
    } else if (!strcmp (command, "lps")) {
        gst_nvm_player_list_capture_param_sets (
            ctx->player_contexts[ctx->controled_ctx].handle);
    } else {
        g_print ("Invalid Command for capture context, type \"h\" for help on usage \n");
    }
}

static void
_check_media_player_specific_command (
    GstNvmPlayerAppCtx *ctx,
    gchar *command,
    gchar *param_str)
{
    GstNvmPlayerInstCtx player = ctx->player_contexts[ctx->controled_ctx];
    GstNvmContextHandle handle = player.handle;
    GstNvmParameter param;

    memset (&param, 0, sizeof (GstNvmParameter));

    if (!strcmp (command, "pv") && param_str) {
        gCtx = ctx;
        strcpy (param.string_value, param_str);
        GstNvmContext *context = (GstNvmContext *) (handle);
        context->play_video = TRUE;
        gst_nvm_player_send_command (handle, GST_NVM_CMD_PLAY, param);
    } else if (!strcmp (command, "pa") && param_str) {
        strcpy (param.string_value, param_str);
        GstNvmContext *context = (GstNvmContext *) (handle);
        context->play_video = FALSE;
        gst_nvm_player_send_command (handle, GST_NVM_CMD_PLAY, param);
    } else if (!strcmp (command, "pp")) {
        gst_nvm_player_send_command (handle, GST_NVM_CMD_PAUSE_RESUME, param);
    } else if (!strcmp (command, "ss") && param_str) {
        if (strchr(param_str, '.') != NULL) {
            g_printf ("Decimal numbers are not allowed for set speed command. Please use integer number between -32 and 32.\n");
            return;
        }
        param.int_value = atoi (param_str);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_SPEED, param);
    } else if (!strcmp (command, "po") && param_str) {
        param.int_value = atoi (param_str);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_POSITION, param);
    } else if (!strcmp (command, "fpsdisp") && param_str) {
        GstNvmContext *context = (GstNvmContext *) (handle);
        if (!strcmp (param_str, "0"))
            context->is_fps_profiling = FALSE;
        else if (!strcmp (param_str, "1"))
            context->is_fps_profiling = TRUE;
    } else if (!strcmp (command, "we")) {
        gst_nvm_player_send_command (handle, GST_NVM_CMD_WAIT_END, param);
        if (!ctx->multi_inst_flag) {
            GstNvmContext *context = (GstNvmContext *) (handle);
            gst_nvm_semaphore_wait (context->wait_end_sem);
        }
    } else if (!strcmp (command, "ad") && param_str) {
         _set_audio_channel (ctx, param_str);
    } else if (!strcmp (command, "dad") && param_str) { // Dynamic Audio Device Change
       _set_audio_channel_dynamic (ctx, param_str);
    } else if (!strcmp (command, "minfo")) {
            g_printf ("Media info for player %d:\n", ctx->controled_ctx);
            gst_nvm_player_print_stream_info (handle);
    } else if (!strcmp (command, "kv")) {
        gst_nvm_player_send_command (handle, GST_NVM_CMD_STOP, param);
        gCtx = NULL;
    } else if (!strcmp (command, "ka")) {
        gst_nvm_player_send_command (handle, GST_NVM_CMD_STOP, param);
    } else if (!strcmp (command, "pm") && param_str) {
        param.int_value = atoi (param_str);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SPEW_PTM_VALUES, param);
    } else if (!strcmp (command, "use-decodebin") && param_str) {
        param.int_value = atoi (param_str);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_USE_DECODEBIN, param);
    } else if (!strcmp (command, "aspect") ||
            !strcmp (command, "sc") ||
            !strcmp (command, "qos") ||
            !strcmp (command, "kc") ||
            !strcmp (command, "sps") ||
            !strcmp (command, "rt") ||
            !strcmp (command, "dv") ||
            !strcmp (command, "ddv") ||
            !strcmp (command, "da") ||
            !strcmp (command, "dav") ||
            !strcmp (command, "evs") ||
            !strcmp (command, "pu") ||
            !strcmp (command, "ku") ||
            !strcmp (command, "s")) {
    g_print ("Not supported\n");
    } else {
        g_print ("Invalid Command for media player context,\
        type \"h\" for help on usage \n");
    }
}

static void
gst_nvm_print_bin_info(GstBin* bin)
{
    GstIterator* iterator;
    GValue eVal = G_VALUE_INIT;
    GstElement* element;
    GstNvmPlayerDbgStruct* dbg_data;

    iterator = gst_bin_iterate_elements(bin);

    if(iterator)
    {
        while(GST_ITERATOR_OK == gst_iterator_next(iterator, &eVal))
        {
            GParamSpec  **property_specs;
            guint       num_properties,i;

            element = g_value_get_object(&eVal);
            g_value_reset(&eVal);
            property_specs = g_object_class_list_properties(
                              G_OBJECT_GET_CLASS(element),
                              &num_properties );
            for (i = 0; i < num_properties; i++)
            {
                GParamSpec  *param = property_specs[i];
                if(strcmp(param->name, "debug-data") == 0)
                {
                    printf("element is %p and element_name is %s\n", element, element->object.name);
                    g_object_get(element, "debug-data", &dbg_data, NULL);
                    printf("Current status of element %s : %s\n", element->object.name, dbg_data->status);
                    printf("Last pts value : %lld \n\n", (long long int)dbg_data->last_pts);
                }
            }
            if(GST_IS_BIN(element))
                gst_nvm_print_bin_info((GstBin*)element);
        }
    }
}

static void
gst_nvm_get_backtrace (void)
{
    GstNvmPlayerInstCtx player;
    GstNvmContextHandle handle;
    GstNvmContext *context = NULL;

    if(gCtx)
    {
        player = gCtx->player_contexts[gCtx->controled_ctx];
        handle = player.handle;
        context = (GstNvmContext *) (handle);
        gst_nvm_print_bin_info((GstBin*)context->pipeline);
    }
}

static gint
_player_process (
    GstNvmPlayerAppCtx *ctx,
    FILE *file)
{
    GstNvmPlayerInstCtx ctrl_ctx = ctx->player_contexts[ctx->controled_ctx];
    GstNvmContextHandle handle = ctrl_ctx.handle;
    gchar input_line[GST_NVM_MAX_STRING_SIZE] = {0};
    gchar command[GST_NVM_MAX_STRING_SIZE] = {0};
    gchar param_str[GST_NVM_MAX_STRING_SIZE] = {0};
    gchar *input_tokens[GST_NVM_MAX_STRING_SIZE] = {0};
    gint i, tokens_num = 0;
    GstNvmParameter param;
    guint chosen_context;
    gint display_used[GST_NVM_MAX_DISPLAYS_PER_CTX];
    gint str_pointer;
    gint used_displays_num;
    const gchar *pos_string;
    regex_t regex;
    regmatch_t match;
    gchar position_field[5];
    gint position[4];
    gint depth;

    gst_nvm_parse_next_command (file, input_line, input_tokens, &tokens_num);
    if (input_line[0] == 0 || !tokens_num)
        return 0;

    memset (&param, 0, sizeof (GstNvmParameter));
    strcpy(command, input_tokens[0]);
    if (input_tokens[1])
        strcpy(param_str, input_tokens[1]);

    if (command[0] == '[') {
        goto freetokens;
    } else if (!strcmp (command, "h")) {
        gst_nvm_print_options ();
    } else if (!strcmp (command, "rs") && input_tokens[1]) {
        _run_script (ctx, input_tokens[1], 0);
    } else if (!strcmp (command, "cctx")) {
        if (ctx->multi_inst_flag) {
            if (input_tokens[1] && !strcmp (input_tokens[1], "cap") && input_tokens[2])
                _start_player_context (ctx, GST_NVM_CAPTURE, input_tokens[1], input_tokens[2]);
            else if (input_tokens[1] && !strcmp (input_tokens[1], "usb") && input_tokens[2]) {
                if (input_tokens[3])
                    strcpy (ctx->usb_device, input_tokens[3]);
                else
                    strcpy (ctx->usb_device, "Android");
                _start_player_context (ctx, GST_NVM_USB_AUDIO, input_tokens[1], input_tokens[2]);
            }else if(input_tokens[1] && !strcmp (input_tokens[1], "avb_src")){
                _start_player_context (ctx, GST_NVM_AVB_SRC, input_tokens[1], input_tokens[2]);
            }else if(input_tokens[1] && !strcmp (input_tokens[1], "avb_sink")){
                _start_player_context (ctx, GST_NVM_AVB_SINK, input_tokens[1], input_tokens[2]);
            }else if ((input_tokens[1] &&
                      !strcmp (input_tokens[1], "play") && input_tokens[2])) {
                _start_player_context (ctx, GST_NVM_PLAYER, input_tokens[1], input_tokens[2]);
            } else if (input_tokens[1] && !input_tokens[2]) {
                _start_player_context (ctx, GST_NVM_PLAYER, "play", input_tokens[1]);
            } else
                g_printf ("Failed creating new context. Only 'cap', 'play' and 'usb' instances can be created\n");
        }
        else
            g_printf ("Failed creating new context. --enable-multi-inst option not chosen\n");
    } else if (!strcmp (command, "sctx")  && input_tokens[1]) {
        if (_set_controled_context (ctx, atoi (input_tokens[1])))
            g_printf ("Failed setting control to context %d. 0-%d instances are available\n",
                       ctx->controled_ctx, GST_NVM_MAX_CONTEXTS - 1);
    } else if (!strcmp (command, "pinfo")) {
            g_printf ("Plugin info for player %d:\n", ctx->controled_ctx);
            gst_nvm_player_print_plugin_info (handle);
    } else if (!strcmp (command, "xx") && input_tokens[1]) {
        param.int_value = atoi (input_tokens[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SPEW_MSGS, param);
    } else if (!strcmp (command, "pr")) {
        str_pointer = 0;
        memset (param.string_value, ' ', GST_NVM_MAX_STRING_SIZE);
        for (i = 1; i < tokens_num; i++) {
            strcpy (&param.string_value[str_pointer], input_tokens[i]);
            str_pointer += strlen (input_tokens[i]) + 1;
            param.string_value[str_pointer - 1] = ' ';
        }
        gst_nvm_player_send_command (handle, GST_NVM_CMD_ECHO_MSG, param);
    } else if (!strcmp (command, "ldd")) {
        gst_nvm_device_map_list_display_devices (&ctx->device_map);
    } else if ((!strcmp (command, "dt") || !strcmp (command, "dd")) && input_tokens[1]) {
        _set_display_device (ctx, input_tokens[1]);
    } else if ((!strcmp (command, "ddt") || !strcmp (command, "ddd")) && input_tokens[1]) { // Dynamic Display Change
        _set_display_device_dynamic (ctx, input_tokens[1]);
    } else if (!strcmp (command, "wi")) {
        param.int_value = atoi (input_tokens[1]);
        if (param.int_value >= 0 && param.int_value < GST_NVM_DISPLAY_WINDOWS) {
            if (!gst_nvm_device_map_update_w_window_id_change (&ctx->device_map, param.int_value, ctx->controled_ctx))
                gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_DISPLAY_WINDOW_ID, param);
            else
                g_printf ("Failed changing window id\n");
        }
        else
            g_printf ("%d is not a valid display window id value\n", param.int_value);
    } else if (!strcmp (command, "dwi")) {
        int window_id = atoi (input_tokens[1]);
        if (window_id >= 0 && window_id < GST_NVM_DISPLAY_WINDOWS) {
            if (!gst_nvm_device_map_update_w_window_id_change (&ctx->device_map, window_id, ctx->controled_ctx))
                gst_nvm_player_set_display_window_id(handle, window_id);
            else
                g_printf ("Failed changing window id\n");
        }
        else
            g_printf ("%d is not a valid display window id value\n", window_id);
    } else if (!strcmp (command, "wd")) {
        param.int_value = atoi (input_tokens[1]);
        if (param.int_value >= 0 && param.int_value < 255) {
            gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_DISPLAY_WINDOW_DEPTH, param);
        }
        else
            g_printf ("%d is not a valid display window depth value\n", param.int_value);
    } else if (!strcmp (command, "dwd")) {
        depth = atoi (input_tokens[1]);
        if (depth >= 0 && depth <= 255)
            gst_nvm_player_set_display_window_depth (handle, depth);
        else
            g_printf ("%d is not a valid display window depth value\n", depth);
    } else if (!strcmp (command, "wp")) {
        strncpy (param.string_value, input_tokens[1], GST_NVM_MAX_STRING_SIZE);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_DISPLAY_WINDOW_POSITION, param);
    } else if (!strcmp (command, "dwp")) {
        regcomp (&regex, "^[0-9]{1,5}:[0-9]{1,5}:[0-9]{1,5}:[0-9]{1,5}", REG_EXTENDED);
        if (regexec (&regex, input_tokens[1], 0, &match, 0) == 0) {
            regcomp (&regex, "^[0-9]*", REG_EXTENDED);
            i = 0;
            pos_string = input_tokens[1];
            // position structure: x0, y0, width, height
            while (regexec (&regex, pos_string, 1, &match, 0) == 0 && (i < 4))
            {
                memset (&position_field[0], '\0', 5);
                memcpy (&position_field[0], &pos_string[match.rm_so], (gint)(match.rm_eo - match.rm_so) * sizeof (gchar));
                position[i] = 0;
                position[i++] = atoi (position_field);
                pos_string += match.rm_eo + 1; // +1 for ':'
            }
            gst_nvm_player_set_display_window_position (handle, position[0], position[1], position[2], position[3]);
        } else
            printf ("Position %s is not valid. Valid position format: x0:x1:y1:y2 every field should be up to 5 digits long\n", input_tokens[1]);
    } else if (!strcmp (command, "dcc")) {
        _describe_current_context (ctx);
    } else if (!strcmp (command, "lac")) {
        _list_active_contexts (ctx);
    } else if (!strcmp (command, "dmx")) {
        param.int_value = atoi (input_tokens[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_DOWNMIX_AUDIO, param);
    } else if (!strcmp (command, "brt") && input_tokens[1]) {
        param.float_value = atof (input_tokens[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_BRIGHTNESS, param);
    } else if (!strcmp (command,"cont") && input_tokens[1]) {
        param.float_value = atof (input_tokens[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_CONTRAST, param);
    } else if (!strcmp (command,"sat") && input_tokens[1]) {
        param.float_value = atof (input_tokens[1]);
        gst_nvm_player_send_command (handle, GST_NVM_CMD_SET_SATURATION, param);
    } else if (!strcmp (command, "wt")) {
        param.float_value = atof (param_str);
        if (!ctx->multi_inst_flag)
            g_usleep (param.float_value * G_USEC_PER_SEC);
        else
            gst_nvm_player_send_command (handle, GST_NVM_CMD_WAIT_TIME, param);
    } else if (!strcmp (command, "q") || !strcmp (command, "quit")) {
        quit_flag = TRUE;
    } else if (!strcmp (command, "kill") && input_tokens[1]) {
        chosen_context = atoi (input_tokens[1]);
        if (chosen_context >= 0 &&
            chosen_context <= GST_NVM_MAX_CONTEXTS &&
            ctx->controled_ctx != chosen_context) {
            gst_nvm_player_quit (ctx->player_contexts[chosen_context].handle);
            ctx->player_contexts[chosen_context].is_used = FALSE;
            // Release display device
            used_displays_num = gst_nvm_device_map_get_display_used_by_ctx (&ctx->device_map,
                                                                            chosen_context,
                                                                            display_used);
            for (i = 0; i < used_displays_num; i++) {
                if (display_used[i] != -1)
                    ctx->device_map.displays_map[display_used[i]].used_by_ctx = GST_NVM_DEVICE_NOT_USED;
            }

        }
        else
            g_print ("Invalid Context number %d\n", chosen_context);
    } else if (!strcmp (command, "enable-eglsink")) {
#ifdef NV_EGL_SINK
        GstNvmContext *context = (GstNvmContext *) (handle);
        if (input_tokens[1] && !strcmp(input_tokens[1],"CUDA_YUV")) {
          context->egl_consumer_type = GST_NVM_EGL_CONSUMER_CUDA_YUV;
          gst_nvm_player_start_egl_thread (handle);
        }
        else if (input_tokens[1] && !strcmp(input_tokens[1],"CUDA_RGB")) {
          context->egl_consumer_type = GST_NVM_EGL_CONSUMER_CUDA_RGB;
          gst_nvm_player_start_egl_thread (handle);
        }
        else if (input_tokens[1] && !strcmp(input_tokens[1],"GL_YUV")) {
          context->egl_consumer_type = GST_NVM_EGL_CONSUMER_GL_YUV;
          gst_nvm_player_start_egl_thread (handle);
        }
        else if (input_tokens[1] && !strcmp(input_tokens[1],"GL_RGB")) {
          context->egl_consumer_type = GST_NVM_EGL_CONSUMER_GL_RGB;
          gst_nvm_player_start_egl_thread (handle);
        }
        else if (input_tokens[1] && !strcmp(input_tokens[1],"CROSS_PROCESS_GL_RGB")) {
          context->egl_consumer_type = GST_NVM_EGL_CONSUMER_GL_RGB;
          gst_nvm_player_init_egl_cross_process (handle);
        }
        else if (input_tokens[1] && !strcmp(input_tokens[1],"CROSS_PROCESS_GL_YUV")) {
          context->egl_consumer_type = GST_NVM_EGL_CONSUMER_GL_YUV;
          gst_nvm_player_init_egl_cross_process (handle);
        }
        else
#endif
          g_print ("Invalid consumer type selected\n");
    } else if (!strcmp (command, "disable-eglsink")) {
#ifdef NV_EGL_SINK
        gst_nvm_player_stop_egl_thread (handle);
#endif
    } else if (ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_PLAYER) {
        _check_media_player_specific_command (ctx,
                                              command,
                                              input_tokens[1]);
    } else if (ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_CAPTURE) {
        _check_capture_specific_command (ctx,
                                         command,
                                         input_tokens[1]);
    } else if (ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_USB_AUDIO) {
        _check_usb_specific_command (ctx,
                                     command,
                                     input_tokens[1]);
    }
    else if (ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_AVB_SRC) {
        _check_avb_src_specific_command (ctx,
                                         command,
                                         input_tokens);
    }
    else if (ctx->player_contexts[ctx->controled_ctx].type == GST_NVM_AVB_SINK) {
        _check_avb_sink_specific_command (ctx,
                                          command,
                                          input_tokens);
    }
    else {
        g_print ("Invalid Command, type \"h\" for help on usage \n");
    }

freetokens:
    for (i = 0; i < tokens_num; i++)
        g_free (input_tokens[i]);

    return 0;
}

static gpointer
_control_thread_func (gpointer data)
{
    GstNvmPlayerAppCtx *ctx = (GstNvmPlayerAppCtx *) data;
    /* Wait Until Main Loop is Running */
    while (!g_main_loop_is_running (ctx->main_loop))
        g_usleep (GST_NVM_MAIN_LOOP_TIMEOUT);

    if (ctx->cmd_line_rscript_flag) {
        ctx->cmd_line_rscript_flag = FALSE;
        _run_script (ctx, ctx->script_name, 1);
        return 0;
    }
    else if (ctx->cmd_line_script_flag) {
        ctx->cmd_line_script_flag = FALSE;
        _run_script (ctx, ctx->script_name, 0);
        return 0;
    }

    g_print ("Type \"h\" for a list of options \n");

    while (!quit_flag) {
        g_print("-");
        fflush (stdout);
        _player_process (ctx, NULL);
    }

    _player_app_quit (ctx);

    return 0;
}

static void
_sig_handler (int signum)
{
    if(signum == SIGQUIT)
    {
        printf("we got a SIGQUIT; print debug info\n");
        gst_nvm_get_backtrace();
        return;
    }
    signal (SIGINT, SIG_IGN);
    signal (SIGTERM, SIG_IGN);
    signal (SIGQUIT, SIG_IGN);

    quit_flag = TRUE;

    signal (SIGINT, SIG_DFL);
    signal (SIGTERM, SIG_DFL);
    signal (SIGQUIT, SIG_DFL);
}

static void
_sig_setup (void)
{
    struct sigaction action;

    memset (&action, 0, sizeof (action));
    action.sa_handler = _sig_handler;

    sigaction (SIGINT, &action, NULL);
    sigaction (SIGQUIT, &action, NULL);
}

static void
_plugin_set_rank (guint rank)
{
  guint i = 0;
  GstElementFactory* factory = NULL;
  gchar* plugin_list[] = {
    "nvmediamp3auddec",
    "nvmediaaacauddec",
    "nvmediawmaauddec",
    "nvmediaaacaudenc",
    "nvmediampeg2viddec",
    "nvmediampeg4viddec",
    "nvmediavc1viddec",
    "nvmediamjpegviddec",
    "nvmediah264viddec",
    "nvmediah264videnc",
    "nvmediacapturesrc",
    "nvmediaoverlaysink",
    "nvmediaeglstreamsink",
    "nvmediavp8viddec",
    "nvmediah265viddec",
    NULL,
  };

  while (plugin_list[i]) {
    factory = gst_element_factory_find (plugin_list[i]);
    if (factory) {
      gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), rank);
      gst_object_unref (factory);
    }
    i++;
  }
}

static gchar *
gst_nvm_debug_print_object (gpointer ptr)
{
  GObject *object = (GObject *) ptr;

  /* nicely printed object */
  if (object == NULL) {
    return g_strdup ("(NULL)");
  }
  if (*(GType *) ptr == GST_TYPE_CAPS) {
    return gst_caps_to_string ((const GstCaps *) ptr);
  }
  if (*(GType *) ptr == GST_TYPE_STRUCTURE) {
    return gst_structure_to_string ((const GstStructure *) ptr);
  }
  if (*(GType *) ptr == GST_TYPE_TAG_LIST) {
    /* FIXME: want pretty tag list with long byte dumps removed.. */
    return gst_tag_list_to_string ((GstTagList *) ptr);
  }
  if (GST_IS_BUFFER (ptr)) {
    GstBuffer *buf = (GstBuffer *) ptr;
    gchar *ret;

    ret =
        g_strdup_printf ("%p, pts %" GST_TIME_FORMAT ", dts %" GST_TIME_FORMAT
        ", dur %" GST_TIME_FORMAT ", size %" G_GSIZE_FORMAT ", offset %"
        G_GUINT64_FORMAT ", offset_end %" G_GUINT64_FORMAT, buf,
        GST_TIME_ARGS (GST_BUFFER_PTS (buf)),
        GST_TIME_ARGS (GST_BUFFER_DTS (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), gst_buffer_get_size (buf),
        GST_BUFFER_OFFSET (buf), GST_BUFFER_OFFSET_END (buf));
    return ret;
  }
#ifdef USE_POISONING
  if (*(guint32 *) ptr == 0xffffffff) {
    return g_strdup_printf ("<poisoned@%p>", ptr);
  }
#endif
  if (GST_IS_PAD (object) && GST_OBJECT_NAME (object)) {
    return g_strdup_printf ("<%s:%s>", GST_DEBUG_PAD_NAME (object));
  }
  if (GST_IS_OBJECT (object) && GST_OBJECT_NAME (object)) {
    return g_strdup_printf ("<%s>", GST_OBJECT_NAME (object));
  }
  if (G_IS_OBJECT (object)) {
    return g_strdup_printf ("<%s@%p>", G_OBJECT_TYPE_NAME (object), object);
  }
  if (GST_IS_MESSAGE (object)) {
    GstMessage *msg = GST_MESSAGE_CAST (object);
    gchar *s, *ret;
    const GstStructure *structure;

    structure = gst_message_get_structure (msg);

    if (structure) {
      s = gst_structure_to_string (structure);
    } else {
      s = g_strdup ("(NULL)");
    }

    ret = g_strdup_printf ("%s message from element '%s': %s",
        GST_MESSAGE_TYPE_NAME (msg), (msg->src != NULL) ?
        GST_ELEMENT_NAME (msg->src) : "(NULL)", s);
    g_free (s);
    return ret;
  }
  if (GST_IS_QUERY (object)) {
    GstQuery *query = GST_QUERY_CAST (object);
    const GstStructure *structure;

    structure = gst_query_get_structure (query);

    if (structure) {
      return gst_structure_to_string (structure);
    } else {
      const gchar *query_type_name;

      query_type_name = gst_query_type_get_name (query->type);
      if (G_LIKELY (query_type_name != NULL)) {
        return g_strdup_printf ("%s query", query_type_name);
      } else {
        return g_strdup_printf ("query of unknown type %d", query->type);
      }
    }
  }
  if (GST_IS_EVENT (object)) {
    GstEvent *event = GST_EVENT_CAST (object);
    gchar *s, *ret;
    GstStructure *structure;

    structure = (GstStructure *) gst_event_get_structure (event);
    if (structure) {
      s = gst_structure_to_string (structure);
    } else {
      s = g_strdup ("(NULL)");
    }

    ret = g_strdup_printf ("%s event at time %"
        GST_TIME_FORMAT ": %s",
        GST_EVENT_TYPE_NAME (event), GST_TIME_ARGS (event->timestamp), s);
    g_free (s);
    return ret;
  }

  return g_strdup_printf ("%p", ptr);
}

static void gst_nvm_log_function (GstDebugCategory *category,
                        GstDebugLevel level,
                        const gchar *file,
                        const gchar *function,
                        gint line,
                        GObject *object,
                        GstDebugMessage *message,
                        gpointer user_data) {
  gint pid;
  GstClockTime elapsed;
  gchar *obj = NULL;
  FILE *log_file;

  if (level > gst_debug_category_get_threshold (category))
    return;

  const gchar *env = g_getenv ("GST_DEBUG_FILE");
  if (env != NULL && *env != '\0') {
    if (strcmp (env, "-") == 0) {
      log_file = stdout;
    } else {
      log_file = g_fopen (env, "w");
      if (log_file == NULL) {
        g_printerr ("Could not open log file '%s' for writing: %s\n", env,
            g_strerror (errno));
        log_file = stderr;
      }
    }
  } else {
    log_file = stderr;
  }

  pid = getpid ();
  if (object) {
    obj = gst_nvm_debug_print_object (object);
  } else {
    obj = g_strdup ("");
  }

  elapsed = GST_CLOCK_DIFF (_priv_gst_info_start_time,
      gst_util_get_timestamp ());

  gchar **tokens = g_strsplit (file, "/", 0);

  fprintf (log_file, "%" GST_TIME_FORMAT "%5d %14p %s %20s %s:%d:%s:%s %s\n", GST_TIME_ARGS (elapsed),
           pid, g_thread_self (), gst_debug_level_get_name (level),
           gst_debug_category_get_name (category), tokens[g_strv_length(tokens) - 1], line, function, obj,
           gst_debug_message_get (message));
  fflush (log_file);
}

gint main (gint  argc, gchar *argv[])
{
    GstNvmPlayerAppCtx *ctx;
    GstNvmPlayerTestArgs test_args;
    GThread * main_thread;
    gint ret_val = 0;
    GstNvmResult result = GST_NVM_RESULT_OK;

    _priv_gst_info_start_time = gst_util_get_timestamp ();
    gst_init (&argc, &argv);
    gst_debug_add_log_function (gst_nvm_log_function, NULL, NULL);
    gst_debug_remove_log_function (gst_debug_log_default);
    GST_DEBUG_CATEGORY_INIT (gst_nvm_player_debug,
                             "gstnvmplayerdebug",
                             GST_DEBUG_FG_YELLOW,
                             "GST NvMedia Player");
    gst_debug_set_threshold_for_name ("gstnvmplayerdebug", GST_LEVEL_ERROR);

    ctx = g_malloc (sizeof (GstNvmPlayerAppCtx));
    if (!ctx) {
        GST_ERROR ("Out of memory");
        return -1;
    }
    memset (ctx, 0, sizeof (GstNvmPlayerAppCtx));

    if (gst_nvm_device_map_init (&ctx->device_map)) {
        g_printf ("Failed to create devices map\n");
        return -1;
    }

    memset (&test_args, 0, sizeof (GstNvmPlayerTestArgs));
    result = gst_nvm_parse_command_line (argc, argv, &test_args);
    if (result != GST_NVM_RESULT_OK) {
        g_printf ("Failed parsing command line\n");
        return -1;
    }

    ctx->multi_inst_flag = test_args.multi_inst_flag;
    ctx->cmd_line_script_flag = test_args.cmd_line_script_flag;
    ctx->cmd_line_rscript_flag = test_args.cmd_line_rscript_flag;
    ctx->default_ctx_capture_flag = test_args.default_ctx_capture_flag;
    ctx->default_ctx_usb_flag = test_args.default_ctx_usb_flag;
    ctx->default_ctx_avb_src_flag = test_args.default_ctx_avb_src_flag;
    ctx->default_ctx_avb_sink_flag = test_args.default_ctx_avb_sink_flag;
    ctx->capture_config_file = &test_args.capture_config_file[0];
    ctx->usb_device = &test_args.usb_device[0];
    ctx->script_name = &test_args.script_name[0];

    GST_DEBUG ("Starting player with following arguments: multi inst ? %s, cmd line script ? %s,\
    recursive script ? %s, default context: %s, capture conf file: %s, usb device type %s, script name: %s",
               ctx->multi_inst_flag ? "YES" : "NO",
               ctx->cmd_line_script_flag ? "YES" : "NO",
               ctx->cmd_line_rscript_flag ? "YES" : "NO",
               ctx->default_ctx_capture_flag ? "CAPTURE" :
               (ctx->default_ctx_usb_flag ? "USB" :
               (ctx->default_ctx_avb_src_flag ? "AVB_SRC" :
               (ctx->default_ctx_avb_sink_flag ? "AVB_SINK" : "MEDIA PLAYER"))),
               ctx->capture_config_file,
               ctx->usb_device,
               ctx->script_name);

    memset (ctx->player_contexts, 0,
            GST_NVM_MAX_CONTEXTS * sizeof (GstNvmPlayerInstCtx));

    /* increase the rank of nvmedia plugins for the duration of this test */
    _plugin_set_rank (GST_RANK_PRIMARY + 1);

    ctx->main_loop = g_main_loop_new (NULL, FALSE);

    result = gst_nvm_player_init (GST_NVM_MAX_CONTEXTS);
    if (ctx->default_ctx_capture_flag)
        ret_val = _start_player_context (ctx, GST_NVM_CAPTURE, "default_capture_context", "default");
    else if (ctx->default_ctx_usb_flag)
        ret_val = _start_player_context (ctx, GST_NVM_USB_AUDIO, "default_usb_context", "default");
    else if (ctx->default_ctx_avb_src_flag)
        ret_val = _start_player_context (ctx, GST_NVM_AVB_SRC, "default_avb_src_context", "default");
    else if (ctx->default_ctx_avb_sink_flag)
        ret_val = _start_player_context (ctx, GST_NVM_AVB_SINK, "default_avb_sink_context", "default");
    else
        ret_val = _start_player_context (ctx, GST_NVM_PLAYER, "default_media_player_context", "default");

    if (ret_val || result != GST_NVM_RESULT_OK) {
        g_printf ("Failed starting context\n");
        gst_nvm_device_map_fini (&ctx->device_map);
        return -1;
    }
    _sig_setup ();
    main_thread = g_thread_new ("main_thread", _control_thread_func, ctx);
    g_main_loop_run (ctx->main_loop);
    g_thread_join (main_thread);
    g_main_loop_unref (ctx->main_loop);
    gst_nvm_device_map_fini (&ctx->device_map);
    gst_nvm_player_fini ();

    g_free (ctx);

    return 0;
}
