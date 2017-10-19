/* Copyright (c) 2013-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <semaphore.h>
#include "player-core-priv.h"
#include "taskpool.h"

/* Threshold for difference b/w media duration and last incoming decode buffer pts */
#define GST_NVM_LAST_BUFFER_THRESHOLD       100 * GST_MSECOND

/* Media Player Specific Data */
typedef struct {
    GstElement             *decodebin;          // decodebin2 element
    gboolean                debug_log_flag;
    gboolean                is_elementary;
    gboolean                is_seekable;
    gboolean                is_audio_present;
    gboolean                is_video_present;
    gboolean                use_decodebin_flag;
    GCond                   eos_cond;
    GMutex                  eos_mutex;
    gboolean                eos_flag;
    GCond                   ptm_cond;
    GMutex                  ptm_mutex;
    gboolean                ptm_flag;
    gchar                   file_name[GST_NVM_MAX_STRING_SIZE];
    gdouble                 playback_rate;
    gint64                  duration;
} GstNvmMediaData;

/* Static Functions */
static GstNvmResult _media_reset (GstNvmContextHandle handle);
static GstNvmResult _media_load (GstNvmContextHandle handle, gchar *sFileName);
static GstNvmResult _media_play_internal (GstNvmContextHandle handle);
static GstNvmResult _media_unload (GstNvmContextHandle handle);
static gboolean     _is_seekable (GstNvmContextHandle handle);
static void         _on_fps_measurement (GstElement *fpsdisplaysink, gdouble fps,
                                         gdouble droprate, gdouble avgfps, gpointer user_data);
/* Command Execution Functions */
static GstNvmResult _media_pause_resume (GstNvmContextHandle handle);
static GstNvmResult _media_set_position (GstNvmContextHandle handle);
static GstNvmResult _media_set_speed (GstNvmContextHandle handle);
static GstNvmResult _media_set_audio_device (GstNvmContextHandle handle);
static GstNvmResult _media_enable_ptm (GstNvmContextHandle handle);
static GstNvmResult _media_enable_media_msgs (GstNvmContextHandle handle);
static GstNvmResult _media_use_decodebin (GstNvmContextHandle handle);
static GstNvmResult _media_downmix_audio (GstNvmContextHandle handle);
static gint64       _media_get_duration (GstNvmContext *ctx);

