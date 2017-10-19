/* Copyright (c) 2013-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <gst/gst.h>
#include <stdlib.h>
#include "player-core-priv.h"
#include "config-parser.h"
#include "testutil_board.h"
#include "testutil_capture_input.h"

#define I2C_0 0
#define I2C_1 1
#define I2C_2 2
#define I2C_3 3
#define I2C_4 4

/* Capture Specific Data */
typedef struct {
    /* I2C device */
    guint               i2c_device;

    /* CSI */
    gboolean            csi_test_flag;
    guint               csi_device;
    guint               csi_frame_format;
    guint               csi_input_width;
    guint               csi_input_height;
    guint               csi_lane_count;
    guint               csi_input_video_std;
    gboolean            csi_capture_interlaced;
    guint               csi_extra_lines;
    guint               csi_interlace_extralines_delta;

    /* VIP */
    gboolean            vip_test_flag;
    guint               vip_device;
    guint               vip_input_width;
    guint               vip_input_height;

    gboolean            live_mode;
    gchar              *interface;
    gchar              *format;
    gchar               crc_file[GST_NVM_MAX_STRING_SIZE];
    gchar               params_set_name[GST_NVM_MAX_STRING_SIZE];
    gchar               config_file_name[GST_NVM_MAX_STRING_SIZE];
    CaptureInputHandle  handle;
} GstNvmCaptureData;

typedef struct {
  gchar name[GST_NVM_MAX_STRING_SIZE];
  gchar description[GST_NVM_MAX_STRING_SIZE];
  gchar input_format[GST_NVM_MAX_STRING_SIZE];
  gchar input_device[GST_NVM_MAX_STRING_SIZE];
  gchar resolution[GST_NVM_MAX_STRING_SIZE];
  gchar board[GST_NVM_MAX_STRING_SIZE];
  gchar interface[GST_NVM_MAX_STRING_SIZE];
  gint  i2c_device;
  guint csi_lanes;
} GstNvmCaptureConfig;

static GstNvmCaptureConfig s_capture_config_sets[GST_NVM_MAX_CONFIG_SECTIONS];
static gint s_param_sets_num = 0;

/* Static Functions Declaration */

static GstNvmResult
_capture_initialize_sink (
    GstNvmContext *ctx);

static GstNvmResult
_capture_configure_csi (
    GstNvmContext *ctx);

static GstNvmResult
_capture_configure_vip (
    GstNvmContext *ctx);

static gint
_capture_get_params_section_id (
    GstNvmCaptureConfig *capture_config_sets,
    gint param_sets,
    gchar* params_set_name);

static GstNvmResult
_capture_params_init (
    GstNvmContext *ctx,
    GstNvmCaptureConfig capture_config);

static gboolean
_capture_bus_callback (
    GstBus *bus,
    GstMessage *message,
    gpointer data);

static
GstNvmResult
_capture_set_mode (
    GstNvmContextHandle handle);

static
GstNvmResult
_capture_set_crc_file (
    GstNvmContextHandle handle);

/* End Of Static Functions Declaration */

