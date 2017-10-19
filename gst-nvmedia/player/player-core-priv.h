/* Copyright (c) 2013-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __PLAYER_CORE_PRIV_H__
#define __PLAYER_CORE_PRIV_H__

#include "player-core.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gmain.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "grutil.h"
#include "device-map.h"
#include "result.h"

#define GST_NVM_WAIT_TIMEOUT    3000

GST_DEBUG_CATEGORY_EXTERN (gst_nvm_player_debug);
#define GST_CAT_DEFAULT gst_nvm_player_debug

typedef GstNvmResult (* GstNvmFunc) (GstNvmContextHandle handle);

typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0),
  GST_PLAY_FLAG_AUDIO         = (1 << 1),
  GST_PLAY_FLAG_TEXT          = (1 << 2),
  GST_PLAY_FLAG_VIS           = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME   = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO  = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO  = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD      = (1 << 7),
  GST_PLAY_FLAG_BUFFERING     = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE   = (1 << 9),
  GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10)
} GstPlayFlags;

typedef enum {
    GST_NVM_OVERLAY_SINK            = 0,
    GST_NVM_EGLSTREAM_SINK          = 1,
    GST_NVM_DUAL_EGL_OVERLAY_SINK   = 2
} GstNvmVideoSinkType;

typedef enum {
    GST_NVM_EGL_CONSUMER_GL_YUV = 0,
    GST_NVM_EGL_CONSUMER_GL_RGB,
    GST_NVM_EGL_CONSUMER_CUDA_YUV,
    GST_NVM_EGL_CONSUMER_CUDA_RGB
} GstNvmEglConsumerType;

typedef struct {
    gint         quit;
    gint         max_instances;
    GThreadPool *worker_threads_pool;
    GThreadPool *progress_threads_pool;
    GThreadPool *egl_threads_pool;
    GThreadPool *egl_cuda_threads_pool;
} GstPlayerData;

/* Context Interface Declaration */
typedef struct {
    GstNvmFunc             *func_table;
    void                   *private_data;               // Context Specific Data
    GstNvmContextType       type;
    guint                   id;
    GAsyncQueue            *command_queue;
    GstNvmParameter         last_command_param;
    GstNvmSemaphore        *wait_end_sem;
    GstNvmSemaphore        *state_playing_sem;
    GstNvmSemaphore        *egl_playback_start_sem;
    GstNvmSemaphore        *egl_playback_stop_sem;
    GstElement             *pipeline;
    GstNvmVideoSinkType     vsink_type;
    GstNvmEglConsumerType   egl_consumer_type;
    GstNvmDisplayDeviceInfo display_properties;
    gint                    asrc_vlan_prio;
    gint                    asrc_stream_id;
    gchar                   display_type[GST_NVM_MAX_DEVICE_NAME]; // Needed for holding the special display types: mirror-egl, dual
    gchar                   audio_dev[GST_NVM_MAX_DEVICE_NAME];
    gchar                   file_name[GST_NVM_MAX_DEVICE_NAME]; //File name specified during "da"
    gchar                   asrc_eth_iface[GST_NVM_MAX_DEVICE_NAME];
    gchar                   asink_eth_iface[GST_NVM_MAX_DEVICE_NAME];
    gchar                   asink_stream_id[GST_NVM_MAX_DEVICE_NAME];
    gboolean                quit_flag;
    gboolean                eglsink_enabled_flag;
    gboolean                egl_cross_process_enabled_flag;
    gboolean                egl_playback_stop_flag;
    gboolean                downmix_audio;
    gboolean                play_video;
    GMutex                  lock;
    GMutex                  gst_lock;
    guint                   timeout;
    guint                   timeout_counter;
    gulong                  audio_probe_id;
    gulong                  video_probe_id_overlay;
    gulong                  video_probe_id_egl;
    GstElement             *video_tee;
    GstElement             *audio_tee;
    GstElement             *video_bin;                      // video bin
    GstElement             *video_sink_overlay;             // video sink lvds
    GstElement             *video_sink_egl;                 // video sink hdmi
    GstElement             *audio_bin;                      // audio sink bin
    GstElement             *audio_convert;                  // audio converter
    GstElement             *caps_filter;                    // caps filter
    GstElement             *audio_sink_alsa;                // alsa sink
    GstElement             *avbappsrc;                      // avb appsrc
    GstElement             *avbappsink;                     // avb appsink
    GstPad                 *ghost_pad_video;                // ghost pad for video sink bin
    GstPad                 *ghost_pad_audio;                // ghost pad for audio sink bin
    GstPad                 *unlinked_audio_srcpad;          // Nedded for disconnecting and connecting back media bin
    gboolean                is_audio_srcpad_unlinked;       // Nedded for disconnecting and connecting back media bin
    gboolean                is_video_srcpad_unlinked;       // Nedded for disconnecting and connecting back media bin
    gboolean                is_fps_profiling;               // Needed to check if fps display is enabled
    GstElement             *source;                         // file source elementary
    guint                   source_id;                      // bus watch source-id
    gboolean                is_active;
    gboolean                egl_initialized_flag;
    gboolean                is_sink_file;                   // Needed to check if file sink
    gboolean                is_avb_src;
    gboolean                is_avb_sink;
    GstPad                 *video_tee_src_pad_ov;
    GstPad                 *video_tee_src_pad_egl;
    GstPad                 *sink_pad_ov;
    GstPad                 *sink_pad_egl;
    guint                   bus_watch_id;
} GstNvmContext;

