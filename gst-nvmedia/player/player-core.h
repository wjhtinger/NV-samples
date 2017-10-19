/* Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __PLAYER_CORE_H__
#define __PLAYER_CORE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gmain.h>
#include <gst/gst.h>
#include <stdio.h>
#include <unistd.h>
#include "common.h"
#include "device-map.h"
#include "result.h"

#define GST_NVM_MAX_STRING_SIZE         256
#define IsFailed(result)                result != GST_NVM_RESULT_OK && result != GST_NVM_RESULT_NOOP
#define IsSucceed(result)               result == GST_NVM_RESULT_OK

typedef gpointer GstNvmContextHandle;

typedef enum {
    GST_NVM_PLAYER = 0,
    GST_NVM_CAPTURE,
    GST_NVM_USB_AUDIO,
    GST_NVM_AVB_SRC,
    GST_NVM_AVB_SINK
} GstNvmContextType;

typedef enum {
    GST_NVM_CMD_PLAY = 0,                    // Has to be sent with a stream name (string value)
    GST_NVM_CMD_PAUSE_RESUME,                // No parameter needed
    GST_NVM_CMD_STOP,                        // No parameter needed
    GST_NVM_CMD_QUIT,                        // No parameter needed
    GST_NVM_CMD_USB_SWITCH,                  // Has to be send with a parameter: usb device (string value)
    GST_NVM_CMD_SET_ETHERNET_INTERFACE,      // Has to be send with a parameter: ethernet interface (string value)
    GST_NVM_CMD_SET_STREAM_ID,               // Has to be send with a parameter: stream id (string value)
    GST_NVM_CMD_SET_VLAN_PRIORITY,           // as to be send with a parameter: vlan priority (int value)
    GST_NVM_CMD_WAIT_TIME,                   // Has to be sent with a time to wait in seconds (float value)
    GST_NVM_CMD_WAIT_END,                    // No parameter needed
    GST_NVM_CMD_SET_POSITION,                // Has to be sent with a parameter: position in miliseconds (int value)
    GST_NVM_CMD_SET_SPEED,                   // Has to be sent with a parameter: speed (int value)
    GST_NVM_CMD_SET_BRIGHTNESS,              // Has to be sent with a parameter: brightness (float value)
    GST_NVM_CMD_SET_CONTRAST,                // Has to be sent with a parameter: contrast (float value)
    GST_NVM_CMD_SET_SATURATION,              // Has to be sent with a parameter: saturation (float value)
    GST_NVM_CMD_SET_AUDIO_DEVICE,            // Has to be sent with a parameter: device name (string value)
    GST_NVM_CMD_SET_AUDIO_DEVICE_USB,        // Has to be sent with a parameter: device name (string value)
    GST_NVM_CMD_SET_VIDEO_DEVICE,            // Has to be sent with a parameter: device name (string value)
    GST_NVM_CMD_SET_DISPLAY_WINDOW_ID,       // Has to be sent with a parameter: window id (int value)
    GST_NVM_CMD_SET_DISPLAY_WINDOW_DEPTH,    // Has to be sent with a parameter: window depth (int value)
    GST_NVM_CMD_SET_DISPLAY_WINDOW_POSITION, // Has to be sent with a parameter: window position (string "x0:x1:width:height")
    GST_NVM_CMD_SPEW_PTM_VALUES,             // Has to be sent with boolean parameter: [0/1] (int value)
    GST_NVM_CMD_SPEW_MSGS,                   // Has to be sent with boolean parameter: [0/1] (int value)
    GST_NVM_CMD_USE_DECODEBIN,               // Has to be sent with boolean parameter: [0/1] (int value)
    GST_NVM_CMD_DOWNMIX_AUDIO,               // Has to be sent with boolean parameter: [0/1] (int value)
    GST_NVM_CMD_ECHO_MSG,                    // Has to be sent with a message to be printed (string value)
    GST_NVM_CMD_CAPTURE_MODE,                // Has to be sent with a parameter: Capture mode [live/cb] (string value)
    GST_NVM_CMD_CAPTURE_CRC_FILE,            // Has to be sent with a parameter: CRC file location (string value)
    GST_NVM_COMMANDS_NUM
} GstNvmCommand;

typedef struct {
    gchar  display_type[GST_NVM_MAX_STRING_SIZE];
    gint   window_id;
} GstNvmDisplayParameter;

typedef struct {
    guint    stream_width;
    guint    stream_height;
    gchar    socket_path[GST_NVM_MAX_STRING_SIZE];
    gboolean fifo_mode;
    guint    surface_type;
    gboolean low_latency;
} GstNvmAvbParameter;

typedef struct {
    gint    int_value;
    gfloat  float_value;
    gchar   string_value[GST_NVM_MAX_STRING_SIZE];
    GstNvmDisplayParameter    display_desc_value;
    GstNvmAvbParameter        avb_params;
} GstNvmParameter;

typedef struct {
    gboolean  flag;
    GMutex    lock;
    GCond     cond;
} GstNvmSemaphore;

//  gst_nvm_semaphore_init
//
//    gst_nvm_semaphore_init()  Creates the semaphore and allocates necessary resources

GstNvmSemaphore*
gst_nvm_semaphore_init (
    void);

//  gst_nvm_semaphore_wait
//
//    gst_nvm_semaphore_wait()  Blocks the threads till the flag is set
//
//  Arguments:
//
//   sem
//      (in) pointer to the semaphore

GstNvmResult
gst_nvm_semaphore_wait (
    GstNvmSemaphore *sem);

//  gst_nvm_semaphore_signal
//
//    gst_nvm_semaphore_signal()  Sets the flag and signals the threads
//
//  Arguments:
//
//   sem
//      (in) pointer to the semaphore

GstNvmResult
gst_nvm_semaphore_signal (
    GstNvmSemaphore *sem);

//  gst_nvm_semaphore_destroy
//
//    gst_nvm_semaphore_destroy()  Releases the resources needed by the semaphore
//                                 and frees it
//
//  Arguments:
//
//   sem
//      (in) pointer to the semaphore

GstNvmResult
gst_nvm_semaphore_destroy (
    GstNvmSemaphore *sem);

//  gst_nvm_player_init
//
//    gst_nvm_player_init()  Create a player context and allocate needed resources
//
//  Arguments:
//
//   max_instances
//      (in) Max number of instances to be created in this app context

GstNvmResult
gst_nvm_player_init (
    gint max_instances);

//  gst_nvm_player_fini
//
//    gst_nvm_player_fini()  Releasing the resources used by the player
//

GstNvmResult
gst_nvm_player_fini (
    void);

//  gst_nvm_player_open_handle
//
//    gst_nvm_player_open_handle()  Creates a new instance of type ctx_type.
//
//  Arguments:
//
//   handle
//      (out) pointer to created player instance context handle
//
//   ctx_type
//      (in) Context type. Available types are GST_NVM_PLAYER and GST_NVM_CAPTURE
//
//   display_device
//     (in) display_device to be used
//
//   audio_device
//      (in) display_device to be used
//
//   capture_config_file
//      (in) capture configuration file to be used (if starting capture context)

GstNvmResult
gst_nvm_player_open_handle (
    GstNvmContextHandle *handle,
    GstNvmContextType ctx_type,
    GstNvmDisplayDeviceInfo *display_device,
    gchar *audio_device,
    gchar *capture_config_file,
    gchar *usb_device);

//  gst_nvm_player_close_handle
//
//    gst_nvm_player_close_handle()  Releasing resources used by a player instance
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_close_handle (
    GstNvmContextHandle handle);

//  gst_nvm_player_send_command
//
//    gst_nvm_player_send_command()  Send command to specific player instance
//                                   Commands are queued and handled in FIFO order
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context
//   command
//      (in) Command type
//   parameter
//      (in) Optional command parameter. Not all commands need additional data.

GstNvmResult
gst_nvm_player_send_command (
    GstNvmContextHandle handle,
    GstNvmCommand command,
    GstNvmParameter parameter);

/* Following two function are supplied for out of order handling */