GstNvmResult
gst_nvm_capture_init (GstNvmContextHandle handle, gchar *config_file)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmCaptureData *priv_data = NULL;
    GstNvmResult result;

    ctx->func_table[GST_NVM_CMD_PLAY]                        = gst_nvm_player_start;
    ctx->func_table[GST_NVM_CMD_PAUSE_RESUME]                = NULL;
    ctx->func_table[GST_NVM_CMD_STOP]                        = gst_nvm_player_stop;
    ctx->func_table[GST_NVM_CMD_QUIT]                        = gst_nvm_player_quit;
    ctx->func_table[GST_NVM_CMD_USB_SWITCH]                  = NULL;
    ctx->func_table[GST_NVM_CMD_WAIT_TIME]                   = gst_nvm_common_wait_time;
    ctx->func_table[GST_NVM_CMD_WAIT_END]                    = NULL;
    ctx->func_table[GST_NVM_CMD_SET_POSITION]                = NULL;
    ctx->func_table[GST_NVM_CMD_SET_SPEED]                   = NULL;
    ctx->func_table[GST_NVM_CMD_SET_BRIGHTNESS]              = gst_nvm_common_set_brightness;
    ctx->func_table[GST_NVM_CMD_SET_CONTRAST]                = gst_nvm_common_set_contrast;
    ctx->func_table[GST_NVM_CMD_SET_SATURATION]              = gst_nvm_common_set_saturation;
    ctx->func_table[GST_NVM_CMD_SET_AUDIO_DEVICE]            = NULL;
    ctx->func_table[GST_NVM_CMD_SET_AUDIO_DEVICE_USB]        = NULL;
    ctx->func_table[GST_NVM_CMD_SET_VIDEO_DEVICE]            = gst_nvm_common_set_display;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_ID]       = gst_nvm_common_set_display_window_id;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_DEPTH]    = gst_nvm_common_set_display_window_depth;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_POSITION] = gst_nvm_common_set_display_window_position;
    ctx->func_table[GST_NVM_CMD_SPEW_PTM_VALUES]             = NULL;
    ctx->func_table[GST_NVM_CMD_SPEW_MSGS]                   = NULL;
    ctx->func_table[GST_NVM_CMD_USE_DECODEBIN]               = NULL;
    ctx->func_table[GST_NVM_CMD_ECHO_MSG]                    = gst_nvm_common_print_msg;
    ctx->func_table[GST_NVM_CMD_CAPTURE_MODE]                = _capture_set_mode;
    ctx->func_table[GST_NVM_CMD_CAPTURE_CRC_FILE]            = _capture_set_crc_file;

    ctx->timeout = 5;

    ctx->private_data = (GstNvmCaptureData *) g_malloc (sizeof (GstNvmCaptureData));
    if (!ctx->private_data)
        return GST_NVM_RESULT_OUT_OF_MEMORY;

    priv_data = (GstNvmCaptureData *) ctx->private_data;
    priv_data->live_mode = TRUE;

    if (config_file) {
        if (strlen (config_file) <= GST_NVM_MAX_STRING_SIZE)
            strcpy (priv_data->config_file_name, config_file);
        else {
            GST_WARNING ("Capture config file name is too long. Using default config file. (max allowed size is %d)", GST_NVM_MAX_STRING_SIZE);
            strcpy (priv_data->config_file_name, "configs/capture.conf");
        }
    } else {
        strcpy (priv_data->config_file_name, "configs/capture.conf");
    }

    memset (priv_data->crc_file, '\0', sizeof (priv_data->crc_file));

    // Parse config file only if wasn't parsed yet.
    if (s_param_sets_num > 0)
        return GST_NVM_RESULT_NOOP;

    GstNvmConfigParamsMap params_map[] = {
    //  {param_name, store_location, param_type, default_value, string_size}
        {"name",         &s_capture_config_sets[0].name,         GST_NVM_TYPE_CHAR_ARR, 0, sizeof (s_capture_config_sets[0].name)},
        {"description",  &s_capture_config_sets[0].description,  GST_NVM_TYPE_CHAR_ARR, 0, sizeof (s_capture_config_sets[0].description)},
        {"input_format", &s_capture_config_sets[0].input_format, GST_NVM_TYPE_CHAR_ARR, 0, sizeof (s_capture_config_sets[0].input_format)},
        {"input_device", &s_capture_config_sets[0].input_device, GST_NVM_TYPE_CHAR_ARR, 0, sizeof (s_capture_config_sets[0].input_device)},
        {"resolution",   &s_capture_config_sets[0].resolution,   GST_NVM_TYPE_CHAR_ARR, 0, sizeof (s_capture_config_sets[0].resolution)},
        {"board",        &s_capture_config_sets[0].board,        GST_NVM_TYPE_CHAR_ARR, 0, sizeof (s_capture_config_sets[0].board)},
        {"interface",    &s_capture_config_sets[0].interface,    GST_NVM_TYPE_CHAR_ARR, 0, sizeof (s_capture_config_sets[0].interface)},
        {"csi_lanes",    &s_capture_config_sets[0].csi_lanes,    GST_NVM_TYPE_UINT,     0, 0},
        {"i2c_device",   &s_capture_config_sets[0].i2c_device,   GST_NVM_TYPE_INT,     -1, 0},
        {NULL} // Specifies the end of the array
    };

    gst_nvm_config_parser_init_params_map (params_map);
    result = gst_nvm_config_parser_parse_file (params_map,
                                               sizeof (GstNvmCaptureConfig),
                                               priv_data->config_file_name,
                                               &s_param_sets_num);
    if (result != GST_NVM_RESULT_OK) {
        GST_ERROR ("gst_nvm_config_parser_parse_file failed");
        return result;
    }

    return GST_NVM_RESULT_OK;
}

static
GstNvmResult
_capture_set_mode (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmCaptureData *priv_data = (GstNvmCaptureData *) ctx->private_data;

    GST_DEBUG ("Setting capture mode to %s", ctx->last_command_param.string_value);
    if (!strcasecmp (ctx->last_command_param.string_value, "live")) {
        priv_data->live_mode = TRUE;
    } else if (!strcasecmp (ctx->last_command_param.string_value, "cb")) {
        priv_data->live_mode = FALSE;
    } else {
        GST_WARNING ("Unknown capture mode encountered: %s", ctx->last_command_param.string_value);
        return GST_NVM_RESULT_INVALID_ARGUMENT;
    }

    return GST_NVM_RESULT_OK;
}

