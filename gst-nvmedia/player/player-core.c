/* Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <string.h>
#include "player-core.h"
#include "player-core-priv.h"
#ifdef NV_EGL_SINK
#include "eglconsumer.h"
#endif

typedef struct {
    GstNvmCommand     cmd;            // Instruction to the player
    GstNvmParameter   parameter;      // Used for some commands
} GstNvmQueueItem;

/* Static Functions */
static void _context_thread_func (gpointer data, gpointer user_data);

GstPlayerData s_player_data;

GstNvmResult
gst_nvm_player_init (
    gint max_instances)
{
    GST_DEBUG_CATEGORY_INIT (gst_nvm_player_debug, "gstnvmplayerdebug",
                             GST_DEBUG_FG_YELLOW, "GST NvMedia Player");
    s_player_data.quit = FALSE;
    s_player_data.max_instances = max_instances;
    s_player_data.worker_threads_pool = g_thread_pool_new (_context_thread_func,
                                                           &s_player_data,
                                                           s_player_data.max_instances,
                                                           TRUE,
                                                           NULL);
    s_player_data.progress_threads_pool = g_thread_pool_new (gst_nvm_media_show_progress,
                                                             &s_player_data,
                                                             s_player_data.max_instances,
                                                             TRUE,
                                                             NULL);
#ifdef NV_EGL_SINK
    gst_nvm_player_egl_init ();
#endif

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_fini (void)
{
    s_player_data.quit = TRUE;
    g_thread_pool_free (s_player_data.worker_threads_pool, TRUE, FALSE);
    g_thread_pool_free (s_player_data.progress_threads_pool, TRUE, FALSE);
#ifdef NV_EGL_SINK
    gst_nvm_player_egl_fini ();
#endif

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_open_handle (
    GstNvmContextHandle *handle,
    GstNvmContextType ctx_type,
    GstNvmDisplayDeviceInfo *display_device,
    gchar *audio_device,
    gchar *capture_config_file,
    gchar *usb_device)
{
    GstNvmContext *ctx;
    GstNvmResult result = GST_NVM_RESULT_OK;
    static gint handle_id = 0;

    ctx = (GstNvmContext *) g_malloc (sizeof (GstNvmContext));
    g_return_val_if_fail (ctx, GST_NVM_RESULT_OUT_OF_MEMORY);

    memset (ctx, 0, sizeof (GstNvmContext));
    ctx->timeout = 5;
    ctx->is_active = FALSE;
    ctx->type = ctx_type;
    ctx->id = handle_id++;
    ctx->command_queue = g_async_queue_new ();
    ctx->quit_flag = FALSE;
    g_mutex_init (&ctx->lock);
    ctx->wait_end_sem = gst_nvm_semaphore_init ();
    ctx->state_playing_sem = gst_nvm_semaphore_init ();
    ctx->egl_playback_start_sem = gst_nvm_semaphore_init ();
    ctx->egl_playback_stop_sem = gst_nvm_semaphore_init ();
    ctx->egl_playback_stop_flag = FALSE;
    g_mutex_init (&ctx->gst_lock);
    ctx->func_table = g_malloc (GST_NVM_COMMANDS_NUM * sizeof (GstNvmFunc));
    ctx->timeout_counter = 0;
    ctx->pipeline = NULL;
    ctx->source_id = 0;
    ctx->egl_initialized_flag = FALSE;
    ctx->egl_cross_process_enabled_flag = FALSE;
    ctx->downmix_audio = FALSE;
    ctx->play_video = TRUE;
    ctx->video_sink_overlay = NULL;
    strcpy(ctx->asrc_eth_iface,GST_NVM_DEFAULT_ETH_INTERFACE);
    ctx->asrc_vlan_prio = GST_NVM_DEFAULT_SRC_VLAN_PRIO;
    ctx->asrc_stream_id = GST_NVM_DEFAULT_SRC_STREAM_ID;
    strcpy(ctx->asink_eth_iface,GST_NVM_DEFAULT_ETH_INTERFACE);
    strcpy(ctx->asink_stream_id,GST_NVM_DEFAULT_STREAM_ID);

    if (display_device) {
        strncpy(ctx->display_type, display_device->name, GST_NVM_MAX_DEVICE_NAME);
        memcpy(&ctx->display_properties, display_device, sizeof (ctx->display_properties));
    }
    else {
        strcpy (ctx->display_type, "NULL");
        memset (&ctx->display_properties, 0, sizeof (ctx->display_properties));
    }

    if (audio_device)
        strcpy (ctx->audio_dev, audio_device);
    else
        strcpy (ctx->audio_dev, "NULL");

    ctx->vsink_type = (!strcmp (ctx->display_type, "egl")) ? GST_NVM_EGLSTREAM_SINK :
                      (!strcmp (ctx->display_type, "mirror-egl") ? GST_NVM_DUAL_EGL_OVERLAY_SINK :
                      GST_NVM_OVERLAY_SINK);
    *handle = ctx;

    switch (ctx_type) {
        case GST_NVM_PLAYER:
            result = gst_nvm_media_init (ctx);
            g_thread_pool_push (s_player_data.progress_threads_pool, ctx, NULL);
            break;
        case GST_NVM_CAPTURE:
            result = gst_nvm_capture_init (ctx, capture_config_file);
            break;
        case GST_NVM_USB_AUDIO:
            result = gst_nvm_usb_init (ctx, usb_device);
            break;
        case GST_NVM_AVB_SRC:
            result = gst_nvm_avb_src_init (ctx);
            break;
        case GST_NVM_AVB_SINK:
            result = gst_nvm_avb_sink_init (ctx);
            break;
        default:
            GST_ERROR ("Context type not supported");
    }

    g_thread_pool_push (s_player_data.worker_threads_pool, ctx, NULL);

    return result;
}

GstNvmResult
gst_nvm_player_close_handle (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext*) handle;

    g_mutex_clear (&ctx->lock);
    g_mutex_clear (&ctx->gst_lock);
    gst_nvm_semaphore_destroy (ctx->wait_end_sem);
    gst_nvm_semaphore_destroy (ctx->state_playing_sem);
    gst_nvm_semaphore_destroy (ctx->egl_playback_start_sem);
    gst_nvm_semaphore_destroy (ctx->egl_playback_stop_sem);

    switch (ctx->type) {
        case GST_NVM_PLAYER:
            gst_nvm_media_fini (ctx);
            break;
        case GST_NVM_CAPTURE:
            gst_nvm_capture_fini (ctx);
            break;
        case GST_NVM_USB_AUDIO:
            gst_nvm_usb_fini (ctx);
            break;
        case GST_NVM_AVB_SRC:
            gst_nvm_avb_src_fini (ctx);
            break;
        case GST_NVM_AVB_SINK:
            gst_nvm_avb_sink_fini (ctx);
            break;
    }

    g_async_queue_unref (ctx->command_queue);
    g_free (ctx->func_table);
    g_free (ctx);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_send_command (
    GstNvmContextHandle handle,
    GstNvmCommand command,
    GstNvmParameter parameter)
{
    GstNvmQueueItem *queue_item = (GstNvmQueueItem *) g_malloc (sizeof (GstNvmQueueItem));
    GstNvmContext *ctx = (GstNvmContext *) handle;

    queue_item->cmd = command;
    queue_item->parameter = parameter;

    GST_DEBUG ("Enqueueing command id %d for player %d",
               queue_item->cmd, ctx->id);
    g_async_queue_push (ctx->command_queue, (gpointer) queue_item);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_start (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    if (ctx->is_active) {
        g_printf ("Player is in active mode. Please stop the playing media first using 'kv'/'ka'/'ku' command");
        return GST_NVM_RESULT_NOOP;
    }

    if (ctx->vsink_type != GST_NVM_OVERLAY_SINK) { // EGL or EGL mirroring
        // Pipeline should be created only after EGL initialized (this happens on
        // EGL thread in: _egl_thread_func)
        while (!ctx->egl_initialized_flag) {
            GST_DEBUG ("Player Context: Wait for EGL init");
            g_usleep (5000);
        }

    }

    switch (ctx->type) {
        case GST_NVM_PLAYER:
            gst_nvm_media_play(ctx);
            break;
        case GST_NVM_CAPTURE:
            gst_nvm_capture_start (ctx);
            break;
        case GST_NVM_USB_AUDIO:
            gst_nvm_usb_start (ctx);
            break;
        case GST_NVM_AVB_SRC:
            gst_nvm_avb_src_play(ctx);
            break;
        case GST_NVM_AVB_SINK:
            gst_nvm_avb_sink_play(ctx);
            break;
        default:
            break;
    }
#ifdef NV_EGL_SINK
    if ((ctx->eglsink_enabled_flag) && (!ctx->egl_cross_process_enabled_flag))
        gst_nvm_player_start_egl_playback (ctx);
#endif

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_stop (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
#ifdef NV_EGL_SINK
    if ((ctx->eglsink_enabled_flag) && (!ctx->egl_cross_process_enabled_flag)) {
            gst_nvm_player_stop_egl_playback (ctx);
    }
#endif
    switch (ctx->type) {
        case GST_NVM_PLAYER:
            gst_nvm_media_stop (ctx);
            break;
        case GST_NVM_CAPTURE:
            gst_nvm_capture_stop (ctx);
            break;
        case GST_NVM_USB_AUDIO:
            gst_nvm_usb_stop (ctx);
            break;
        case GST_NVM_AVB_SRC:
            gst_nvm_avb_src_stop(ctx);
            break;
        case GST_NVM_AVB_SINK:
            gst_nvm_avb_sink_stop(ctx);
            break;
    }

    ctx->video_sink_overlay = NULL;

    return GST_NVM_RESULT_OK;
}

static void
_context_thread_func (
    gpointer data,
    gpointer user_data)
{
    GstNvmContext *ctx = (GstNvmContext *) data;
    GstNvmQueueItem *queue_item;
    GstNvmResult result = GST_NVM_RESULT_FAIL;

    while (!ctx->quit_flag) {
        queue_item = g_async_queue_pop (ctx->command_queue);
        ctx->last_command_param = queue_item->parameter;
        result = ctx->func_table[queue_item->cmd](ctx);
        if (IsFailed (result))
            GST_ERROR ("Execution of command %d on player :%d failed: %d",
                       queue_item->cmd, ctx->id, result);
        g_free (queue_item);
    }
    gst_nvm_player_close_handle (ctx);
}

GstNvmResult
gst_nvm_player_quit (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    GST_DEBUG ("Quiting Context %d...", ctx->id);
    if (ctx->eglsink_enabled_flag)
    {
        /* Close EGL */
        if (ctx->pipeline != NULL)
            gst_nvm_player_stop (handle);
        ctx->eglsink_enabled_flag = FALSE;
        ctx->egl_playback_stop_flag = TRUE;
        if (!ctx->egl_cross_process_enabled_flag) {
            gst_nvm_semaphore_signal (ctx->egl_playback_start_sem);
            gst_nvm_semaphore_wait (ctx->egl_playback_stop_sem);//Wait for EGL thread to stop
        }
    }
    ctx->quit_flag = TRUE;


    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_print_plugin_info (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    if (!ctx->pipeline) {
        GST_WARNING ("Couldn't get plugin info. No active playback found");
        return GST_NVM_RESULT_NOOP;
    }

    gst_nvm_common_get_plugin_info (ctx->pipeline, NULL);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_wait_on_end (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    if (ctx->type == GST_NVM_PLAYER)
        gst_nvm_media_wait_on_end (ctx);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_print_stream_info (
    GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    int num_streams;
    int current_stream;
    int i;
    gchar *str;
    GstTagList *tags = NULL;
    gint64 position = 0;

    if(!ctx->pipeline) {
        GST_WARNING ("Cannot print stream info. No active playback found");
        return GST_NVM_RESULT_NOOP;
    }

    /* get the current position */
    if (!gst_element_query_position (ctx->pipeline, GST_FORMAT_TIME, &position)) {
        GST_ERROR ("Curr position in the stream could not be retrieved");
        return GST_NVM_RESULT_FAIL;
    }

    g_print("Media Duration is %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (position));
    g_object_get(G_OBJECT(ctx->pipeline), "n-audio", &num_streams, NULL);
    g_object_get(G_OBJECT(ctx->pipeline), "current-audio", &current_stream, NULL);
    g_print ("Current Audio Stream : (%d)/(%d) \n",current_stream, num_streams);
    for (i = 0; i < num_streams; i++) {
        g_signal_emit_by_name (ctx->pipeline, "get-audio-tags", i, &tags);
        if (tags) {
            str = gst_structure_to_string ((GstStructure *) tags);
            g_print ("audio %d: %s\n", i, str);
            g_free (str);
        }
    }
    g_object_get (G_OBJECT (ctx->pipeline), "n-video", &num_streams, NULL);
    g_object_get (G_OBJECT (ctx->pipeline), "current-video", &current_stream, NULL);
    g_print ("Current Video Stream : (%d)/(%d) \n",current_stream, num_streams);
    for (i = 0;i < num_streams; i++) {
        g_signal_emit_by_name (ctx->pipeline, "get-video-tags", i, &tags);
        if (tags) {
            str = gst_structure_to_string ((GstStructure *) tags);
            g_print ("video %d: %s\n", i, str);
            g_free (str);
        }
    }

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_list_capture_param_sets (
    GstNvmContextHandle handle)
{
    return gst_nvm_capture_list_param_sets (handle);
}

static GstPadProbeReturn
event_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
    if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_DATA (info)) != GST_EVENT_EOS) {
        return GST_PAD_PROBE_OK;
    }

    gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

    return GST_PAD_PROBE_DROP;
}

static GstNvmResult
_player_connect_audio_bin (GstNvmContext *ctx)
{
    GstPad* sinkpad;
    GstPad* srcpad;

    if (GST_IS_BIN (ctx->audio_bin) && ctx->unlinked_audio_srcpad) {
        GST_DEBUG ("Connecting media bin in context %d", ctx->id);
        srcpad = gst_bin_find_unlinked_pad (GST_BIN (ctx->audio_bin), GST_PAD_SRC);
        sinkpad = gst_bin_find_unlinked_pad (GST_BIN (ctx->audio_bin), GST_PAD_SINK);
        GST_DEBUG ("srcpad %p sinkpad %p", srcpad, sinkpad);
        if (srcpad && sinkpad) {
            GST_DEBUG ("Linking srcpad and sinkpad");
            gst_pad_link (srcpad, sinkpad);
            gst_pad_remove_probe (ctx->unlinked_audio_srcpad, ctx->audio_probe_id);
            ctx->audio_probe_id = 0;
            gst_object_unref (srcpad);
            ctx->unlinked_audio_srcpad = 0;
            gst_object_unref (sinkpad);
        } else {
            if (srcpad)
                gst_object_unref (srcpad);
            ctx->unlinked_audio_srcpad = 0;
            if (sinkpad)
                gst_object_unref (sinkpad);
        }
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_player_disconnect_audio_bin (GstNvmContext *ctx)
{
    GstPad* sinkpad;
    GstPad* srcpad;

    if (GST_IS_BIN (ctx->audio_bin)) {
        GST_DEBUG ("Disconnecting media bin in context %d", ctx->id);
        sinkpad = gst_element_get_static_pad (ctx->audio_sink_alsa, "sink");
        if (sinkpad && gst_pad_is_linked (sinkpad)) {
            srcpad =  gst_pad_get_peer (sinkpad);
            ctx->audio_probe_id = gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
                            event_probe_cb, ctx, NULL);
            ctx->unlinked_audio_srcpad = srcpad;
            GST_DEBUG ("Unlinking srcpad %p sinkpad %p", srcpad, sinkpad);
            gst_pad_unlink (srcpad, sinkpad);
            gst_object_unref (srcpad);
            gst_object_unref (sinkpad);
        } else {
            /* videobin is already disconnected */
            gst_object_unref (sinkpad);
        }
    }

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_set_display_dynamic (
    GstNvmContextHandle handle,
    gchar *display_device,
    gint window_id)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    if (ctx->pipeline) {
        GST_DEBUG ("Setting display device for context %d to %s", ctx->id, display_device);
        if (strcasecmp (display_device, "egl") && strcasecmp (display_device, "mirror-egl")) { // Changing to overlay type (from egl or other overlay display)
            /* Set overlay display*/
            if (!strcasecmp (display_device, "display-0"))
                g_object_set (G_OBJECT (ctx->video_sink_overlay), "display-device", 0, NULL);
            else if (!strcasecmp (display_device, "display-1"))
                g_object_set (G_OBJECT (ctx->video_sink_overlay), "display-device", 1, NULL);
            else if (!strcasecmp (display_device, "dual"))
                g_object_set (G_OBJECT (ctx->video_sink_overlay), "display-device", 2, NULL);
            else if (!strcasecmp (display_device, "NULL"))
                g_object_set (G_OBJECT (ctx->video_sink_overlay), "display-device", 3, NULL);

            /* Set window id */
            ctx->display_properties.window_id = window_id;
            g_object_set (G_OBJECT (ctx->video_sink_overlay), "window-id", ctx->display_properties.window_id, NULL);
        } else if (!strcasecmp (display_device, "egl") && ctx->vsink_type == GST_NVM_OVERLAY_SINK) { // Moving to egl
            GST_DEBUG ("Enabling egl");
            g_object_set (G_OBJECT (ctx->video_sink_overlay), "display-device", 3, NULL);
        } else if (!strcasecmp (display_device, "egl") && ctx->vsink_type != GST_NVM_OVERLAY_SINK) { // Moving from mirroring mode (overlay + egl) to egl
            GST_DEBUG ("Disabling overlay");
            g_object_set (G_OBJECT (ctx->video_sink_overlay), "display-device", 3, NULL);
        } else if (!strcasecmp (display_device, "mirror-egl") && ctx->vsink_type == GST_NVM_OVERLAY_SINK) { // Adding mirroring on egl
            GST_DEBUG ("Enabling egl");
        }

        if (ctx->display_properties.depth_specified)
            g_object_set (G_OBJECT (ctx->video_sink_overlay), "depth", ctx->display_properties.depth, NULL);
        if (ctx->display_properties.position_specified)
            g_object_set (G_OBJECT (ctx->video_sink_overlay), "position", ctx->display_properties.position_str, NULL);
    }
    strcpy (ctx->display_type, display_device);
    ctx->vsink_type = (!strcasecmp (display_device, "egl")) ? GST_NVM_EGLSTREAM_SINK :
                      (!strcasecmp (display_device, "mirror-egl") ? GST_NVM_DUAL_EGL_OVERLAY_SINK :
                      GST_NVM_OVERLAY_SINK);
    return GST_NVM_RESULT_OK;
}

static void
_player_audio_bin_unlinked_cb (GstPad  *pad,
                               GstPad  *peer,
                               gpointer user_data)
{
    GstNvmContext *ctx = (GstNvmContext *) user_data;

    // Without the following sleep, stream hangs
    g_usleep (G_USEC_PER_SEC / 5);
    ctx->is_audio_srcpad_unlinked = TRUE;
}

GstNvmResult
gst_nvm_player_set_audio_channel_dynamic (GstNvmContextHandle handle,
                                          gchar *audio_channel)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstState state = GST_STATE_NULL;

    if (strcasecmp (ctx->audio_dev, audio_channel)) {
        strcpy (ctx->audio_dev, audio_channel);
        if (ctx->pipeline) {
            /* Connect to the pad-added signal */
            g_signal_connect (gst_element_get_static_pad (ctx->audio_sink_alsa, "sink"), "unlinked", G_CALLBACK (_player_audio_bin_unlinked_cb), ctx);
            ctx->is_audio_srcpad_unlinked = FALSE;
            _player_disconnect_audio_bin (ctx);
            /* Wait for pad unlinked signal emition */
            while (!ctx->is_audio_srcpad_unlinked) {
                // Wait for audio bin disconnection
            }
            GST_DEBUG ("Setting audio device for context %d to %s", ctx->id, ctx->audio_dev);
            gst_element_get_state (ctx->pipeline, &state, NULL, GST_NVM_WAIT_TIMEOUT);
            gst_element_set_state (ctx->audio_sink_alsa, GST_STATE_NULL);
            g_object_set (G_OBJECT (ctx->audio_sink_alsa), "device", ctx->audio_dev, NULL);
            if (state == GST_STATE_READY || state == GST_STATE_PAUSED || state == GST_STATE_PLAYING)
                gst_element_set_state (ctx->audio_sink_alsa, GST_STATE_READY);
            if (state == GST_STATE_PAUSED || state == GST_STATE_PLAYING)
                gst_element_set_state (ctx->audio_sink_alsa, GST_STATE_PAUSED);
            if (state == GST_STATE_PLAYING)
                gst_element_set_state (ctx->audio_sink_alsa, GST_STATE_PLAYING);
            _player_connect_audio_bin (ctx);

            return GST_NVM_RESULT_OK;
        }
    }

    return GST_NVM_RESULT_NOOP;
}

GstNvmResult
gst_nvm_player_set_display_window_id (GstNvmContextHandle handle,
                                      gint window_id)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    GST_DEBUG ("Setting display window id to %d", window_id);

    ctx->display_properties.window_id = window_id;

    if(ctx->pipeline && ctx->video_sink_overlay)
        g_object_set (G_OBJECT (ctx->video_sink_overlay), "window-id", window_id, NULL);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_set_display_window_depth (GstNvmContextHandle handle,
                                         gint depth)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;

    g_return_val_if_fail (depth >=0 && depth <= 255, GST_NVM_RESULT_FAIL);
    GST_DEBUG ("Setting display window depth to %d", depth);

    ctx->display_properties.depth_specified = TRUE;
    ctx->display_properties.depth = depth;

    if(ctx->pipeline && ctx->video_sink_overlay)
        g_object_set (G_OBJECT (ctx->video_sink_overlay), "depth", depth, NULL);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_player_set_display_window_position (GstNvmContextHandle handle,
                                            gint x0,
                                            gint y0,
                                            gint width,
                                            gint height)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar position_str[GST_NVM_MAX_STRING_SIZE] = {0};

    g_return_val_if_fail (x0 >= 0 && y0 >= 0 && width >= 0 && height >= 0, GST_NVM_RESULT_FAIL);

    sprintf (position_str, "%d:%d:%d:%d", x0, y0, x0 + width, y0 + height);
    GST_DEBUG ("Setting display window position to %s", position_str); // position has the structure: x0,y0,x1,y1

    ctx->display_properties.position_specified = TRUE;
    strcpy (ctx->display_properties.position_str, position_str);
    ctx->display_properties.position.x0 = x0;
    ctx->display_properties.position.y0 = y0;
    ctx->display_properties.position.width = width;
    ctx->display_properties.position.height = height;

    if(ctx->pipeline && ctx->video_sink_overlay)
        g_object_set (G_OBJECT (ctx->video_sink_overlay), "position", ctx->display_properties.position_str, NULL);

    return GST_NVM_RESULT_OK;
}

GstNvmSemaphore*
gst_nvm_semaphore_init (void)
{
    GstNvmSemaphore *sem;

    sem = (GstNvmSemaphore *) g_malloc (sizeof (GstNvmSemaphore));
    g_assert (sem != NULL);
    g_mutex_init (&sem->lock);
    sem->flag = FALSE;
    g_cond_init (&sem->cond);

    return sem;
}

GstNvmResult
gst_nvm_semaphore_wait (GstNvmSemaphore *sem)
{
   g_mutex_lock (&sem->lock);
   while (!sem->flag)
       g_cond_wait (&sem->cond, &sem->lock);
   sem->flag = FALSE;
   g_mutex_unlock (&sem->lock);

   return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_semaphore_signal (GstNvmSemaphore *sem)
{
   g_mutex_lock (&sem->lock);
   sem->flag = TRUE;
   g_cond_signal (&sem->cond);
   g_mutex_unlock (&sem->lock);

   return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_semaphore_destroy (GstNvmSemaphore *sem)
{
   g_mutex_clear (&sem->lock);
   g_cond_clear (&sem->cond);
   g_free (sem);

   return GST_NVM_RESULT_OK;
}
