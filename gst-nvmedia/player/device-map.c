/* Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <string.h>

#include "asoundlib.h"
#include "device-map.h"
#include "player-core.h"
#include "player-core-priv.h"

#define MAX_PHYSICAL_DISPLAY_DEVICES    2
#define MAX_DISPLAY_DEVICES    6

static
gint
_get_available_display_id (
    GstNvmDevicesMap *device_map,
    gchar *display_name,
    gint *display_ids)
{
    guint i, j = 0;

    // For dual, get the free window of each display
    if (!strcasecmp (display_name, "dual")) {
        for (i = 0; i < device_map->display_devices_max_num; i++) {
            if (!strcasecmp (device_map->displays_map[i].device.name, "display-0") &&
                device_map->displays_map[i].used_by_ctx == GST_NVM_DEVICE_NOT_USED) {
                display_ids[j++] = (gint)i;
                break;
            }
        }
        for (i = 0; i < device_map->display_devices_max_num; i++) {
            if (!strcasecmp (device_map->displays_map[i].device.name, "display-1") &&
                device_map->displays_map[i].used_by_ctx == GST_NVM_DEVICE_NOT_USED) {
                display_ids[j++] = (gint)i;
                break;
            }
        }
    } else {
        for (i = 0; i < device_map->display_devices_max_num; i++) {
            if (!strcasecmp (device_map->displays_map[i].device.name, display_name) &&
                device_map->displays_map[i].used_by_ctx == GST_NVM_DEVICE_NOT_USED) {
                display_ids[j++] = (gint)i;
                break;
            }
        }
    }

    return j;
}

gint
gst_nvm_device_map_init (
    GstNvmDevicesMap *device_map)
{
    guint i = 0;
    gint display_ids[MAX_PHYSICAL_DISPLAY_DEVICES];
    gint displays_num = 1, index = 0;
    gint k, j;
    GstQuery * query;
    guint display[MAX_DISPLAY_DEVICES] = {0};
    GstStructure* config;

    for (i = 0; i < MAX_PHYSICAL_DISPLAY_DEVICES; i++)
        display_ids[i] = GST_NVM_DEVICE_NOT_USED;

    device_map->audio_dev_map = g_malloc (sizeof (GstNvmAudioDeviceMap));
    device_map->audio_dev_num = 1;
    strcpy (device_map->audio_dev_map->device.name, "default");

    // Get the available displays by using query to the gst-nvmedia library.
    device_map->query_sink =  gst_element_factory_make ("nvmediaoverlaysink", "fake-sink");
    config = gst_structure_new ("query-config", "display-query", G_TYPE_POINTER, &display[0], NULL);

    /* create a custom query and pass it downstream */
    query = gst_query_new_custom (GST_QUERY_CUSTOM, config);
    if (gst_element_query (device_map->query_sink, query)) {
        GST_DEBUG ("Element queried for display-devices");
    } else {
        GST_DEBUG ("Element query failed");
    }
    gst_query_unref (query);

    i = 0;
    for (j = 0; j < MAX_PHYSICAL_DISPLAY_DEVICES; j++) {
        if (display[j] == 1) {
            display_ids[i++] = j;
        }
    }

    // Number of available physical display devices
    displays_num = i;

    device_map->physical_display_dev_num = displays_num;
    device_map->display_dev_num = displays_num * GST_NVM_DISPLAY_WINDOWS + 1; // +1 for egl
    device_map->display_devices_max_num = MAX_PHYSICAL_DISPLAY_DEVICES * GST_NVM_DISPLAY_WINDOWS + 1; // +1 for egl
    device_map->displays_map = g_malloc (sizeof (GstNvmDisplayDeviceMap) * device_map->display_devices_max_num);
    memset (device_map->displays_map, 0, sizeof (GstNvmDisplayDeviceMap) * device_map->display_devices_max_num);

    for (k = 0; k < displays_num; k++) {
        for (j = GST_NVM_DISPLAY_WINDOWS - 1; j >= 0 ; j--) {
            index = k + j * displays_num;

            device_map->displays_map[index].device.device_id = display_ids[k];
            device_map->displays_map[index].device.window_id = GST_NVM_DISPLAY_WINDOWS - j - 1;
            sprintf (device_map->displays_map[index].device.description,
                     "Display %d, window %d",
                     device_map->displays_map[index].device.device_id,
                     device_map->displays_map[index].device.window_id);
            sprintf (device_map->displays_map[index].device.name,
                     "display-%d",
                     device_map->displays_map[index].device.device_id);
            device_map->displays_map[index].device.depth = 10 * (GST_NVM_DISPLAY_WINDOWS - device_map->displays_map[index].device.window_id);
            device_map->displays_map[index].device.depth_specified = TRUE;
            device_map->displays_map[index].used_by_ctx = GST_NVM_DEVICE_NOT_USED;
        }
    }

    // EGL display
    strcpy (device_map->displays_map[device_map->display_dev_num - 1].device.name, "egl");
    strcpy (device_map->displays_map[device_map->display_dev_num - 1].device.description, "Output to EGL");
    device_map->displays_map[device_map->display_dev_num - 1].device.device_id = -1;
    device_map->displays_map[device_map->display_dev_num - 1].device.window_id = 0;
    device_map->displays_map[device_map->display_dev_num - 1].used_by_ctx = GST_NVM_DEVICE_NOT_USED;

    for (i = device_map->display_dev_num; i < device_map->display_devices_max_num; i++) {
        device_map->displays_map[i].used_by_ctx = GST_NVM_DEVICE_NOT_CONNECTED;
    }

    return 0;
}