static
GstNvmResult
_capture_set_crc_file (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmCaptureData *priv_data = (GstNvmCaptureData *) ctx->private_data;

    if (!priv_data->live_mode) {
        GST_DEBUG ("Setting capture crc file to %s", ctx->last_command_param.string_value);
        if (strcasecmp (ctx->last_command_param.string_value, "NULL")) {
            memset (priv_data->crc_file, '\0', sizeof (priv_data->crc_file));
            return GST_NVM_RESULT_OK;
        } else {
            if (strlen (ctx->last_command_param.string_value) <= sizeof (priv_data->crc_file))
                strcpy (priv_data->crc_file, ctx->last_command_param.string_value);
            else {
                GST_WARNING ("Failed reading CRC file. CRC file name is too long. (max allowed size is %ld)", sizeof (priv_data->crc_file));
                return GST_NVM_RESULT_FAIL;
            }
        }
    } else {
        GST_WARNING ("CRC checks are valid only in test mode (cb)");
        return GST_NVM_RESULT_NOOP;
    }

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_capture_start (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmCaptureData *priv_data = (GstNvmCaptureData *) ctx->private_data;
    gchar *capture_params_name = ctx->last_command_param.string_value;
    gint param_set_id = 0;
    GstNvmResult result;
   //GstMessage* msg;
    GstBus *bus;

    // Set Capture Params
    if (capture_params_name != NULL) {
        strncpy (priv_data->params_set_name, capture_params_name, GST_NVM_MAX_STRING_SIZE);
        priv_data->params_set_name[GST_NVM_MAX_STRING_SIZE - 1] = '\0';
    }

    param_set_id = _capture_get_params_section_id (
                                            s_capture_config_sets,
                                            s_param_sets_num,
                                            priv_data->params_set_name);
    if (param_set_id == -1) {
        param_set_id = 0; // Params set name doesn't exist; use default
        GST_INFO ("Params set name %s wasn't found. Using default.", priv_data->params_set_name);
    }

    _capture_params_init (ctx, s_capture_config_sets[param_set_id]);
    GST_INFO ("Capture using params set index: %d, name: %s",
              param_set_id,
              s_capture_config_sets[param_set_id].name);

    // Create Capture Pipeline
    ctx->pipeline = gst_pipeline_new ("capture-process");
    ctx->source = NULL;
    ctx->source = gst_element_factory_make ("nvmediacapturesrc", NULL);
    if (!ctx->source) {
        GST_ERROR ("capture source creation failed");
        return GST_NVM_RESULT_FAIL;
    }

    // Init Video Sink
    _capture_initialize_sink (ctx);

    // Configure the board with CSI/VIP settings
    if (priv_data->csi_test_flag) {
        result = _capture_configure_csi (ctx);
    } else if (priv_data->vip_test_flag) {
        result = _capture_configure_vip (ctx);
    } else {
        GST_ERROR ("Either CSI or VIP capture has to be used.");
        return GST_NVM_RESULT_FAIL;
    }

    if (IsFailed (result)) {
        GST_ERROR ("Capture configuration failed.");
        return GST_NVM_RESULT_FAIL;
    }

    gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->source, ctx->video_bin, NULL);
    if (!gst_element_link_many (ctx->source, ctx->video_bin, NULL)) {
        GST_ERROR ("Failed to add elements to the bin");
        return GST_NVM_RESULT_FAIL;
    }

    // Add a bus to watch for messages
    bus = gst_pipeline_get_bus (GST_PIPELINE (ctx->pipeline));
    ctx->source_id = gst_bus_add_watch (bus,
                                        (GstBusFunc) _capture_bus_callback,
                                        ctx);
    gst_object_unref (bus);

    // Start playing
    if (IsFailed (gst_nvm_common_change_state (ctx, GST_STATE_PLAYING,
                                               GST_NVM_WAIT_TIMEOUT))) {
        GST_ERROR ("Unable to set the pipeline to the playing state.");
        return GST_NVM_RESULT_FAIL;
    }

    ctx->is_active = TRUE;

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_capture_configure_vip (GstNvmContext *ctx)
{
    GstNvmCaptureData *priv_data = (GstNvmCaptureData *) ctx->private_data;
    CaptureInputConfigParams params;
    gint is_vip_ntsc = strncmp (priv_data->interface, "vip-ntsc", 8);

    switch (priv_data->vip_device) {
        case AnalogDevices_ADV7180:
            break;
        case AnalogDevices_ADV7182:
            params.width = priv_data->vip_input_width;
            params.height = priv_data->vip_input_height;
            params.vip.std = is_vip_ntsc ? 1 : 0;
            if (testutil_capture_input_open (priv_data->i2c_device,
                                             priv_data->vip_device,
                                             priv_data->live_mode,
                                             &priv_data->handle) < 0) {
                GST_ERROR ("Failed to open VIP device");
                return GST_NVM_RESULT_FAIL;
            }
            if (testutil_capture_input_configure (priv_data->handle, &params) < 0) {
                GST_ERROR ("Failed to configure VIP device");
                return GST_NVM_RESULT_FAIL;
            }
            break;
        }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_capture_configure_csi (GstNvmContext *ctx)
{
    GstNvmCaptureData *priv_data = (GstNvmCaptureData *) ctx->private_data;
    CaptureInputConfigParams params;

    if (!testutil_board_detect (BOARD_TYPE_E1861, BOARD_VERSION_A02)) {
        testutil_board_module_workaround (BOARD_TYPE_E1861,
                                          BOARD_VERSION_A02,
                                          MODULE_TYPE_NONE);
    }
    else if (!testutil_board_detect (BOARD_TYPE_E1861, BOARD_VERSION_NONE) &&
             (priv_data->csi_device == AnalogDevices_ADV7281 ||
              priv_data->csi_device == AnalogDevices_ADV7282)) {
        testutil_board_module_workaround (BOARD_TYPE_E1861,
                                          BOARD_VERSION_A01,
                                          MODULE_TYPE_NONE);
    }
    else if (!testutil_board_detect (BOARD_TYPE_E1611, BOARD_VERSION_A04)) {
        testutil_board_module_workaround(BOARD_TYPE_E1611,
                                         BOARD_VERSION_A04,
                                         MODULE_TYPE_NONE);
    }
    else { //if(!testutil_board_detect(BOARD_TYPE_PM358, BOARD_VERSION_B00))
        testutil_board_module_workaround(BOARD_TYPE_PM358,
                                         BOARD_VERSION_B00,
                                         MODULE_TYPE_NONE);
    }

    params.width = priv_data->csi_input_width;
    params.height = priv_data->csi_input_height;

    switch (priv_data->csi_device) {
        case AnalogDevices_ADV7281:
        case AnalogDevices_ADV7282:
            params.cvbs2csi.std = priv_data->csi_input_video_std;
            params.cvbs2csi.structure = (priv_data->csi_capture_interlaced) ? 1 : 0;
            break;
        case Toshiba_TC358743:
            params.hdmi2csi.lanes = priv_data->csi_lane_count;
            params.hdmi2csi.format = priv_data->csi_frame_format;
            params.hdmi2csi.structure = (priv_data->csi_capture_interlaced) ? 1 : 0;
            break;
        case NationalSemi_DS90UR910Q:
        default:
            params.hdmi2csi.lanes = priv_data->csi_lane_count;
            params.hdmi2csi.format = priv_data->csi_frame_format;
            break;
    }

    if (testutil_capture_input_open (priv_data->i2c_device,
                                     priv_data->csi_device,
                                     priv_data->live_mode,
                                     &priv_data->handle) < 0) {
        GST_ERROR ("Failed to open CSI device with following params: i2c device: %u, csi device: %u, mode: %s",
            priv_data->i2c_device, priv_data->csi_device, priv_data->live_mode ? "live" : "color bar");
        return GST_NVM_RESULT_FAIL;
    }
    GST_DEBUG ("Configuring CSI device with: Resolution: [%dx%d], lanes: %d, format: %d", params.width,
                                                                                          params.height,
                                                                                          params.hdmi2csi.lanes,
                                                                                          params.hdmi2csi.format);
    if (testutil_capture_input_configure (priv_data->handle, &params) < 0) {
        GST_ERROR ("Failed to configure CSI device");
        return GST_NVM_RESULT_FAIL;
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_capture_initialize_sink (GstNvmContext *ctx)
{
    GstNvmCaptureData *priv_data = (GstNvmCaptureData *) ctx->private_data;
    GstElement* renderer_overlay = NULL;
    GstElement* renderer_egl = NULL;
    GstElement* mixer;
    GstPad*     pad_video;

    ctx->video_tee = gst_element_factory_make ("tee", NULL);
    ctx->video_bin = gst_bin_new ("videosinkbin");
    if (!ctx->video_tee || !ctx->video_bin) {
        GST_ERROR ("Unable to create some elements. video bin: %p, video tee: %p",
                    ctx->video_bin, ctx->video_tee);
        return GST_NVM_RESULT_FAIL;
    }

    /* Create overlay sink*/
    if (priv_data->live_mode || (priv_data->crc_file[0] == '\0' || isblank (priv_data->crc_file))) {
        // live mode or no crc file supplied
        renderer_overlay = gst_element_factory_make ("nvmediaoverlaysink", NULL);
        if (!strcasecmp (ctx->display_type, "display-0"))
            g_object_set (G_OBJECT (renderer_overlay), "display-device", 0, NULL);
        else if (!strcasecmp (ctx->display_type, "display-1"))
            g_object_set (G_OBJECT (renderer_overlay), "display-device", 1, NULL);
        else if (!strcasecmp ("dual", ctx->display_type))
            g_object_set (G_OBJECT (renderer_overlay), "display-device", 2, NULL);
        else
            g_object_set (G_OBJECT (renderer_overlay), "display-device", 3, NULL); // None

        g_object_set (G_OBJECT (renderer_overlay), "window-id", ctx->display_properties.window_id, NULL);
        if (ctx->display_properties.depth_specified)
            g_object_set (G_OBJECT (renderer_overlay), "depth", ctx->display_properties.depth, NULL);
        if (ctx->display_properties.position_specified)
            g_object_set (G_OBJECT (renderer_overlay), "position", ctx->display_properties.position_str, NULL);
        if (priv_data->csi_capture_interlaced) {
            g_object_set (GST_OBJECT (renderer_overlay), "deinterlace-mode", 1, NULL); // Apply BOB by default
        }
    } else {
        GST_DEBUG ("Initializing overlay sink for color bar mode. crc file: %s", priv_data->crc_file);
        g_object_set (GST_OBJECT (ctx->source), "max-buffers", 10, NULL);
        renderer_overlay = gst_element_factory_make ("nvmediavtestsink", NULL);
        g_object_set (GST_OBJECT (renderer_overlay), "live-mode", priv_data->live_mode, NULL);
        g_object_set (GST_OBJECT (renderer_overlay), "chkcrc", priv_data->crc_file, NULL);
    }

    if (!renderer_overlay){
        GST_ERROR ("NvMediaVideoSink not found; playbin2 uses the default sink");
        return GST_NVM_RESULT_FAIL;
    }
    g_object_set (GST_OBJECT (renderer_overlay), "sync", 0, NULL);
    g_object_set (G_OBJECT (renderer_overlay), "async", FALSE, NULL);

    /* Create egl sink */
    if (ctx->eglsink_enabled_flag) {
        mixer = gst_element_factory_make ("nvmediasurfmixer", NULL);
        if (priv_data->live_mode || (priv_data->crc_file[0] == '\0' || isblank (priv_data->crc_file))) {
            renderer_egl = gst_element_factory_make ("nvmediaeglstreamsink", NULL);
        } else {
            GST_DEBUG ("Initializing egl sink for color bar mode. crc file: %s", priv_data->crc_file);
            g_object_set (GST_OBJECT (ctx->source), "max-buffers", 10, NULL);
            renderer_egl = gst_element_factory_make ("nvmediavtestsink", NULL);
            g_object_set (GST_OBJECT (renderer_egl), "chkcrc", priv_data->crc_file, NULL);
        }

        if (!mixer || !renderer_egl) {
            GST_ERROR ("Failed creating mixer (%p) or egl-sink (%p)", mixer, renderer_egl);
            return GST_NVM_RESULT_FAIL;
        }
#ifdef NV_EGL_SINK
        g_object_set (GST_OBJECT (renderer_egl), "display",  grUtilState.display, NULL);
        g_object_set (GST_OBJECT (renderer_egl), "stream", eglStream, NULL);
#endif
        g_object_set (GST_OBJECT (renderer_egl), "sync", 0, NULL);

        gst_bin_add_many (GST_BIN (ctx->video_bin), ctx->video_tee, renderer_overlay, mixer, renderer_egl, NULL);
        if (!gst_element_link_many (ctx->video_tee, renderer_overlay, NULL)) {
            GST_ERROR ("Failed linking tee with overlay sink");
            return GST_NVM_RESULT_FAIL;
        }
        if (!gst_element_link_many (ctx->video_tee, mixer, renderer_egl, NULL)) {
            GST_ERROR ("Failed linking tee with mixer and egl sink");
            return GST_NVM_RESULT_FAIL;
        }
    } else {
        gst_bin_add_many (GST_BIN (ctx->video_bin), ctx->video_tee, renderer_overlay, NULL);
        if (!gst_element_link_many (ctx->video_tee, renderer_overlay, NULL)) {
            GST_ERROR ("Failed linking tee with overlay sink");
            return GST_NVM_RESULT_FAIL;
        }
    }

    /* Set video bin sink ghost pad */
    pad_video = gst_element_get_static_pad (ctx->video_tee, "sink");
    ctx->ghost_pad_video = gst_ghost_pad_new ("sink", pad_video);
    gst_pad_set_active (ctx->ghost_pad_video, TRUE);
    gst_element_add_pad (ctx->video_bin, ctx->ghost_pad_video);
    gst_object_unref (pad_video);

    ctx->video_sink_overlay = renderer_overlay;
    ctx->video_sink_egl = renderer_egl;

    g_object_set (GST_OBJECT (ctx->source),"interface", priv_data->interface, NULL);

    if (priv_data->csi_test_flag) {
        g_object_set (GST_OBJECT (ctx->source), "width", priv_data->csi_input_width, NULL);
        g_object_set (GST_OBJECT (ctx->source), "height", priv_data->csi_input_height, NULL);
        g_object_set (GST_OBJECT (ctx->source), "csilanes", priv_data->csi_lane_count, NULL);
        g_object_set (GST_OBJECT (ctx->source), "extra-lines", priv_data->csi_extra_lines, NULL);
        g_object_set (GST_OBJECT (ctx->source), "interlace-extralines-delta", priv_data->csi_interlace_extralines_delta, NULL);
        g_object_set (GST_OBJECT (ctx->source), "captureinterlaceflag", priv_data->csi_capture_interlaced, NULL);
        g_object_set (GST_OBJECT (ctx->source), "format", priv_data->format, NULL);
    }

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_capture_stop (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmCaptureData *priv_data = (GstNvmCaptureData *) ctx->private_data;
    GstState state;

    if (!ctx->is_active) return GST_NVM_RESULT_NOOP;

    if (ctx->source_id)
        g_source_remove (ctx->source_id);

    g_mutex_lock (&ctx->gst_lock);
    // Free resources
    if (ctx->pipeline) {
        gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);
        gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT);
        if (state != GST_STATE_NULL)
            GST_WARNING ("%s - pipeline not in GST_STATE_NULL but in %s state",
                        __func__, gst_element_state_get_name (state));

        gst_object_unref (GST_OBJECT (ctx->pipeline));
        g_source_remove (ctx->source_id);
        ctx->pipeline = NULL;
        ctx->video_bin = NULL;
    }

    // VIP
    if (priv_data->vip_test_flag) {
        switch (priv_data->vip_device) {
            case AnalogDevices_ADV7180:
                break;
            case AnalogDevices_ADV7182:
                testutil_capture_input_close (priv_data->handle);
                break;
        }
    }

    // CSI
    if (priv_data->csi_test_flag) {
        switch (priv_data->csi_device) {
            case AnalogDevices_ADV7281:
            case AnalogDevices_ADV7282:
            case NationalSemi_DS90UR910Q:
            case Toshiba_TC358743:
                testutil_capture_input_close (priv_data->handle);
                break;
        }
    }

    g_mutex_unlock (&ctx->gst_lock);

    ctx->is_active = FALSE;
    ctx->pipeline = NULL;
    ctx->source_id = 0;

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_capture_fini (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmCaptureData *priv_data = (GstNvmCaptureData *) ctx->private_data;

    if (priv_data) {
        gst_nvm_capture_stop (handle);
        g_free (priv_data);
        priv_data = NULL;
    }

    return GST_NVM_RESULT_OK;
}

static gint
_capture_get_params_section_id (
    GstNvmCaptureConfig *capture_config_sets,
    gint param_sets,
    gchar* params_set_name)
{
    gint i;

    for (i = 0; i < param_sets; i++)
        if (!strcmp (capture_config_sets[i].name, params_set_name))
            return i;

    return -1;
}

static GstNvmResult
_capture_params_init (
    GstNvmContext *ctx,
    GstNvmCaptureConfig config)
{
    GstNvmCaptureData *args = (GstNvmCaptureData *) ctx->private_data;
    gint width = 0, height = 0;
    gchar *separate_token;
    gchar board_split[2][GST_NVM_MAX_STRING_SIZE];
    BoardType board_type = BOARD_TYPE_NONE;
    BoardVersion board_version = BOARD_VERSION_A00;
    ModuleType module_type = MODULE_TYPE_NONE;
    gint i2c = I2C_0;

    if (!sscanf (config.resolution, "%ux%u", &width, &height) == 2) {
        GST_ERROR ("Bad resolution: %s", config.resolution);
        return GST_NVM_RESULT_FAIL;
    }

    // CSI-set defaults
    args->csi_test_flag = FALSE;
    args->csi_device = Toshiba_TC358743;
    args->csi_input_width = width;
    args->csi_input_height = height;
    args->csi_input_video_std = 0; //480p
    args->csi_frame_format = 1;
    args->csi_extra_lines = 0;
    args->csi_interlace_extralines_delta = 0;
    args->csi_capture_interlaced = FALSE;
    args->csi_lane_count = config.csi_lanes;
    if(args->csi_lane_count != 1 && args->csi_lane_count != 2 && args->csi_lane_count != 4) {
        GST_ERROR ("Bad # CSI interface lanes specified in config file: %d.using lanes - 2 as default",config.csi_lanes);
        args->csi_lane_count = 2;
    }

    // VIP-set defaults
    args->vip_test_flag = FALSE;
    args->vip_device = AnalogDevices_ADV7180;
    args->vip_input_width = width;
    args->vip_input_height = height;

    //general-set defaults
    args->interface = "csi-ab"; //default
    args->format = "422";

    //board-split
    separate_token = index (config.board, '-');
    strncpy (board_split[0], config.board, separate_token - config.board);
    board_split[0][separate_token - config.board] = '\0';
    strncpy (board_split[1], separate_token + 1, GST_NVM_MAX_STRING_SIZE);
    board_split[1][GST_NVM_MAX_STRING_SIZE - 1] = '\0';

    // Board Type
    board_type = BOARD_TYPE_NONE;
    if (!strcmp (board_split[0], "E1688"))
        board_type = BOARD_TYPE_E1688;
    else if (!strcmp (board_split[0], "E1853"))   //...jettson (vip)
        board_type = BOARD_TYPE_E1853;
    else if (!strcmp (board_split[0], "E1861"))   //...jettson (csi-hdmi, csi-cvbs)
        board_type = BOARD_TYPE_E1861;
    else if (!strcmp (board_split[0], "E1611"))   //...dalmore
        board_type = BOARD_TYPE_E1611;
    else if (!strcmp (board_split[0], "PM358"))   //...laguna
        board_type = BOARD_TYPE_PM358;
    else {
        GST_ERROR ("Bad board type specified: %s. Using default.",
                    board_split[0]);
        board_type = BOARD_TYPE_E1861;
    }

    // Board Version
    if (!strcmp (board_split[1], "a00"))
        board_version = BOARD_VERSION_A00;
    else if (!strcmp (board_split[1], "a01"))
        board_version = BOARD_VERSION_A01;
    else if (!strcmp (board_split[1], "a02"))
        board_version = BOARD_VERSION_A02;
    else if (!strcmp (board_split[1], "a03"))
        board_version = BOARD_VERSION_A03;
    else if (!strcmp (board_split[1], "a04"))
        board_version = BOARD_VERSION_A04;
    else if (!strcmp (board_split[1], "b00"))
        board_version = BOARD_VERSION_B00;
    else {
        GST_ERROR ("Bad board version specified: %s. Using default.",
                board_split[1]);
        board_version = BOARD_VERSION_A02;
    }

    // Module Type
    if (!strcmp (config.input_device, "tc358743")) {
        module_type = MODULE_TYPE_CAPTURE_CSI_H2C; }
    else if (!strcmp (config.input_device, "adv7281") || !strcmp (config.input_device, "adv7282"))
        module_type = MODULE_TYPE_CAPTURE_CSI_C2C;
    else if (!strcmp (config.input_device, "adv7180") || !strcmp (config.input_device, "adv7182") )
        module_type = MODULE_TYPE_CAPTURE_VIP;
    else {
        GST_ERROR ("Bad capture interface: %s", config.input_device);
        module_type = MODULE_TYPE_NONE;
    }

    //set csi/vip capture
    if (module_type == MODULE_TYPE_CAPTURE_VIP)
        args->vip_test_flag = TRUE;
    else
        args->csi_test_flag = TRUE;

    //get i2c_device
    if (config.i2c_device >= 0) {
        args->i2c_device = (guint) config.i2c_device;
    } else if(!testutil_board_module_get_i2c(board_type, board_version, module_type, &i2c)) {
        args->i2c_device = (guint) i2c;
    }

    // csi-capture
    if (args->csi_test_flag) {
        //csi_device
        if (module_type == MODULE_TYPE_CAPTURE_CSI_H2C)
            args->csi_device = Toshiba_TC358743;
        else { // module_type = MODULE_TYPE_CAPTURE_CSI_C2C
            args->csi_device = AnalogDevices_ADV7281;
        }

        //check input_std: valid only for csi-cvbs
        if(module_type == MODULE_TYPE_CAPTURE_CSI_C2C)
            if(args->csi_input_width != 720 || (args->csi_input_height != 480 && args->csi_input_height != 576))
                GST_ERROR ("csi-cvbs capture supports only 480i and 576i input resolutions. Bad resolution specified: %s.",config.resolution);

        //input_format
        if(module_type == MODULE_TYPE_CAPTURE_CSI_C2C) {
            args->csi_lane_count = 1;
            if(!strcmp (config.input_format, "422i")) {
                args->format = "422";
                args->csi_capture_interlaced = TRUE;
                args->csi_extra_lines = (args->csi_input_height == 480)? 14 : 0;
                args->csi_interlace_extralines_delta = (args->csi_input_height == 480)? 1 : 0;
                args->csi_input_video_std = (args->csi_input_height == 480)? 0 : 1;
            } else
                GST_ERROR ("csi-cvbs capture supports only 422i input format.Bad format specified: %s.",config.input_format);
        } else { //MODULE_TYPE_CAPTURE_CSI_H2C
            if (!strcmp (config.input_format, "422i")) {
                args->format = "422";
                args->csi_capture_interlaced = TRUE;
            } else if (!strcmp (config.input_format, "422p")) {
                args->format = "422";
                args->csi_frame_format = 1;
            } else if (!strcmp (config.input_format, "rgb")) {
                args->format = "rgb";
                args->csi_frame_format = 3;
            } else
                GST_ERROR ("csi-hdmi capture supports only 422p/422i/rgb input format.Bad format specified: %s.",config.input_format);
        }

        // Interface type
        if (!strcmp (config.interface, "csi-a"))
            args->interface = "csi-a";
        else if (!strcmp (config.interface, "csi-b"))
            args->interface = "csi-b";
        else if (!strcmp (config.interface, "csi-ab"))
            args->interface = "csi-ab";
        else if (!strcmp (config.interface, "csi-cd"))
            args->interface = "csi-cd";
        else if (!strcmp (config.interface, "csi-e"))
            args->interface = "csi-e";
        else {
            GST_WARNING ("Bad interface-type specified: %s.Using csi-ab as default",config.interface);
            args->interface = "csi-ab";
        }
    } else { //vip-capture
        //vip-device
        if (board_type == BOARD_TYPE_E1688)
        args->vip_device = AnalogDevices_ADV7180;
        else
        args->vip_device = AnalogDevices_ADV7182;

        //input_format
        args->format = "422";
        args->csi_frame_format = 1;

        //input_std
        if(module_type == MODULE_TYPE_CAPTURE_VIP)
        if(args->vip_input_width != 720 || (args->vip_input_height != 480 && args->vip_input_height != 576))
            GST_ERROR ("vip capture supports only 480i and 576i input resolutions. Bad resolution specified: %s.",config.resolution);

        // Interface type
        if (!strcmp (config.interface, "vip")) {
            if (args->vip_input_height == 480)
                args->interface = "vip-ntsc";
            else // vip_input_height == 576
                args->interface = "vip-pal";
        }
    }

    if (!args->live_mode) {
        //test mode
        if (args->vip_test_flag && args->vip_device != AnalogDevices_ADV7182) {
            GST_ERROR ("mode cb is not a supported option for board = 1688\n");
            return GST_NVM_RESULT_FAIL;
        }
    }

    GST_DEBUG ("_capture_params_init: Finished successfully.\
    \nResolution: [%d:%d]; CSI lanes: %u; Board: %s-%s; Module type: %d (%s); i2c device: %u, Input format: %s (frame format: %u); Interface: %s",
             width, height,
             args->csi_lane_count,
             board_split[0], board_split[1],
             module_type, config.input_device,
             args->i2c_device,
             args->format, args->csi_frame_format,
             args->interface);

    return GST_NVM_RESULT_OK;
}

static gboolean
_capture_bus_callback (GstBus *bus, GstMessage *message, gpointer data)
{
    GstNvmContext *ctx = (GstNvmContext *) data;
    GError* err;
    gchar* debug;

    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error (message, &err, &debug);
            GST_ERROR ("ERROR from element %s: %s",
            GST_OBJECT_NAME (message->src), err->message);
            GST_ERROR ("Debugging info: %s", (debug) ? debug : "none");
            g_error_free (err);
            g_free (debug);
            break;
        case GST_MESSAGE_APPLICATION:
            g_timeout_add_seconds (1,
                                  (GSourceFunc) gst_nvm_common_timeout_callback,
                                   ctx);
            break;
        case GST_MESSAGE_INFO:
            gst_message_parse_info (message, &err, &debug);
            g_error_free (err);
            g_free (debug);
            if (strstr (GST_OBJECT_NAME (message->src), "eglstreamsink")) {
                // TODO egl - p1
            }
            break;
        default:
            break;
    }

    return TRUE;
}

GstNvmResult
gst_nvm_capture_list_param_sets (GstNvmContextHandle handle)
{
    gint i;

    // Capture context has to be initialized before this function can be used
    // s_capture_config_sets is being populated in gst_nvm_capture_init
    if (!handle)
       return GST_NVM_RESULT_NOOP;

    g_printf ("Available parameter sets:\n");
    for (i = 0; i < s_param_sets_num; i++) {
        g_printf ("%s\n", s_capture_config_sets[i].name);
        g_printf ("\tDescription: %s\n", s_capture_config_sets[i].description);
    }

    return GST_NVM_RESULT_OK;
}
