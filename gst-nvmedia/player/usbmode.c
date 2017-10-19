/* Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
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
#include "androida4a.h"

/* USB Specific Data */
typedef struct {
    gboolean is_device_set;
    GMutex   dev_mutex;
} GstNvmUsbData;

/* Static Functions */
static GstNvmResult _usb_reset (GstNvmContextHandle handle);
static GstNvmResult _usb_set_device (GstNvmContextHandle handle, gchar *usb_device);
static GstNvmResult _usb_load  (GstNvmContextHandle handle);
static GstNvmResult _usb_unload (GstNvmContextHandle handle);

/* Command Execution Functions */
static GstNvmResult _usb_set_audio_device (GstNvmContextHandle handle);

static GstNvmResult _usb_set_device (GstNvmContextHandle handle, gchar *usb_device)
{
   GstNvmContext *ctx = (GstNvmContext *) handle;
   GstNvmUsbData *priv_data = (GstNvmUsbData *) ctx->private_data;
   GstNvmResult ret;

   if (usb_device) {
        if (!strcmp (usb_device, "Android")) {
            GST_DEBUG ("Setting up Android device in accessory mode...");
            ret = androida4a();
            if (IsFailed(ret)) {
                GST_ERROR ("Accessory not connected");
                priv_data->is_device_set = FALSE;
                return GST_NVM_RESULT_FAIL;
            }
        } else if (!strcmp (usb_device, "iOS")) {
            GST_DEBUG ("Setting up Apple device in accessory mode...");
            ret = GST_NVM_RESULT_FAIL; //iossetup();
            if (IsFailed(ret)) {
                GST_ERROR ("Accessory not connected");
                priv_data->is_device_set = FALSE;
                return GST_NVM_RESULT_FAIL;
            }
        } else {
            GST_ERROR ("Operating System not supported");
            priv_data->is_device_set = FALSE;
            return GST_NVM_RESULT_FAIL;
        }
    } else {
          priv_data->is_device_set = FALSE;
          GST_ERROR ("Device not specified");
          return GST_NVM_RESULT_FAIL;
    }
   priv_data->is_device_set = TRUE;
   return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_usb_init (GstNvmContextHandle handle, gchar *usb_device)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmUsbData *priv_data = (GstNvmUsbData *) g_malloc (sizeof (GstNvmUsbData));
    GstNvmResult ret;

    priv_data->is_device_set = FALSE;
    g_mutex_init (&priv_data->dev_mutex);
    ctx->private_data = priv_data;

    ctx->func_table[GST_NVM_CMD_PLAY]                        = gst_nvm_player_start;
    ctx->func_table[GST_NVM_CMD_PAUSE_RESUME]                = NULL;
    ctx->func_table[GST_NVM_CMD_STOP]                        = gst_nvm_player_stop;
    ctx->func_table[GST_NVM_CMD_QUIT]                        = gst_nvm_player_quit;
    ctx->func_table[GST_NVM_CMD_USB_SWITCH]                  = gst_nvm_usb_switch;
    ctx->func_table[GST_NVM_CMD_WAIT_TIME]                   = NULL;
    ctx->func_table[GST_NVM_CMD_WAIT_END]                    = NULL;
    ctx->func_table[GST_NVM_CMD_SET_POSITION]                = NULL;
    ctx->func_table[GST_NVM_CMD_SET_SPEED]                   = NULL;
    ctx->func_table[GST_NVM_CMD_SET_BRIGHTNESS]              = NULL;
    ctx->func_table[GST_NVM_CMD_SET_CONTRAST]                = NULL;
    ctx->func_table[GST_NVM_CMD_SET_SATURATION]              = NULL;
    ctx->func_table[GST_NVM_CMD_SET_AUDIO_DEVICE]            = NULL;
    ctx->func_table[GST_NVM_CMD_SET_AUDIO_DEVICE_USB]        = _usb_set_audio_device;
    ctx->func_table[GST_NVM_CMD_SET_VIDEO_DEVICE]            = NULL;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_ID]       = NULL;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_DEPTH]    = NULL;
    ctx->func_table[GST_NVM_CMD_SET_DISPLAY_WINDOW_POSITION] = NULL;
    ctx->func_table[GST_NVM_CMD_SPEW_PTM_VALUES]             = NULL;
    ctx->func_table[GST_NVM_CMD_SPEW_MSGS]                   = NULL;
    ctx->func_table[GST_NVM_CMD_USE_DECODEBIN]               = NULL;
    ctx->func_table[GST_NVM_CMD_DOWNMIX_AUDIO]               = NULL;
    ctx->func_table[GST_NVM_CMD_ECHO_MSG]                    = NULL;
    ctx->func_table[GST_NVM_CMD_CAPTURE_MODE]                = NULL;
    ctx->func_table[GST_NVM_CMD_CAPTURE_CRC_FILE]            = NULL;

    ret = _usb_set_device (handle, usb_device);
    if (IsFailed (ret))
        return GST_NVM_RESULT_FAIL;
    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_usb_load (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstBus *bus;
    GstElement  *queue;

    GST_DEBUG ("Creating pipeline for USB playback");
    ctx->pipeline = gst_pipeline_new ("player");
    g_assert (ctx->pipeline);
    if (!ctx->pipeline) {
        GST_ERROR ("Couldn't create new pipeline");
        return GST_NVM_RESULT_FAIL;
    }
    gchar *audio_src = ctx->last_command_param.string_value;
    ctx->source = gst_element_factory_make ("alsasrc", "alsasrc");
    g_object_set (ctx->source, "device", audio_src, NULL);
    if (!ctx->source) {
        GST_ERROR ("Couldn't create alsa source");
        return GST_NVM_RESULT_FAIL;
    }

    queue = gst_element_factory_make("queue", "queue for usb");
    if (!queue) {
        GST_ERROR ("Couldn't create queue");
        return GST_NVM_RESULT_FAIL;
    }

    if (ctx->is_sink_file == TRUE) {
        ctx->audio_sink_alsa = gst_element_factory_make ("filesink", NULL);
        /* Saved as an audio_sink_alsa element only for simplicity purpose.
           Will still be saved as a file only */
        g_object_set (GST_OBJECT (ctx->audio_sink_alsa),
                     "location", ctx->file_name, NULL);
    }
    else {
        ctx->audio_sink_alsa = gst_element_factory_make ("alsasink", "alsasink");
        g_assert (ctx->audio_sink_alsa);
        g_object_set (G_OBJECT (ctx->audio_sink_alsa), "device", ctx->audio_dev, NULL);
        g_object_set (ctx->audio_sink_alsa, "sync", FALSE, NULL);
    }

    if (!ctx->audio_sink_alsa) {
        GST_ERROR ("Couldn't create audio sink");
        return GST_NVM_RESULT_FAIL;
    }

    gst_bin_add_many (GST_BIN (ctx->pipeline), ctx->source, queue, ctx->audio_sink_alsa, NULL);
    if (!ctx->pipeline) {
        GST_ERROR ("Pipeliene not created");
        return GST_NVM_RESULT_FAIL;
    }

    gst_element_link_many (ctx->source, queue, ctx->audio_sink_alsa, NULL);

    bus = gst_pipeline_get_bus (GST_PIPELINE (ctx->pipeline));

    if (!bus) {
        GST_ERROR ("Could not create bus for pipeline");
        return GST_NVM_RESULT_FAIL;
    }
    GstStateChangeReturn ret;
    ret = gst_element_set_state (ctx->pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {
       GST_ERROR ("Couldn't change state ");
       return GST_NVM_RESULT_FAIL;
    }

    return GST_NVM_RESULT_OK;
}

static GstNvmResult
_usb_unload (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    GST_DEBUG ("Unloading usb");
    if (ctx->pipeline) {
        GST_DEBUG ("Releasing Pipeline...");
        gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
        gst_object_unref (GST_OBJECT (ctx->pipeline));
        ctx->pipeline = NULL;
        GST_DEBUG ("Pipeline released");
        if (ctx->source_id) {
            g_source_remove (ctx->source_id);
            ctx->source_id = 0;
        }
        return GST_NVM_RESULT_OK;
    } else {
       GST_ERROR ("No pipeline to release");
       return GST_NVM_RESULT_FAIL;
    }
}

static GstNvmResult
_usb_reset (GstNvmContextHandle handle)
{
     if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    GST_DEBUG ("Resetting usb");
    ctx->is_active = FALSE;

    _usb_unload (handle);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_usb_start (GstNvmContextHandle handle)
{
   if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    GstNvmUsbData *priv_data = (GstNvmUsbData *) ctx->private_data;
    GstNvmResult result;
    g_mutex_lock (&priv_data->dev_mutex);

    ctx->is_active = TRUE;
    if (priv_data->is_device_set) {
        result = _usb_load (ctx);
        g_mutex_unlock (&priv_data->dev_mutex);
    } else {
        g_mutex_unlock (&priv_data->dev_mutex);
        GST_ERROR ("No device to play from");
        return GST_NVM_RESULT_FAIL;
    }
    if (IsFailed (result)) {
        GST_DEBUG ("Play usb failed. Unloading usb...");
        _usb_unload (ctx);
    }

    return result;
}

GstNvmResult
gst_nvm_usb_stop (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;

    g_mutex_lock (&ctx->gst_lock);

    if (ctx->is_active) {
        GST_DEBUG ("Stopping usb in context %d...", ctx->id);
        _usb_reset (ctx);
    }

    g_mutex_unlock (&ctx->gst_lock);

    return GST_NVM_RESULT_OK;
}

GstNvmResult
gst_nvm_usb_fini (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }
   GstNvmContext *ctx = (GstNvmContext *) handle;
   GstNvmUsbData *priv_data = (GstNvmUsbData *) ctx->private_data;

    if (priv_data) {
        g_mutex_clear (&priv_data->dev_mutex);
        g_free (priv_data);
        priv_data = NULL;
        ctx->private_data = NULL;
        return GST_NVM_RESULT_OK;
    }
    else
        return GST_NVM_RESULT_FAIL;
}

GstNvmResult
gst_nvm_usb_switch (GstNvmContextHandle handle)
{
   if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }
   GstNvmContext *ctx = (GstNvmContext *) handle;
   gchar *usb_device = ctx->last_command_param.string_value;
   GstNvmResult ret;
   GstNvmUsbData *priv_data = (GstNvmUsbData *) ctx->private_data;

   g_mutex_lock (&priv_data->dev_mutex);
   GST_DEBUG ("Stopping the pipeline...");
   ret = gst_nvm_usb_stop (handle);
   if (IsFailed (ret)) {
       g_mutex_unlock (&priv_data->dev_mutex);
       GST_ERROR ("Couldn't release pipeline");
       return GST_NVM_RESULT_FAIL;
   }

   GST_DEBUG ("Setting up the device...");
   ret = _usb_set_device (handle, usb_device);
   g_mutex_unlock (&priv_data->dev_mutex);
   if (IsFailed (ret)) {
       GST_ERROR ("Couldn't set up device");
       return GST_NVM_RESULT_FAIL;
   }
   return GST_NVM_RESULT_OK;
}

static GstNvmResult
_usb_set_audio_device (GstNvmContextHandle handle)
{
    if (!handle) {
        GST_ERROR ("Handle not initialised");
        return GST_NVM_RESULT_FAIL;
    }

    GstNvmContext *ctx = (GstNvmContext *) handle;
    gchar *audio_device = ctx->last_command_param.string_value;

    // If audio device changed
    if(strcasecmp (ctx->audio_dev, audio_device)) {
        GST_DEBUG ("Setting audio device for context %d to %s", ctx->id, audio_device);
        strcpy (ctx->audio_dev, audio_device);
        return GST_NVM_RESULT_OK;
    }

    GST_DEBUG ("Context %d already using audio channel %s", ctx->id, audio_device);
    return GST_NVM_RESULT_NOOP;
}