void
gst_nvm_device_map_fini (
    GstNvmDevicesMap *device_map)
{
    if (!device_map)
        return;

    gst_object_unref (device_map->query_sink);
    g_free (device_map->audio_dev_map);
    g_free (device_map->displays_map);
}

void
gst_nvm_device_map_list_display_devices (
    GstNvmDevicesMap *device_map)
{
    guint i = 0;
    gint k, j;
    GstQuery * query;
    guint display[MAX_DISPLAY_DEVICES] = {0};
    GstStructure* config;
    gint display_ids[MAX_PHYSICAL_DISPLAY_DEVICES], missing_ids[MAX_PHYSICAL_DISPLAY_DEVICES];
    gint displays_num = 0;
    gchar device_name[GST_NVM_MAX_DEVICE_NAME];

    for (i = 0; i < MAX_PHYSICAL_DISPLAY_DEVICES; i++) {
        display_ids[i] = -1;
        missing_ids[i] = -1;
    }

    GST_DEBUG ("Updating device-map. Checking for changes in display device availability");
    config = gst_structure_new ("query-config", "display-query", G_TYPE_POINTER, &display[0], NULL);
    /* create a custom query and pass it downstream */
    query = gst_query_new_custom (GST_QUERY_CUSTOM, config);
    if (gst_element_query (device_map->query_sink, query)) {
        GST_DEBUG ("Element queried for display-devices");
    } else {
        GST_DEBUG ("Element query failed");
    }
    gst_query_unref (query);

    i = 0;
    for (j = 0; j < MAX_PHYSICAL_DISPLAY_DEVICES; j++) {
        if (display[j] == 1) {
            GST_DEBUG ("Found connected device: %d", j);
            display_ids[i] = j;
            missing_ids[i] = j;
            i++;
        }
    }

    // Number of available physical display devices
    displays_num = i;

    GST_DEBUG ("Remove the unplugged devices from device-map (%d, %d)", display_ids[0], display_ids[1]);
    // Remove the unplugged devices from device-map and update which are missing
    for (i = 0; i < device_map->display_devices_max_num; i++) { // Displays in device-map
        if (!strcasecmp (device_map->displays_map[i].device.name, "egl")) {
            continue; // Continue to next device - egl should always remain in device-map
        }

        for (k = 0; k < displays_num; k++) { // Actual displays available
            if (display_ids[k] != -1) {
                sprintf (device_name, "display-%d", display_ids[k]);
                if (!strcasecmp (device_map->displays_map[i].device.name, device_name)) {
                    GST_DEBUG ("Device: %d is still connected", display_ids[k]);
                    missing_ids[k] = -1;
                    break; // Continue to next device in device-map - currently checked is still connected
                } else if (k == (displays_num - 1)) {
                    // If we reached here, it means the device wasn't found - remove it
                    memset (&device_map->displays_map[i], 0, sizeof (device_map->displays_map[i]));
                    device_map->displays_map[i].used_by_ctx = GST_NVM_DEVICE_NOT_CONNECTED;
                }
            }
        }
    }

    GST_DEBUG ("Current physical devices num: %d", displays_num);
    device_map->physical_display_dev_num = displays_num;
    device_map->display_dev_num = displays_num * GST_NVM_DISPLAY_WINDOWS + 1; // +1 for egl

    GST_DEBUG ("Add new devices to device-map if such exist (%d, %d)", missing_ids[0], missing_ids[1]);
    // Add new devices to device-map if such exist
    for (k = 0; k < MAX_PHYSICAL_DISPLAY_DEVICES; k++) {
        // If not missing, continue
        if (missing_ids[k] == -1)
            continue;

        GST_DEBUG ("Missing: %d", missing_ids[k]);
        for (j = GST_NVM_DISPLAY_WINDOWS - 1; j >= 0 ; i--) {
            // Find next available index for the new device
            for (i = 0; i < device_map->display_devices_max_num; i++) {
                if (device_map->displays_map[i].used_by_ctx != GST_NVM_DEVICE_NOT_CONNECTED)
                    continue;

                device_map->displays_map[i].device.device_id = missing_ids[k];
                device_map->displays_map[i].device.window_id = GST_NVM_DISPLAY_WINDOWS - j - 1;
                sprintf (device_map->displays_map[i].device.description,
                        "Display %d, window %d",
                        device_map->displays_map[i].device.device_id,
                        device_map->displays_map[i].device.window_id);
                sprintf (device_map->displays_map[i].device.name,
                        "display-%d",
                        device_map->displays_map[i].device.device_id);
                device_map->displays_map[i].device.depth = 10 * (GST_NVM_DISPLAY_WINDOWS - device_map->displays_map[i].device.window_id);
                device_map->displays_map[i].device.depth_specified = TRUE;
                device_map->displays_map[i].used_by_ctx = GST_NVM_DEVICE_NOT_USED;
            }
        }
    }

    if (displays_num == 0) {
        g_printf ("No Display Devices Found\n");
    } else {
        g_printf ("Available Display Devices:\n");
        for (i = 0; i < MAX_PHYSICAL_DISPLAY_DEVICES; i++) {
            if (display_ids[i] != -1) {
                sprintf (device_name, "display-%d", display_ids[i]);
                g_printf ("\"%s\n", device_name);
            }
        }
    }

    g_printf ("\n");
}

