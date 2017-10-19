/* Copyright (c) 2013-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "player-core-priv.h"

GstNvmResult
gst_nvm_common_set_display (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *display_type = ctx->last_command_param.display_desc_value.display_type;
    gint window_id = ctx->last_command_param.display_desc_value.window_id;

    // If video device changed
    if (strcasecmp (ctx->display_type, display_type)) {
        GST_DEBUG ("Setting display device for context %d to %s", ctx->id, display_type);
        strcpy (ctx->display_type, display_type);
        ctx->vsink_type = (!strcasecmp (display_type, "egl")) ? GST_NVM_EGLSTREAM_SINK :
                          (!strcasecmp (display_type, "mirror-egl") ? GST_NVM_DUAL_EGL_OVERLAY_SINK :
                          GST_NVM_OVERLAY_SINK);

        ctx->display_properties.window_id = window_id;

        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using display device %s", ctx->id, display_type);
    return GST_NVM_RESULT_NOOP;
}

GstNvmResult
gst_nvm_common_query_state (
    GstNvmContext *ctx,
    GstState *curr_state,
    gint32 timeout_ms)
{
    GstStateChangeReturn ret;
    GstState state = GST_STATE_NULL;

    if (!ctx->pipeline)
        return GST_NVM_RESULT_FAIL;

    g_mutex_lock (&ctx->lock);

    ret = gst_element_get_state (ctx->pipeline, &state, NULL, timeout_ms * GST_MSECOND);
    if (ret == GST_STATE_CHANGE_SUCCESS) {
        g_mutex_unlock (&ctx->lock);
        *curr_state = state;
        return GST_NVM_RESULT_OK;
    }

    g_mutex_unlock (&ctx->lock);
    return GST_NVM_RESULT_FAIL;
}

GstNvmResult
gst_nvm_common_change_state (
    GstNvmContext *ctx,
    GstState new_state,
    gint32 timeout_ms)
{
    GstStateChangeReturn ret;
    GstState state = GST_STATE_NULL;
    gchar *gst_states[] = {"GST_STATE_VOID_PENDING", "GST_STATE_NULL", "GST_STATE_READY", "GST_STATE_PAUSED", "GST_STATE_PLAYING"};

    if (!ctx->pipeline)
        return GST_NVM_RESULT_FAIL;

    g_mutex_lock (&ctx->lock);

    gst_element_set_state (ctx->pipeline, new_state);
    ret = gst_element_get_state (ctx->pipeline, &state, NULL, timeout_ms * GST_MSECOND);
    if (ret != GST_STATE_CHANGE_SUCCESS)
        GST_ERROR ("Failed setting pipeline to state %s...", gst_states[new_state]);
    else
        GST_DEBUG ("Pipeline was set to state %s...", gst_states[new_state]);

    g_mutex_unlock (&ctx->lock);

    return ((ret == GST_STATE_CHANGE_SUCCESS) && (state == new_state)) ?
                                                    GST_NVM_RESULT_OK :
                                                    GST_NVM_RESULT_FAIL;
}

gboolean
gst_nvm_common_timeout_callback (GstNvmContext* ctx)
{
    if (ctx->timeout_counter >= ctx->timeout) {
        GST_DEBUG ("Stopping %s after %d secs timeout",
                    ctx->type == GST_NVM_PLAYER ? "video" : "capture",
                    ctx->timeout_counter);
        gst_nvm_player_quit (ctx);
        return FALSE;
    }

    ctx->timeout_counter++;
    return TRUE;
}

GstNvmResult
gst_nvm_common_set_brightness (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gfloat value = ctx->last_command_param.float_value;

    g_return_val_if_fail (value >= -0.5 && value <= 0.5,
                          GST_NVM_RESULT_INVALID_ARGUMENT);

    g_mutex_lock (&ctx->lock);
    if (ctx->video_sink_overlay) {
        GST_DEBUG ("Setting brightness for context %d to %lf", ctx->id, value);
        g_object_set (G_OBJECT (ctx->video_sink_overlay),"brightness", value ,NULL);
    } else {
        GST_WARNING ("Overlay sink not found to set brightness");
    }

    g_mutex_unlock (&ctx->lock);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_common_set_contrast (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gfloat value = ctx->last_command_param.float_value;

    g_return_val_if_fail (value >= 0.1 && value <= 2.0,
                          GST_NVM_RESULT_INVALID_ARGUMENT);

    g_mutex_lock (&ctx->lock);

    if (ctx->video_sink_overlay) {
        GST_DEBUG ("Setting contrast for context %d to %lf", ctx->id, value);
        g_object_set (G_OBJECT (ctx->video_sink_overlay),"contrast", value ,NULL);
    } else {
        GST_WARNING ("Overlay sink not found to set contrast");
    }

    g_mutex_unlock (&ctx->lock);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_common_set_saturation (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gfloat value = ctx->last_command_param.float_value;

    g_return_val_if_fail (value >= 0.1 && value <= 2.0,
                          GST_NVM_RESULT_INVALID_ARGUMENT);

    g_mutex_lock (&ctx->lock);

    if (ctx->video_sink_overlay) {
        GST_DEBUG ("Setting saturation for context %d to %lf", ctx->id, value);
        g_object_set (G_OBJECT(ctx->video_sink_overlay),"saturation", value ,NULL);
    } else {
        GST_WARNING ("Overlay sink not found to set saturation");
    }

    g_mutex_unlock (&ctx->lock);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_common_wait_time (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gfloat seconds = ctx->last_command_param.float_value;

    GST_DEBUG ("Setting context %d on wait for %lf seconds", ctx->id, seconds);
    g_usleep (seconds * G_USEC_PER_SEC);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_common_get_plugin_info (GstElement *element, GQueue* queue)
{
    GList* child;
    gchar* name;

    child = GST_BIN (element)->children;
    while (child != NULL) {
        if (GST_IS_BIN (child->data)) {
            gst_nvm_common_get_plugin_info (child->data, queue);
        } else {
            if (queue) {
                g_queue_push_tail (queue, gst_element_get_name (child->data));
            } else {
                name = gst_element_get_name (child->data);
                g_print ("%s\n", name);
                g_free (name);
            }
        }
        child = child->next;
    }

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_common_print_msg (GstNvmContextHandle handle)
{
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *message = ctx->last_command_param.string_value;

    g_printf ("%s\n", message);
    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_common_set_display_window_id (GstNvmContextHandle handle) {
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gint window_id = ctx->last_command_param.int_value;

    GST_DEBUG ("Setting context %d to use display window id: %d", ctx->id, window_id);
    return gst_nvm_player_set_display_window_id (handle, window_id);
}

GstNvmResult
gst_nvm_common_set_display_window_depth (GstNvmContextHandle handle) {
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gint depth = ctx->last_command_param.int_value;

    GST_DEBUG ("Setting context %d to use display window depth: %d", ctx->id, depth);
    return gst_nvm_player_set_display_window_depth (handle, depth);
}

GstNvmResult
gst_nvm_common_set_display_window_position (GstNvmContextHandle handle) {
    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *position = ctx->last_command_param.string_value;
    gint x0 = 0, y0 = 0, width = 0, height = 0;

    if (sscanf(position, "%d:%d:%d:%d", &x0, &y0, &width, &height) != 4) {
        GST_ERROR ("Failed setting window position. Malformed position was encountered: %s. Valid position input: x0:y0:width:heigh", position);
        return GST_NVM_RESULT_FAIL;
    }

    GST_DEBUG ("Setting context %d to use display window position: %s", ctx->id, position);
    return gst_nvm_player_set_display_window_position (handle, x0, y0, width, height);
}