//  gst_nvm_player_start
//
//    gst_nvm_player_start()  Starts playing media or capture. If needed,
//                            initializes EGL.
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_start (
    GstNvmContextHandle handle);

//  gst_nvm_player_stop
//
//    gst_nvm_player_stop()  Stop player instance and reset all instance data
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_stop (
    GstNvmContextHandle handle);

//  gst_nvm_player_quit
//
//    gst_nvm_player_quit()  Kill player instance. Release resources used by
//                           this instance.
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_quit (
    GstNvmContextHandle handle);

//  gst_nvm_player_wait_on_end
//
//    gst_nvm_player_wait_on_end()  Wait for the end of the running stream before
//                                  handling nect queued command
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_wait_on_end (
    GstNvmContextHandle handle);

//  gst_nvm_player_print_plugin_info
//
//    gst_nvm_player_print_plugin_info()  Print used plugins
//                                        Can be used only while media is playing
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_print_plugin_info (
    GstNvmContextHandle handle);

//  gst_nvm_player_print_stream_info
//
//    gst_nvm_player_print_stream_info()  Print stream info
//                                        Can be used only while media is playing
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_print_stream_info (
    GstNvmContextHandle handle);

//  gst_nvm_player_list_capture_param_sets
//
//    gst_nvm_player_list_capture_param_sets()  Prints available param sets in
//                                              capture config file
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context

