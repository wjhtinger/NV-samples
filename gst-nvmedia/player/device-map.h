/* Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __DEVICE_MAP_H__
#define __DEVICE_MAP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <ctype.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "result.h"

#define GST_NVM_MAX_PROPERTY_SIZE           32
#define GST_NVM_MAX_DEVICE_NAME             64
#define GST_NVM_MAX_DESCRIPTION_SIZE        256
#define GST_NVM_MAX_DISPLAYS_PER_CTX        3
#define GST_NVM_DISPLAY_WINDOWS             3
#define GST_NVM_DEVICE_NOT_USED             -1
#define GST_NVM_DEVICE_NOT_CONNECTED        -2
#define GST_NVM_DEFAULT_ETH_INTERFACE       "eth0"
#define GST_NVM_DEFAULT_SRC_VLAN_PRIO        2
#define GST_NVM_DEFAULT_SRC_STREAM_ID        0
#define GST_NVM_DEFAULT_STREAM_ID           "0"

typedef struct {
    gint x0;
    gint y0;
    gint width;
    gint height;
} GstNvmWindowPosition;

typedef struct {
    gchar                   present_name[GST_NVM_MAX_DEVICE_NAME];
    gchar                   name[GST_NVM_MAX_DEVICE_NAME];
    guint                   device_id;
    guint                   window_id;
    guint                   depth_specified;
    guint                   depth;
    gboolean                position_specified;
    GstNvmWindowPosition    position;
    gchar                   position_str[GST_NVM_MAX_PROPERTY_SIZE]; // x0:y0:x1:y1
    gchar                   description[GST_NVM_MAX_DESCRIPTION_SIZE];
} GstNvmDisplayDeviceInfo;

typedef struct {
    GstNvmDisplayDeviceInfo device;
    gint                    used_by_ctx;
} GstNvmDisplayDeviceMap;

typedef struct {
    gchar           name[GST_NVM_MAX_DEVICE_NAME];
    gchar           description[GST_NVM_MAX_DESCRIPTION_SIZE];
} GstNvmAudioDeviceInfo;

typedef struct {
    GstNvmAudioDeviceInfo   device;
    gint                    used_by_ctx;
} GstNvmAudioDeviceMap;

typedef struct {
    GstNvmAudioDeviceMap     *audio_dev_map;
    guint                     audio_dev_num;
    GstNvmDisplayDeviceMap   *displays_map;
    guint                     display_dev_num;
    guint                     physical_display_dev_num;
    guint                     display_devices_max_num;
    GstElement               *query_sink;
} GstNvmDevicesMap;

gint
gst_nvm_device_map_init (
    GstNvmDevicesMap *device_map);

void
gst_nvm_device_map_fini (
    GstNvmDevicesMap *device_map);

void
gst_nvm_device_map_list_display_devices (
    GstNvmDevicesMap *device_map);

gint
gst_nvm_device_map_get_display_used_by_ctx (
    GstNvmDevicesMap *device_map,
    gint context_id,
    gint *display_ids);

gint
gst_nvm_device_map_get_available_display_id (
    GstNvmDevicesMap *device_map);

gint
gst_nvm_device_map_set_display_device_to_ctx (
    GstNvmDevicesMap *device_map,
    gchar *display_device,
    guint ctx_id,
    gint *display_ids);

guint
gst_nvm_device_map_update_w_window_id_change (
    GstNvmDevicesMap *device_map,
    guint new_window_id,
    guint ctx_id);

#ifdef __cplusplus
}
#endif

#endif