GstNvmResult
gst_nvm_media_init (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) g_malloc (sizeof (GstNvmMediaData));

    priv_data->playback_rate = 1.0;
    priv_data->duration = 0;
    priv_data->use_decodebin_flag = FALSE;
    priv_data->eos_flag = FALSE;
    g_cond_init (&priv_data->eos_cond);
    g_mutex_init (&priv_data->eos_mutex);
    priv_data->ptm_flag = FALSE;
    g_cond_init (&priv_data->ptm_cond);
    g_mutex_init (&priv_data->ptm_mutex);
    ctx->private_data = priv_data;

    ctx->func_table[GST_NVM_CMD_PLAY]                        = gst_nvm_player_start;
    ctx->func_table[GST_NVM_CMD_PAUSE_RESUME]                = _media_pause_resume;
    ctx->func_table[GST_NVM_CMD_STOP]                        = gst_nvm_player_stop;
    ctx->func_table[GST_NVM_CMD_QUIT]                        = gst_nvm_player_quit;
    ctx->func_table[GST_NVM_CMD_USB_SWITCH]                  = NULL;
    ctx->func_table[GST_NVM_CMD_WAIT_TIME]                   = gst_nvm_common_wait_time;
    ctx->func_table[GST_NVM_CMD_WAIT_END]                    = gst_nvm_media_wait_on_end;
    ctx->func_table[GST_NVM_CMD_SET_POSITION]                = _media_set_position;
    ctx->func_table[GST_NVM_CMD_SET_SPEED]                   = _media_set_speed;
    ctx->func_table[GST_NVM_CMD_SET_BRIGHTNESS]              = gst_nvm_common_set_brightness;
    ctx->func_table[GST_NVM_CMD_SET_CONTRAST]                = gst_nvm_common_set_contrast;
    ctx->func_table[GST_NVM_CMD_SET_SATURATION]              = gst_nvm_common_set_saturation;
    ctx->func_table[GST_NVM_CMD_SET_AUDIO_DEVICE]            = _media_set_audio_device;
    ctx->func_table[GST_NVM_CMD_SET_AUDIO_DEVICE_USB]        = NULL;
    ctx->func_table[GST_NVM_CMD_SET_VIDEO_DEVICE]            = gst_nvm_common_set_display;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_ID]       = gst_nvm_common_set_display_window_id;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_DEPTH]    = gst_nvm_common_set_display_window_depth;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_POSITION] = gst_nvm_common_set_display_window_position;
    ctx->func_table[GST_NVM_CMD_SPEW_PTM_VALUES]             = _media_enable_ptm;
    ctx->func_table[GST_NVM_CMD_SPEW_MSGS]                   = _media_enable_media_msgs;
    ctx->func_table[GST_NVM_CMD_USE_DECODEBIN]               = _media_use_decodebin;
    ctx->func_table[GST_NVM_CMD_DOWNMIX_AUDIO]               = _media_downmix_audio;
    ctx->func_table[GST_NVM_CMD_ECHO_MSG]                    = gst_nvm_common_print_msg;
    ctx->func_table[GST_NVM_CMD_CAPTURE_MODE]                = NULL;
    ctx->func_table[GST_NVM_CMD_CAPTURE_CRC_FILE]            = NULL;

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_media_fini (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;

    if (priv_data) {
        g_mutex_clear (&priv_data->eos_mutex);
        g_cond_clear (&priv_data->eos_cond);
        g_mutex_clear (&priv_data->ptm_mutex);
        g_cond_clear (&priv_data->ptm_cond);
        g_free (priv_data);
        priv_data = NULL;
    }

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_media_wait_on_end (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;

    if (ctx->is_active && !priv_data->eos_flag) {
        GST_DEBUG ("Context %d is waiting for EOS...", ctx->id);
        g_mutex_lock (&priv_data->eos_mutex);

        while (!priv_data->eos_flag)
            g_cond_wait (&priv_data->eos_cond, &priv_data->eos_mutex);

        GST_DEBUG ("EOS reached. Context %d is ready for next commands!", ctx->id);
        g_mutex_unlock (&priv_data->eos_mutex);
    } else
        GST_DEBUG ("Context %d cannot be put on wait since no video is active or EOS was already reached", ctx->id);

    gst_nvm_semaphore_signal(ctx->wait_end_sem);
    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_media_stop (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    g_mutex_lock (&ctx->gst_lock);

    if (ctx->is_active) {
        GST_DEBUG ("Stopping Media in context %d...", ctx->id);
        _media_reset (ctx);
    }

    g_mutex_unlock (&ctx->gst_lock);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_media_play (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    gchar *file_name = ctx->last_command_param.string_value;
    GstNvmResult result;

    GST_DEBUG ("Stop current media if any and start playing %s", file_name);
    gst_nvm_media_stop (ctx);
    result = _media_load (ctx, file_name);
    if (IsSucceed (result)) {
        GST_DEBUG ("Start Playing...");
        priv_data->eos_flag = FALSE;
        ctx->is_active = TRUE;
        result = _media_play_internal (ctx);
    }

    /* WAR: wait for state change bus callback to signal completion of
       state transistion to playing state. */
    if (IsSucceed (result)) {
        gst_nvm_semaphore_wait (ctx->state_playing_sem);
        priv_data->is_seekable = _is_seekable (handle);
        priv_data->duration = _media_get_duration (ctx);
    }

    if (IsFailed (result)) {
        GST_DEBUG ("Play media failed. Unloading Media...");
        _media_unload (ctx);
    }

    return result;
}

static GstNvmResult
_media_reset (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;

    GST_DEBUG ("Resetting media");
    ctx->is_active = FALSE;
    g_mutex_lock (&priv_data->eos_mutex);
    priv_data->eos_flag = TRUE;
    g_cond_signal (&priv_data->eos_cond);
    g_mutex_unlock (&priv_data->eos_mutex);

    _media_unload (handle);

    priv_data->is_audio_present = FALSE;
    priv_data->is_video_present = FALSE;
    priv_data->is_elementary = TRUE;
    priv_data->duration = 0;

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_pause_resume (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    GstState state;

    if (IsSucceed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT))) {
        GST_DEBUG ("%s context: %d",
                   state == GST_STATE_PLAYING ? "Pausing" : "Resuming", ctx->id);
        switch (state) {
            case GST_STATE_PAUSED:
                if (priv_data->playback_rate != 0)
                    gst_nvm_common_change_state (ctx, GST_STATE_PLAYING, GST_NVM_WAIT_TIMEOUT);
                break;
            case GST_STATE_PLAYING:
                gst_nvm_common_change_state (ctx, GST_STATE_PAUSED, GST_NVM_WAIT_TIMEOUT);
                break;
            default:
                break;
        }
    }

    if (IsSucceed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
        GST_DEBUG ("Context: %d is now %s",
                   ctx->id, state == GST_STATE_PLAYING ? "playing" : "paused");

    return GST_NVM_RESULT_OK;
}

static gboolean
_is_seekable (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gboolean  seekable = FALSE;
    GstQuery *query;

    query = gst_query_new_seeking (GST_FORMAT_TIME);
    if (!ctx->pipeline)
        GST_WARNING ("Pipeline not active");
    if (gst_element_query (ctx->pipeline, query))
        gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
    gst_query_unref (query);

    return seekable;
}

static void
_on_fps_measurement (GstElement *fpsdisplaysink,
                     gdouble fps,
                     gdouble droprate,
                     gdouble avgfps,
                     gpointer user_data)
{
    g_print ("Current FPS: %f, Frames dropped: %f, Average FPS: %f\n", fps, droprate, avgfps);
    return;
}

static GstNvmResult
_media_set_position (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    GstEvent* seek_event;
    gboolean retval;
    gint value = ctx->last_command_param.int_value;
    GstState state;

    GST_DEBUG ("Setting position of context %d to %d", ctx->id, value);

    g_return_val_if_fail (value >= 0, GST_NVM_RESULT_FAIL);

    if (!ctx->pipeline) {
        GST_WARNING("Set position only active during playback");
        return GST_NVM_RESULT_FAIL;
    }

    if (!priv_data->is_seekable) {
        GST_WARNING ("Stream not seekable (trick modes not applicable)");
        return GST_NVM_RESULT_FAIL;
    }

    if (priv_data->eos_flag) {
        priv_data->playback_rate = 1.0;
        priv_data->eos_flag = FALSE;
    }

    seek_event = gst_event_new_seek (priv_data->playback_rate, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SKIP,
        GST_SEEK_TYPE_SET, (priv_data->playback_rate > 0.0) ? value * GST_MSECOND : 0,
        GST_SEEK_TYPE_SET, (priv_data->playback_rate > 0.0) ? -1 : value * GST_MSECOND);

    /* send the event */
    // HACK: Hangs when called in PAUSED state. Setting to PLAYING state first
    gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT);
    if (state == GST_STATE_PAUSED) {
        GST_DEBUG ("Media is PAUSED. Set to PLAYING mode before sending seek event");
        gst_nvm_common_change_state (ctx, GST_STATE_PLAYING, GST_NVM_WAIT_TIMEOUT);
        retval = gst_element_send_event (ctx->pipeline, seek_event);
        GST_DEBUG ("Setting media back to PAUSED mode");
        gst_nvm_common_change_state (ctx, GST_STATE_PAUSED, GST_NVM_WAIT_TIMEOUT);
    } else {
        retval = gst_element_send_event (ctx->pipeline, seek_event);
    }

    g_mutex_lock (&priv_data->ptm_mutex);
    g_cond_signal (&priv_data->ptm_cond);
    g_mutex_unlock (&priv_data->ptm_mutex);
    GST_DEBUG ("position: %d, playback_rate: %f, seek return value:%d",
                value, priv_data->playback_rate, retval);

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_set_speed (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    GstEvent* seek_event;
    gint64 position = 0;
    gint value = ctx->last_command_param.int_value;
    gboolean retval = FALSE;
    GstState state;

    GST_DEBUG ("Setting speed of context %d to %d", ctx->id, value);

    if (!ctx->pipeline) {
        GST_WARNING("Set speed only active during playback");
        return GST_NVM_RESULT_FAIL;
    }

    if (!priv_data->is_seekable) {
        GST_WARNING ("Stream not seekable - trick modes not applicable");
        return GST_NVM_RESULT_FAIL;
    }

    g_return_val_if_fail (value >= -32 && value <=32, GST_NVM_RESULT_FAIL);

    if ((value < -16) || (value > 16)) {
        GST_WARNING ("Set speed upto %c16x only supported. Defaulting to %c16x",
                     value < 0 ? '-' : '+', value < 0 ? '-' : '+');
        if (value < -16) value = -16;
        if (value > 16) value = 16;
    }

    /* If eos, the media position cannot be queried. So, set seek position based on
       playback rate. Else, get the current position. */
    if (priv_data->eos_flag) {
        if (priv_data->playback_rate > 0 && priv_data->duration > GST_NVM_LAST_BUFFER_THRESHOLD)
            position = priv_data->duration - GST_NVM_LAST_BUFFER_THRESHOLD;
        else
            position = 0;
        priv_data->eos_flag = FALSE;
    }
    else if (!gst_element_query_position (ctx->pipeline, GST_FORMAT_TIME, &position)) {
        GST_ERROR ("Curr position in the stream could not be retrieved");
        return GST_NVM_RESULT_FAIL;
    }

    if (IsSucceed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT))) {
        switch (state) {
            case GST_STATE_PAUSED:
                if (value != 0 && priv_data->playback_rate == 0) // Was paused by speed 0
                    gst_nvm_common_change_state (ctx, GST_STATE_PLAYING, GST_NVM_WAIT_TIMEOUT);
                break;
            case GST_STATE_PLAYING:
                if (value == 0) {
                    gst_nvm_common_change_state (ctx, GST_STATE_PAUSED, GST_NVM_WAIT_TIMEOUT);
                    priv_data->playback_rate = 0;
                    return GST_NVM_RESULT_OK;
                }
                break;
            default:
                break;
        }
    }

    seek_event = gst_event_new_seek ((gdouble) value, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SKIP,
        GST_SEEK_TYPE_SET, (value > 0) ? position : 0,
        GST_SEEK_TYPE_SET, (value > 0) ? -1 : position);

    /* send the event */
    // HACK: Hangs when called in PAUSED state. Setting to PLAYING state first
    gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT);
    if (state == GST_STATE_PAUSED) {
        GST_DEBUG ("Media is PAUSED. Setting to PLAYING mode before sending seek event");
        gst_nvm_common_change_state (ctx, GST_STATE_PLAYING, GST_NVM_WAIT_TIMEOUT);
        retval = gst_element_send_event (ctx->pipeline, seek_event);
        GST_DEBUG ("Setting media back to PAUSED mode");
        gst_nvm_common_change_state (ctx, GST_STATE_PAUSED, GST_NVM_WAIT_TIMEOUT);
    } else {
        retval = gst_element_send_event (ctx->pipeline, seek_event);
    }

    g_mutex_lock (&priv_data->ptm_mutex);
    g_cond_signal (&priv_data->ptm_cond);
    g_mutex_unlock (&priv_data->ptm_mutex);
    GST_DEBUG ("position: %lld, playback_rate: %d, seek_ret_val:%d",
                (long long int)position, value, retval);

    priv_data->playback_rate = (gdouble) value;
    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_get_absolute_path (gchar *file_name, gchar *abs_file_name)
{
    if (!abs_file_name || !file_name)
        return GST_NVM_RESULT_INVALID_POINTER;

    /* derive URI of the media file */
    if (strstr (file_name, "file://")) {
        /* parameter is already a URL (starts with "file://") */
        strcpy (abs_file_name, file_name);
    } else if (file_name[0] == '/') {
        /* absolute path; just prepend with "file://" */
        strcpy (abs_file_name, "file://");
        strcat (abs_file_name + strlen ("file://"), file_name);
    } else {
        /* relative path; prepend with "file://" and current directory */
        strcpy (abs_file_name, "file://");
        strcat (abs_file_name, g_get_current_dir ());
        strcat (strcat (abs_file_name, "/"), file_name);
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_initialize_sinks (GstNvmContext *ctx) {
    GstElement* renderer_overlay = NULL;
    GstElement* renderer_egl = NULL;
    GstElement* mixer;
    GstElement* fpsdispsink = NULL;
    GstPad *pad_audio, *pad_video;

    if (ctx->is_fps_profiling) {
        fpsdispsink = gst_element_factory_make ("fpsdisplaysink", NULL);
        if (!fpsdispsink) {
            GST_ERROR ("Unable to create some of the element: fpsdispsink (%p)", fpsdispsink);
            return GST_NVM_RESULT_FAIL;
        }
    }

    ctx->video_bin = gst_bin_new ("videosinkbin");
    ctx->video_tee = gst_element_factory_make ("tee", NULL);
    if (!ctx->video_tee || !ctx->video_bin) {
        GST_ERROR ("Failed creating some of the elements: video bin (%p), video tee (%p)",
                    ctx->video_bin, ctx->video_tee);
        return GST_NVM_RESULT_FAIL;
    }

    /* Create overlay sink*/
    renderer_overlay = gst_element_factory_make ("nvmediaoverlaysink", NULL);
    if (!renderer_overlay){
        GST_ERROR ("NvMediaVideoSink not found");
        return GST_NVM_RESULT_FAIL;
    } else {
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
    }

    if (ctx->is_fps_profiling) {
        g_object_set (G_OBJECT (fpsdispsink), "video-sink", G_OBJECT (renderer_overlay), NULL);
        g_object_set (G_OBJECT (fpsdispsink), "signal-fps-measurements", TRUE, NULL);
        g_object_set (G_OBJECT (fpsdispsink), "fps-update-interval", 1000, NULL);
        g_object_set (G_OBJECT (fpsdispsink), "text-overlay", FALSE, NULL);

        g_signal_connect (fpsdispsink, "fps-measurements", G_CALLBACK (_on_fps_measurement), NULL);
    }
    /* Add tee, overlay sink and eglsink to video bin */
    if (ctx->eglsink_enabled_flag) {
        /* Create egl sink */
        renderer_egl = gst_element_factory_make ("nvmediaeglstreamsink", NULL);
        if (!renderer_egl) {
            GST_ERROR ("Failed creating eglsink (%p)", renderer_egl);
            return GST_NVM_RESULT_FAIL;
        }
#ifdef NV_EGL_SINK
        g_object_set (GST_OBJECT (renderer_egl), "display",  grUtilState.display, NULL);
        g_object_set (GST_OBJECT (renderer_egl), "stream", eglStream, NULL);
#endif

        if (ctx->egl_consumer_type == GST_NVM_EGL_CONSUMER_CUDA_RGB ||
            ctx->egl_consumer_type == GST_NVM_EGL_CONSUMER_GL_RGB) {
            mixer = gst_element_factory_make ("nvmediasurfmixer", NULL);
            if(ctx->egl_cross_process_enabled_flag)
                g_object_set (GST_OBJECT (mixer), "bottom-origin", TRUE, NULL);
            if (!mixer) {
                GST_ERROR ("Failed creating mixer (%p)", mixer);
                return GST_NVM_RESULT_FAIL;
            }
            if (ctx->is_fps_profiling) {
                gst_bin_add_many (GST_BIN (ctx->video_bin), ctx->video_tee, mixer, fpsdispsink, NULL);
                g_object_set (G_OBJECT (fpsdispsink), "video-sink", G_OBJECT (renderer_egl), NULL);

                if (!gst_element_link_many (ctx->video_tee, mixer, fpsdispsink, NULL)) {
                    GST_ERROR ("Failed linking tee with mixer and fps display sink");
                    return GST_NVM_RESULT_FAIL;
                }
            }
            else {
                gst_bin_add_many (GST_BIN (ctx->video_bin), ctx->video_tee, mixer, renderer_egl, NULL);
                if (!gst_element_link_many (ctx->video_tee, mixer, renderer_egl, NULL)) {
                    GST_ERROR ("Failed linking tee with mixer and egl sink");
                    return GST_NVM_RESULT_FAIL;
                }
            }
        }
        else
        {
            if (ctx->is_fps_profiling) {
                gst_bin_add_many (GST_BIN (ctx->video_bin), ctx->video_tee, fpsdispsink, NULL);
                g_object_set (G_OBJECT (fpsdispsink), "video-sink", G_OBJECT (renderer_egl), NULL);

                if (!gst_element_link_many (ctx->video_tee, fpsdispsink, NULL)) {
                    GST_ERROR ("Failed linking tee with fps display sink");
                    return GST_NVM_RESULT_FAIL;
                }
            }
            else {
                gst_bin_add_many (GST_BIN (ctx->video_bin), ctx->video_tee, renderer_egl, NULL);
                if (!gst_element_link_many (ctx->video_tee, renderer_egl, NULL)) {
                    GST_ERROR ("Failed linking tee with egl sink");
                    return GST_NVM_RESULT_FAIL;
                }
            }
        }
    }
    else {
        if (ctx->is_fps_profiling) {
            gst_bin_add_many (GST_BIN (ctx->video_bin), ctx->video_tee, fpsdispsink, NULL);
            if (!gst_element_link_many (ctx->video_tee, fpsdispsink, NULL)) {
                GST_ERROR ("Failed linking tee with fps display sink");
                return GST_NVM_RESULT_FAIL;
            }
        }
        else
        {
            gst_bin_add_many (GST_BIN (ctx->video_bin), ctx->video_tee, renderer_overlay, NULL);
            if (!gst_element_link_many (ctx->video_tee, renderer_overlay, NULL)) {
                GST_ERROR ("Failed linking tee with overlay sink");
                return GST_NVM_RESULT_FAIL;
            }
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

    /* Audio sink */
    ctx->audio_bin = gst_bin_new ("audiosinkbin");
    if (!strcasecmp (ctx->audio_dev, "NULL"))
	ctx->audio_sink_alsa = gst_element_factory_make ("fakesink", NULL);
    else
	ctx->audio_sink_alsa = gst_element_factory_make ("alsasink", NULL);

    if (!ctx->audio_sink_alsa) {
        GST_ERROR ("Alsa/fake sink not found; playbin2 uses the default sink");
    } else {
        if (strcasecmp (ctx->audio_dev, "NULL")) { // If not NULL
            GST_DEBUG ("Setting audio device to %s", ctx->audio_dev);
            g_object_set (G_OBJECT (ctx->audio_sink_alsa), "device", ctx->audio_dev, NULL);
        }
    }
    ctx->audio_tee = gst_element_factory_make ("tee", NULL);

    if (ctx->downmix_audio) {
        ctx->audio_convert = gst_element_factory_make ("audioconvert", NULL);
        ctx->caps_filter = gst_element_factory_make ("capsfilter", NULL);
        GstCaps *pCaps = gst_caps_new_simple ("audio/x-raw", "channels", G_TYPE_INT, 2, NULL);
        g_object_set (ctx->caps_filter, "caps", pCaps, NULL);
        gst_caps_unref (pCaps);
        gst_bin_add_many (GST_BIN (ctx->audio_bin), ctx->audio_tee, ctx->audio_convert, ctx->caps_filter, ctx->audio_sink_alsa, NULL);
        gst_element_link_many (ctx->audio_tee, ctx->audio_convert, ctx->caps_filter, ctx->audio_sink_alsa, NULL);
    }
    else {
        gst_bin_add_many (GST_BIN (ctx->audio_bin), ctx->audio_tee, ctx->audio_sink_alsa, NULL);
        gst_element_link_many (ctx->audio_tee, ctx->audio_sink_alsa, NULL);
    }

    pad_audio = gst_element_get_static_pad (ctx->audio_tee, "sink");
    ctx->ghost_pad_audio = gst_ghost_pad_new ("sink", pad_audio);
    gst_pad_set_active (ctx->ghost_pad_audio, TRUE);
    gst_element_add_pad (ctx->audio_bin, ctx->ghost_pad_audio);
    gst_object_unref (pad_audio);

    return GST_NVM_RESULT_OK;
}

static gboolean
_media_bus_callback (GstBus *bus, GstMessage *message, gpointer data)
{
    GstNvmContext *ctx = (GstNvmContext *) data;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    GError* err;
    gchar* debug;
    GstState old, new, pending;

    switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error (message, &err, &debug);
            GST_ERROR ("ERROR from element %s: %s\nDebugging info: %s",
            GST_OBJECT_NAME (message->src), err->message, (debug) ? debug : "none");
            g_error_free (err);
            g_free (debug);
            break;
        case GST_MESSAGE_EOS:
            GST_DEBUG ("Received Bus Callback EOS");
            g_mutex_lock (&priv_data->eos_mutex);
            priv_data->eos_flag = TRUE;
            g_cond_signal (&priv_data->eos_cond);
            g_mutex_unlock (&priv_data->eos_mutex);
            break;
        case GST_MESSAGE_APPLICATION:
            g_timeout_add_seconds (1, (GSourceFunc) gst_nvm_common_timeout_callback, ctx);
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC (message) == GST_OBJECT (ctx->pipeline)) {
                gst_message_parse_state_changed (message, &old, &new, &pending);
                if (new == GST_STATE_PLAYING)
                    gst_nvm_semaphore_signal (ctx->state_playing_sem);
            }
            break;
        default:
            /* unhandled message */
            break;
    }
    return TRUE;
}

static gboolean
_media_on_stream_status (GstBus *bus, GstMessage *message,
                         gpointer user_data)
{
    GstStreamStatusType type;
    GstElement *owner;
    const GValue *val;
    GstTask *task = NULL;

    gst_message_parse_stream_status (message, &type, &owner);

    val = gst_message_get_stream_status_object (message);

    if (G_VALUE_TYPE (val) == GST_TYPE_TASK) {
        task = g_value_get_object (val);
    }

    switch (type) {
        case GST_STREAM_STATUS_TYPE_CREATE:
            if (!strcmp (gst_element_get_name (owner), "aqueue")) {
                GstTaskPool* pool = gst_nvm_task_pool_new (GST_NVM_AUDIO_QUEUE_PRIORITY,
                                                             GST_NVM_AUDIO_QUEUE_SCHED_POLICY);
                gst_task_set_pool (task, pool);
            }
            else if (!strcmp (gst_element_get_name (owner), "vqueue")) {
                GstTaskPool* pool = gst_nvm_task_pool_new (GST_NVM_VIDEO_QUEUE_PRIORITY,
                                                             GST_NVM_VIDEO_QUEUE_SCHED_POLICY);
                gst_task_set_pool (task, pool);
            }
            break;
       default:
            break;
       }
   return TRUE;
}

static gboolean
_media_autoplug_continue (GstElement *bin, GstPad *pad,
                          GstCaps *caps, gpointer data)
{
  GstStructure* structure = gst_caps_get_structure (caps, 0);
  const gchar*  type_name = gst_structure_get_name (structure);

  /* expose custom video type to typefind by disabling autoplug */
  return !(strstr (type_name, "video/x-nvmedia"));
}

static void
_media_new_pad_added (GstElement *element, GstPad *pad, gpointer data)
{
  GstCaps         *caps         = gst_pad_query_caps (pad, NULL);
  GstStructure    *structure    = gst_caps_get_structure (caps, 0);
  const gchar     *name         = gst_structure_get_name (structure);
  GstPad          *sinkpad      = NULL;
  GstElement     **decoder      = NULL;
  GstNvmContext   *ctx          = (GstNvmContext *) data;

  GST_DEBUG ("creating pipeline for \"%s\"", name);

  decoder = ((strstr (name, "video") || strstr (name, "image")) && strcasecmp(ctx->display_type, "NULL")) ?
            &ctx->video_bin :
            ((strstr (name, "audio") && strcasecmp(ctx->audio_dev, "NULL")) ? &ctx->audio_bin : NULL);

  if (decoder && *decoder) {
    /* add the video/audio pipe to the pipeline */
    if (g_object_is_floating (*decoder)) {
        if (gst_element_set_state (*decoder, GST_STATE_PAUSED) ==
            GST_STATE_CHANGE_FAILURE) {
            GST_ERROR ("Element: %s state change failure to %s",
                        GST_ELEMENT_NAME (*decoder),
                        gst_element_state_get_name (GST_STATE_PAUSED));
            gst_object_unref (*decoder);
            gst_caps_unref (caps);
            return;
        }

        if (!gst_bin_add (GST_BIN (ctx->pipeline), *decoder)) {
            GST_ERROR ("Failure to add element: %s to pipeline",
                        GST_ELEMENT_NAME (*decoder));
            gst_object_unref (*decoder);
            gst_caps_unref (caps);
            return;
        }
    }

    sinkpad = gst_element_get_static_pad (*decoder, "sink");
    if (sinkpad) {
        if (GST_PAD_LINK_FAILED (gst_pad_link (pad, sinkpad))) {
            GST_ERROR ("Failed to link \"%s\" to the (decode+)render pipeline \"%s\"",
                        name, GST_ELEMENT_NAME (*decoder));
            gst_object_unref (sinkpad);
            gst_element_set_state (GST_ELEMENT (*decoder), GST_STATE_NULL);
            gst_bin_remove (GST_BIN (ctx->pipeline), *decoder);
        } else {
            gst_object_unref (sinkpad);
        }
    } else {
        GST_ERROR ("Failed to get sink pad from %s", GST_ELEMENT_NAME (*decoder));
        gst_element_set_state (GST_ELEMENT (*decoder), GST_STATE_NULL);
        gst_bin_remove (GST_BIN (ctx->pipeline), *decoder);
    }
  }

  gst_caps_unref (caps);
}

static GstNvmResult
_media_build_custom_pipeline (GstNvmContext *ctx)
{
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;

    ctx->pipeline = gst_pipeline_new ("player");

    /* filesrc element */
    ctx->source = gst_element_factory_make ("filesrc", NULL);
    g_object_set (GST_OBJECT (ctx->source), "location",
                    priv_data->file_name + 7, NULL);

    if (!gst_bin_add (GST_BIN (ctx->pipeline), ctx->source)) {
        GST_ERROR ("Failed to add source: %s to pipeline",
                GST_ELEMENT_NAME (ctx->source));
        gst_object_unref (ctx->source);
        return GST_NVM_RESULT_FAIL;
    }

    /* decodebin (bin contains autoplugged demuxer and decoders) */
    priv_data->decodebin = gst_element_factory_make ("decodebin", NULL);
    if (!priv_data->decodebin) {
        GST_ERROR ("Failed to create decodebin element");
        return GST_NVM_RESULT_FAIL;
    }

    if (!gst_bin_add (GST_BIN (ctx->pipeline), priv_data->decodebin)) {
        GST_ERROR ("Failed to add decodebin to pipeline");
        gst_object_unref (priv_data->decodebin);
        return GST_NVM_RESULT_FAIL;
    }

    if (!gst_element_link (ctx->source, priv_data->decodebin)) {
        GST_ERROR ("failed to link source %s to decodebin2",
                GST_ELEMENT_NAME (ctx->source));
        return GST_NVM_RESULT_FAIL;
    }

    /* connect the audio/video pipes dynamically to the src pads of decodebin */
    g_signal_connect (GST_ELEMENT (priv_data->decodebin), "pad-added",
                      G_CALLBACK (_media_new_pad_added), ctx);

    g_signal_connect (priv_data->decodebin, "autoplug-continue",
                      G_CALLBACK (_media_autoplug_continue), ctx);

    /* create a video and audio sink bins to plug dynamically */
    ctx->video_bin = gst_bin_new ("videosinkbin");
    ctx->video_tee = gst_element_factory_make ("tee", NULL);
    if (!ctx->video_tee || !ctx->video_bin) {
        GST_ERROR ("Unable to create some of the elements: video bin (%p), video tee (%p)",
                    ctx->video_bin, ctx->video_tee);
        return GST_NVM_RESULT_FAIL;
    }

    return _media_initialize_sinks (ctx);
}

static void
_media_parse_input_uri (GstNvmContextHandle handle, gchar* uri)
{
    GstNvmContext   *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    GstElement  *playbin;
    GstElement  *v_fakesink;
    GstElement  *a_fakesink;
    GstElement  *element = NULL;
    GstState     state;
    GstPad      *input_selector_sinkpad;
    GstPad      *pad = NULL;
    gchar       *string = NULL;
    GQueue      *queue;

    priv_data->is_audio_present = FALSE;
    priv_data->is_video_present = FALSE;
    priv_data->is_elementary = TRUE;

    GST_DEBUG ("Fake a playbin pipeline for URI parsing");
    playbin = gst_element_factory_make ("playbin", NULL);
    v_fakesink = gst_element_factory_make ("nvmediaoverlaysink", NULL);
    a_fakesink = gst_element_factory_make ("fakesink", NULL);

    GST_DEBUG ("Configure sinks to be synchronous to avoid hangs");
    g_object_set (G_OBJECT (v_fakesink), "async", FALSE, NULL);
    g_object_set (G_OBJECT (a_fakesink), "async", FALSE, NULL);

    GST_DEBUG ("Set the properties of playbin");
    g_object_set (GST_OBJECT (playbin), "uri", uri, NULL);
    g_object_set (GST_OBJECT (playbin), "video-sink", v_fakesink, NULL);
    g_object_set (GST_OBJECT (playbin), "audio-sink", a_fakesink, NULL);

    GST_DEBUG ("Set playbin to PAUSED state");
    gst_element_set_state (playbin, GST_STATE_PAUSED);
    gst_element_get_state (playbin, &state, NULL, GST_CLOCK_TIME_NONE);

    GST_DEBUG ("Emit get-video-pad signal");
    g_signal_emit_by_name (playbin, "get-video-pad", 0, &input_selector_sinkpad);
    if (input_selector_sinkpad == NULL) {
        GST_DEBUG ("Video pipe absent");
    } else {
        GST_DEBUG ("Video pipe present");
        priv_data->is_video_present = TRUE;
        pad = gst_pad_get_peer (input_selector_sinkpad);
        element = gst_pad_get_parent_element (pad);
        queue = g_queue_new ();
        gst_nvm_common_get_plugin_info (element, queue);

        while (!g_queue_is_empty (queue)) {
            g_free (string);
            string = (gchar *) (g_queue_pop_tail (queue));
            if (priv_data->is_elementary && strstr (string, "demux")) {
                GST_DEBUG ("Not an elementary stream");
                priv_data->is_elementary = FALSE;
                continue;
            }
        }

        while (!g_queue_is_empty (queue)) {
            g_free (string);
            string = (gchar *) (g_queue_pop_tail (queue));
        }
        g_free (string);
        g_queue_free (queue);
        gst_object_unref (input_selector_sinkpad);
        gst_object_unref (element);
        gst_object_unref (pad);
    }

    GST_DEBUG ("Emit get-audio-pad signal");
    g_signal_emit_by_name (playbin, "get-audio-pad", 0, &input_selector_sinkpad);
    if (input_selector_sinkpad == NULL) {
        GST_DEBUG ("Audio pipe absent");
    } else {
        GST_DEBUG ("Audio pipe present");
        gst_object_unref (input_selector_sinkpad);
        priv_data->is_audio_present = TRUE;
    }

    gst_element_set_state (playbin, GST_STATE_NULL);
    gst_element_get_state (playbin, &state, NULL, GST_CLOCK_TIME_NONE);
    gst_object_unref (playbin);
}

static GstNvmResult
_media_load (GstNvmContextHandle handle, gchar *file_name)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    gboolean play_video= ctx->play_video;
    GstNvmResult gr;
    gchar handle_id[3]= {0};
    GstBus *bus;
    GstNvmResult result;
    gint flags;

    priv_data->duration = 0;
    priv_data->is_elementary = TRUE;
    gr = _get_absolute_path (file_name, priv_data->file_name);
    g_return_val_if_fail (IsSucceed (gr), gr);
    GST_INFO ("Got absolute path to media: %s for context %d",
              priv_data->file_name, ctx->id);

    /* SBC support in play mode */
    if (strstr (priv_data->file_name, ".sbc")) {
        sprintf (handle_id, "%d", ctx->id);
        ctx->pipeline = gst_pipeline_new (strcat ("player", handle_id));
        ctx->source = gst_element_factory_make ("filesrc", NULL);
        g_object_set (G_OBJECT (ctx->source), "location", priv_data->file_name + 7, NULL);
        ctx->audio_sink_alsa = gst_element_factory_make ("alsasink", NULL);
        ctx->audio_bin = gst_bin_new ("videosinkbin");
        ctx->audio_tee = gst_element_factory_make ("tee", NULL);
        gst_bin_add_many (GST_BIN (ctx->audio_bin), ctx->audio_tee, ctx->audio_sink_alsa, NULL);
        gst_element_link_many (ctx->audio_tee, ctx->audio_sink_alsa, NULL);
        priv_data->decodebin = gst_element_factory_make ("sbcdec", NULL);

        if (priv_data->decodebin) {
            gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->source,
                                priv_data->decodebin, ctx->audio_bin, NULL);
            gst_element_link_many (ctx->source, priv_data->decodebin,
                                    ctx->audio_bin, NULL);
        } else {
            GST_ERROR ("Not able to get sbcdec element. Check plugin installation");
            gst_object_unref (GST_OBJECT (ctx->audio_bin));
            gst_object_unref (GST_OBJECT (ctx->source));
        }
    }

    _media_parse_input_uri (ctx, priv_data->file_name);

    if (priv_data->use_decodebin_flag) {
        GST_DEBUG ("Building decodebin pipeline");
        _media_build_custom_pipeline (ctx);
    } else {
        GST_DEBUG ("Initializing Sinks");
        result = _media_initialize_sinks (ctx);
        if (IsFailed (result)) {
            GST_ERROR ("Sinks initialization failed with error %d", result);
            return result;
        }

        /* Create playbin pipeline*/
        GST_DEBUG ("Create Playbin Pipeline");
        ctx->pipeline = gst_element_factory_make ("playbin", NULL);
        g_object_set (GST_OBJECT (ctx->pipeline), "uri", priv_data->file_name, NULL);

        g_object_get (GST_OBJECT (ctx->pipeline), "flags", &flags, NULL);
        if (!strcasecmp (ctx->audio_dev, "NULL")) {
            GST_DEBUG ("Audio device set to NULL. No Audio Output");
            flags &= ~GST_PLAY_FLAG_AUDIO;
        }

        if (!play_video)
            flags &= ~GST_PLAY_FLAG_VIDEO;

        /* Set Audio/Video Sinks */
        g_object_set (GST_OBJECT (ctx->pipeline), "audio-sink", ctx->audio_bin, NULL);
        g_object_set (GST_OBJECT (ctx->pipeline), "video-sink", ctx->video_bin, NULL);
        g_object_set (GST_OBJECT (ctx->pipeline), "flags", flags, NULL);
    }

    /* Add a bus to watch for messages */
    bus = gst_pipeline_get_bus (GST_PIPELINE (ctx->pipeline));
    ctx->source_id = gst_bus_add_watch (bus, (GstBusFunc) _media_bus_callback, ctx);

    gst_bus_enable_sync_message_emission (bus);
    g_signal_connect (bus, "sync-message::stream-status", (GCallback) _media_on_stream_status, NULL);
    gst_object_unref (GST_OBJECT (bus));

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_play_internal (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;

    if (IsFailed (gst_nvm_common_change_state (ctx, GST_STATE_READY, GST_NVM_WAIT_TIMEOUT)))
        return GST_NVM_RESULT_FAIL;
    if (IsFailed (gst_nvm_common_change_state (ctx, GST_STATE_PAUSED, GST_NVM_WAIT_TIMEOUT)))
        return GST_NVM_RESULT_FAIL;
    if (IsFailed (gst_nvm_common_change_state (ctx, GST_STATE_PLAYING, GST_NVM_WAIT_TIMEOUT)))
        return GST_NVM_RESULT_FAIL;

    g_mutex_lock (&priv_data->ptm_mutex);
    g_cond_signal (&priv_data->ptm_cond);
    g_mutex_unlock (&priv_data->ptm_mutex);

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_unload (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstState state;
    GST_DEBUG ("Unloading media");
    if (ctx->pipeline) {
        GST_DEBUG ("Releasing Pipeline...");
        if (IsFailed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
            gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);
        else {
            if (state == GST_STATE_PLAYING)
                gst_nvm_common_change_state (ctx, GST_STATE_PAUSED, GST_NVM_WAIT_TIMEOUT);

            if (IsFailed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
                gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);
            if (state == GST_STATE_PAUSED)
                gst_nvm_common_change_state (ctx, GST_STATE_READY, GST_NVM_WAIT_TIMEOUT);

            if (IsFailed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
                gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);
            if (state == GST_STATE_READY)
                gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);

            if (IsFailed (gst_nvm_common_query_state (ctx, &state, GST_NVM_WAIT_TIMEOUT)))
                gst_nvm_common_change_state (ctx, GST_STATE_NULL, GST_NVM_WAIT_TIMEOUT);
            if (state != GST_STATE_NULL)
                GST_ERROR ("Failed setting pipeline to NULL state while unloading media");
            else
                GST_DEBUG ("Pipeline was set to NULL state");
        }
        gst_object_unref (GST_OBJECT (ctx->pipeline));
        ctx->pipeline = NULL;
        GST_DEBUG ("Pipeline released");
    }

    if (ctx->source_id) {
        g_source_remove (ctx->source_id);
        ctx->source_id = 0;
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_set_audio_device (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *audio_device = ctx->last_command_param.string_value;

    // If audio device changed
    if(strcasecmp(ctx->audio_dev, audio_device)) {
        GST_DEBUG ("Setting audio device for context %d to %s", ctx->id, audio_device);
        strcpy (ctx->audio_dev, audio_device);
        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using audio channel %s", ctx->id, audio_device);
    return GST_NVM_RESULT_NOOP;
}

static GstNvmResult
_media_enable_media_msgs (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    gint value = ctx->last_command_param.int_value;

    g_return_val_if_fail (value == 0 || value == 1,
                          GST_NVM_RESULT_INVALID_ARGUMENT);

    GST_DEBUG ("%s media messages for context %d...",
               value == 1 ? "Enabling" : "Disabling" , ctx->id);
    priv_data->debug_log_flag = value;

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_enable_ptm (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    gint value = ctx->last_command_param.int_value;

    g_return_val_if_fail (value == 0 || value == 1,
                          GST_NVM_RESULT_INVALID_ARGUMENT);

    GST_DEBUG ("%s ptm messages for context %d...",
               value == 1 ? "Enabling" : "Disabling" , ctx->id);
    priv_data->ptm_flag = value;
    if (priv_data->ptm_flag) {
        g_mutex_lock (&priv_data->ptm_mutex);
        priv_data->ptm_flag = TRUE;
        g_cond_signal (&priv_data->ptm_cond);
        g_mutex_unlock (&priv_data->ptm_mutex);
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_use_decodebin (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    gint value = ctx->last_command_param.int_value;

    g_return_val_if_fail (value == 0 || value == 1,
                          GST_NVM_RESULT_INVALID_ARGUMENT);

    GST_DEBUG ("%s decodebin for context %d...",
               value == 1 ? "Enabling" : "Disabling" , ctx->id);
    priv_data->use_decodebin_flag = value;

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_media_downmix_audio (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gint value = ctx->last_command_param.int_value;

    g_return_val_if_fail (value == 0 || value == 1,
                          GST_NVM_RESULT_INVALID_ARGUMENT);

    if (value == 1) {
        GST_DEBUG ("Downmixing audio to stereo for context %d...",
                    ctx->id);
    }
    else {
        GST_DEBUG ("Disabling downmix option for context %d...",
                    ctx->id);
    }
    ctx->downmix_audio = value;

    return GST_NVM_RESULT_OK;
}

static gint64
_media_get_duration (GstNvmContext *ctx)
{
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    GstFormat format = GST_FORMAT_TIME;

    if (!ctx->pipeline)
        return 0;

    g_mutex_lock (&ctx->lock);

    if (!priv_data->duration &&
        !gst_element_query_duration (GST_ELEMENT (ctx->pipeline), format, &priv_data->duration))
            priv_data->duration = 0;

    g_mutex_unlock (&ctx->lock);

    return priv_data->duration;
}

void
gst_nvm_media_show_progress (gpointer data, gpointer user_data)
{
    GstNvmContext *ctx = (GstNvmContext *) data;
    GstNvmMediaData *priv_data = (GstNvmMediaData *) ctx->private_data;
    GstFormat format = GST_FORMAT_TIME;
    gint64 position, length;

    while (!ctx->quit_flag) {
        if (!priv_data->ptm_flag) {
            g_mutex_lock (&priv_data->ptm_mutex);
            while (!priv_data->ptm_flag)
                g_cond_wait (&priv_data->ptm_cond, &priv_data->ptm_mutex);
            g_mutex_unlock (&priv_data->ptm_mutex);
            continue;
        } else if (ctx->is_active) {
            g_mutex_lock(&ctx->gst_lock);
            length = _media_get_duration (ctx);
            position = 0;
            /* get the current position */
            if (!gst_element_query_position (ctx->pipeline, format, &position))
                position = 0;

            g_print ("OnPosition:  %lldms/%lldms\n",
                     ((long long int)position / 1000000), ((long long int)length / 1000000));

            g_mutex_unlock (&ctx->gst_lock);
        }
        /* spew for every five seconds */
        g_usleep (G_USEC_PER_SEC / 5);
    }
}