GstNvmResult
gst_nvm_player_list_capture_param_sets (
    GstNvmContextHandle handle);

//  gst_nvm_player_set_display_dynamic
//
//    gst_nvm_player_set_display_dynamic()  Sets display device. Works for dynamic
//                                          change as well - while video is playing.
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context
//
//   display_device
//      (in) Display device name. Currently upported display devices: lvds/hdmi/null
//           In case of null, no video will be displayed.

GstNvmResult
    gst_nvm_player_set_display_dynamic (
    GstNvmContextHandle handle,
    gchar *display_device,
    gint window_id);

//  gst_nvm_player_set_audio_channel_dynamic
//
//    gst_nvm_player_set_audio_channel_dynamic()  Sets audio channel. Works for dynamic
//                                                change as well - while video is playing.
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context
//
//   audio_channel
//      (in) audio channel name or "null" for no audio output.

GstNvmResult
    gst_nvm_player_set_audio_channel_dynamic (
    GstNvmContextHandle handle,
    gchar *audio_channel);

//  gst_nvm_player_set_display_window_position
//
//    gst_nvm_player_set_display_window_position()  Set display window position on screen
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context
//
//  x0
//      (in) x0 position
//
//  y0
//      (in) y0 position
//
//  width
//      (in) Width of the window
//
//  height
//      (in) Height of the window

GstNvmResult
gst_nvm_player_set_display_window_position (GstNvmContextHandle handle,
                                            gint x0,
                                            gint y0,
                                            gint width,
                                            gint height);

//  gst_nvm_player_set_display_window_id
//
//    gst_nvm_player_set_display_window_id()  Set display window depth
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context
//
//  window_id
//      (in) Display window id

GstNvmResult
gst_nvm_player_set_display_window_id (GstNvmContextHandle handle,
                                      gint window_id);

//  gst_nvm_player_set_display_window_depth
//
//    gst_nvm_player_set_display_window_depth()  Set display window depth
//
//  Arguments:
//
//   handle
//      (in) Handle to player instance context
//
//  depth
//      (in) Display window depth

GstNvmResult
gst_nvm_player_set_display_window_depth (GstNvmContextHandle handle,
                                         gint depth);


#ifdef __cplusplus
}
#endif

#endif