extern gboolean script_mode;
extern GstPlayerData s_player_data;

/* Common functions for Capture and Media Player */

GstNvmResult
gst_nvm_common_set_display (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_common_query_state (
    GstNvmContext *ctx,
    GstState *curr_state,
    gint32 timeout_ms);

GstNvmResult
gst_nvm_common_change_state (
    GstNvmContext *ctx,
    GstState new_state,
    gint32 timeout_ms);

GstNvmResult
gst_nvm_common_set_brightness (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_common_set_contrast (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_common_set_saturation (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_common_wait_time (
    GstNvmContextHandle handle);

gboolean
gst_nvm_common_timeout_callback (
    GstNvmContext* ctx);

GstNvmResult
gst_nvm_common_get_plugin_info (
    GstElement *element,
    GQueue* queue);

GstNvmResult
gst_nvm_common_set_display_window_id (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_common_set_display_window_depth (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_common_set_display_window_position (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_common_print_msg (
    GstNvmContextHandle handle);

/* Specific functions for Media Player */

GstNvmResult
gst_nvm_media_init (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_media_fini (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_media_play (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_media_stop (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_media_wait_on_end (
    GstNvmContextHandle handle);

void
gst_nvm_media_show_progress (
    gpointer data,
    gpointer user_data); // PTM query thread function

/* Common functions for Capture */

GstNvmResult
gst_nvm_capture_init (
    GstNvmContextHandle handle,
    gchar *capture_config_file);

GstNvmResult
gst_nvm_capture_fini (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_capture_start (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_capture_stop (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_capture_list_param_sets (
    GstNvmContextHandle handle);

/* Common functions for USB */

GstNvmResult
gst_nvm_usb_init (
    GstNvmContextHandle handle,
    gchar *usb_device);

GstNvmResult
gst_nvm_usb_fini (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_usb_start (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_usb_stop (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_usb_switch (
    GstNvmContextHandle handle);

/* Common functions for AVB SRC */

GstNvmResult
gst_nvm_avb_src_init (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_avb_src_fini (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_avb_src_play (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_avb_src_stop (
    GstNvmContextHandle handle);

/* Common functions for AVB SINK */

GstNvmResult
gst_nvm_avb_sink_init (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_avb_sink_fini (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_avb_sink_play (
    GstNvmContextHandle handle);

GstNvmResult
gst_nvm_avb_sink_stop (
    GstNvmContextHandle handle);

#ifdef __cplusplus
}
#endif

#endif