gint
gst_nvm_device_map_get_display_used_by_ctx (
    GstNvmDevicesMap *device_map,
    gint used_by_ctx,
    gint *display_ids)
{
    guint i, j = 0;

    display_ids[0] = -1;
    display_ids[1] = -1;

    for (i = 0; i < device_map->display_devices_max_num; i++)
        if (device_map->displays_map[i].used_by_ctx == used_by_ctx)
            display_ids[j++] = (gint)i;

    return j;
}

gint
gst_nvm_device_map_get_available_display_id (
    GstNvmDevicesMap *device_map)
{
    guint i;

    for (i = 0; i < device_map->display_devices_max_num; i++) {
        if (device_map->displays_map[i].used_by_ctx == GST_NVM_DEVICE_NOT_USED &&
            strcasecmp (device_map->displays_map[i].device.name, "egl")) {
            // Do not acquire EGL by default. Only by user request
            return i;
        }
    }

    return -1;
}

gint
gst_nvm_device_map_set_display_device_to_ctx (
    GstNvmDevicesMap *device_map,
    gchar *display_device,
    guint ctx_id,
    gint *display_ids)
{
    gint prev_id[GST_NVM_MAX_DISPLAYS_PER_CTX];
    gint i, new_displays_num = 0, used_displays_num;

    for (i = 0; i < GST_NVM_MAX_DISPLAYS_PER_CTX; i++)
        prev_id[i] = -1;

    if (!strcasecmp (display_device, "mirror-egl")) {
        new_displays_num = _get_available_display_id (device_map, "egl", display_ids);

        if (new_displays_num == 0) {
            GST_ERROR ("Chosen display device is being used \n");
            return -1;
        }
    } else if (strcasecmp (display_device, "NULL")) { // If not setting to NULL check device availability
        new_displays_num = _get_available_display_id (device_map, display_device, display_ids);

        if (new_displays_num == 0) {
            GST_ERROR ("Chosen display device is not available or being used \n");
            return -1;
        }
    }

    used_displays_num = gst_nvm_device_map_get_display_used_by_ctx (device_map, ctx_id, prev_id);

    if (((new_displays_num + used_displays_num) > GST_NVM_MAX_DISPLAYS_PER_CTX) &&
        !strcasecmp (display_device, "mirror-egl")) {
        GST_ERROR ("Exceeded the max number of used diaplays (%d). Setting display device failed.\n",
                   GST_NVM_MAX_DISPLAYS_PER_CTX);
        return -1;
    }

    for (i = 0; i <= used_displays_num; i++) {
        if (prev_id[i] != -1 && strcasecmp (display_device, "mirror-egl"))
            device_map->displays_map[prev_id[0]].used_by_ctx = GST_NVM_DEVICE_NOT_USED;
    }

    for (i = 0; i <= new_displays_num; i++) {
        if (display_ids[i] != -1)
            device_map->displays_map[display_ids[i]].used_by_ctx = ctx_id;
    }

    return 0;
}

guint
gst_nvm_device_map_update_w_window_id_change (
    GstNvmDevicesMap *device_map,
    guint new_window_id,
    guint ctx_id)
{
    gint prev_id[GST_NVM_MAX_DISPLAYS_PER_CTX];
    gint i, used_displays_num = 0;
    guint j;
    char *display_device;

    for (i = 0; i < GST_NVM_MAX_DISPLAYS_PER_CTX; i++)
        prev_id[i] = -1;

    used_displays_num = gst_nvm_device_map_get_display_used_by_ctx (device_map, (guint)ctx_id, prev_id);

    for (i = 0; i < used_displays_num; i++) {
        display_device = device_map->displays_map[prev_id[i]].device.name;
        if(strcasecmp(display_device, "egl")) { // Do not change window id for egl!
            // First only check if changing window id is possible for all used displays. Next round actually change.
            for (j = 0; j < device_map->display_devices_max_num; j++) {
                if (!strcasecmp(device_map->displays_map[j].device.name, display_device) && device_map->displays_map[j].device.window_id == new_window_id) {
                    if (device_map->displays_map[j].used_by_ctx != GST_NVM_DEVICE_NOT_USED && device_map->displays_map[j].used_by_ctx != (gint)ctx_id) {
                        GST_ERROR ("Failed changing window id to %d for display %s. Window ID is already used by context: %d",
                                   new_window_id, display_device, device_map->displays_map[j].used_by_ctx);
                        return -1;
                    }
                }
            }
            // Change used window ID
            for (j = 0; j < device_map->display_devices_max_num; j++) {
                if (!strcasecmp(device_map->displays_map[j].device.name, display_device) && device_map->displays_map[j].device.window_id == new_window_id) {
                    device_map->displays_map[prev_id[i]].used_by_ctx = GST_NVM_DEVICE_NOT_USED; // Release old window id
                    device_map->displays_map[j].used_by_ctx = ctx_id; // Acquire new window id
                }
            }
        } else {
            GST_ERROR ("Changing window id for egl display is not allowed. Skipping window id change for egl display.\n");
            return -1;
        }
    }

    return 0;
}
