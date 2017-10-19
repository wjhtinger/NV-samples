/* Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include <dlfcn.h>

#include "androida4a.h"
#include "result.h"

#define DESC "Android Accessory"
#define VER  "1.0"
#define URI  "www.nvidia.com"
#define SERIAL "00000012345"

/* Function pointers for USB functions */
struct usb_dl {

  void* handle; // Handle for libusb
  void (*usb_t) (void);
  int (*usb_control_msg_t) (usb_dev_handle*, int, int, int, int, char *, int, int);
  struct usb_bus* (*usb_bus_t) (void);
  struct usb_dev_handle* (*usb_open_t) (struct usb_device *);
  void (*usb_close_t) (usb_dev_handle*);
  const char *dl_error;

} dlusb;

static GstNvmResult init_dl (void)
{
   if (!dlusb.handle) {
       dlusb.handle = dlopen ("libusb-0.1.so.4", RTLD_LAZY);
       dlusb.dl_error = dlerror ();
       if (dlusb.dl_error) {
           GST_ERROR ("Cannot open libusb-0.1. %s",dlusb.dl_error);
           dlclose (dlusb.handle);
           dlusb.handle = NULL;
           return GST_NVM_RESULT_FAIL;
       }
   }

   dlusb.usb_t = dlsym (dlusb.handle, "usb_init");
   dlusb.dl_error = dlerror ();
   if (dlusb.dl_error) {
       GST_ERROR ("Cannot get usb_init. %s",dlusb.dl_error);
       dlusb.usb_t = NULL;
       return GST_NVM_RESULT_FAIL;
     }
   dlusb.usb_t ();

   dlusb.usb_t = dlsym (dlusb.handle, "usb_find_busses");
   dlusb.dl_error = dlerror ();
   if (dlusb.dl_error) {
       GST_ERROR ("Cannot get usb_find_busses. %s",dlusb.dl_error);
       dlusb.usb_t = NULL;
       return GST_NVM_RESULT_FAIL;
     }
   dlusb.usb_t ();

   dlusb.usb_t = dlsym (dlusb.handle, "usb_find_devices");
   dlusb.dl_error = dlerror ();
   if (dlusb.dl_error) {
       GST_ERROR ("Cannot get usb_find_devices. %s",dlusb.dl_error);
       dlusb.usb_t = NULL;
       return GST_NVM_RESULT_FAIL;
     }
   dlusb.usb_t ();

   dlusb.usb_bus_t = dlsym (dlusb.handle, "usb_get_busses");
   dlusb.dl_error = dlerror ();
   if (dlusb.dl_error) {
       GST_ERROR ("Cannot get usb_get_busses. %s",dlusb.dl_error);
       dlusb.usb_bus_t = NULL;
       return GST_NVM_RESULT_FAIL;
     }

   dlusb.usb_open_t = dlsym (dlusb.handle, "usb_open");
   dlusb.dl_error = dlerror ();
   if (dlusb.dl_error) {
       GST_ERROR ("Cannot get usb_open. %s",dlusb.dl_error);
       dlusb.usb_open_t = NULL;
       return GST_NVM_RESULT_FAIL;
     }

   dlusb.usb_close_t = dlsym (dlusb.handle, "usb_close");
   dlusb.dl_error = dlerror ();
   if (dlusb.dl_error) {
       GST_ERROR ("Cannot get usb_close. %s",dlusb.dl_error);
       dlusb.usb_close_t = NULL;
       return GST_NVM_RESULT_FAIL;
     }

   dlusb.usb_control_msg_t = dlsym (dlusb.handle, "usb_control_msg");
   dlusb.dl_error = dlerror ();
   if (dlusb.dl_error) {
       GST_ERROR ("Cannot get usb_control_msg. %s",dlusb.dl_error);
       dlusb.usb_control_msg_t = NULL;
       return GST_NVM_RESULT_FAIL;
     }

   return GST_NVM_RESULT_OK;
}

static int is_android_accessory (struct usb_device_descriptor *desc)
{
    if (!desc) {
        GST_ERROR ("USB Device Descriptor empty");
        return -1;
    }

    return (desc->idVendor == USB_ACCESSORY_VENDOR_ID
       && (desc->idProduct == USB_ACCESSORY_PRODUCT_ID
        || desc->idProduct == USB_ACCESSORY_ADB_PRODUCT_ID
        || desc->idProduct == USB_AUDIO_PRODUCT_ID
        || desc->idProduct == USB_AUDIO_ADB_PRODUCT_ID
        || desc->idProduct == USB_ACCESSORY_AUDIO_PRODUCT_ID
        || desc->idProduct == USB_ACCESSORY_AUDIO_ADB_PRODUCT_ID));
}

static int send_string (usb_dev_handle *device_handle, gint index, gchar *str)
{
    if (!device_handle || !index || !str) {
        GST_ERROR ("Invalid arguments");
        return -1;
    }

   return dlusb.usb_control_msg_t (device_handle,
                USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
                ACCESSORY_SEND_STRING,
                0,
                index,
                str,
                strlen(str) + 1,
                DEFAULT_TIMEOUT);
}

static int switch_device (usb_dev_handle *device_handle)
{
    if (!device_handle) {
        GST_ERROR ("Device handle not initialised");
        return -1;
    }

    short protocol = -1;
    int retval = 0;

    GST_DEBUG ("Setting up Android Accessory Protocol...");
    retval = dlusb.usb_control_msg_t (device_handle,
                USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
                ACCESSORY_GET_PROTOCOL,
                0,
                0,
                (char *) &protocol,
                2,
                DEFAULT_TIMEOUT);

    if (retval < 0) {
        GST_ERROR ("%s %d: accessory get protocol failed \n", __func__, __LINE__);
        return -1;
    }

    if (protocol >= 1) {
        if (send_string (device_handle, ACCESSORY_STRING_DESCRIPTION, DESC) == -1) {
            GST_ERROR ("Sending accessory description failed");
            return -1;
        }

        if (send_string (device_handle, ACCESSORY_STRING_VERSION, VER) == -1) {
            GST_ERROR ("Sending accessory version failed");
            return -1;
        }

        if (send_string (device_handle, ACCESSORY_STRING_URI, URI) == -1) {
            GST_ERROR ("Sending accessory uri failed");
            return -1;
        }

        if (send_string (device_handle, ACCESSORY_STRING_SERIAL, SERIAL) == -1) {
            GST_ERROR ("Sending accessory serial failed");
            return -1;
        }

        retval = dlusb.usb_control_msg_t (device_handle,
                    USB_TYPE_VENDOR | USB_ENDPOINT_OUT,
                    AUDIO_START,
                    1,
                    0,
                    NULL,
                    0,
                    DEFAULT_TIMEOUT);

        if (retval < 0) {
            GST_ERROR ("%s %d: audio start failed\n", __func__, __LINE__);
            return retval;
        }

        retval = dlusb.usb_control_msg_t(device_handle,
                    USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
                    ACCESSORY_START,
                    0,
                    0,
                    NULL,
                    0,
                    DEFAULT_TIMEOUT);
        GST_DEBUG ("Accessory protocol got");
        return retval;
    }
    else {
        return -1;
    }
}

GstNvmResult androida4a (void)
{
    struct usb_bus *busses;
    struct usb_bus *bus;
    struct usb_device *dev;
    usb_dev_handle *device_handle = NULL;
    GstNvmResult result;
    gboolean flag = FALSE;

    result = init_dl ();
    if (result == GST_NVM_RESULT_FAIL) {
        GST_ERROR ("Error while opening libusb-0.1 ");
        return GST_NVM_RESULT_FAIL;
    }

    busses = dlusb.usb_bus_t ();
    for (bus = busses; bus; bus = bus->next) {
       for (dev = bus->devices; dev; dev = dev->next) {
            struct usb_device_descriptor *desc = &dev->descriptor;
            device_handle = dlusb.usb_open_t (dev);
            if (!device_handle) {
                GST_DEBUG ("%s %d:usb_open failed for %x:%x\n",
                          __func__, __LINE__, desc->idVendor,
                          desc->idProduct);
                continue;
            }
            if (is_android_accessory (&dev->descriptor)) {
                g_print ("%s %d:Android accessory- %x:%x\n",
                          __func__, __LINE__, desc->idVendor,
                         desc->idProduct);
                flag = TRUE;
                break;
            }
            else {
                if (switch_device(device_handle) >= 0) {
                    g_print ("%s %d:Android accessory- %x:%x\n",
                              __func__, __LINE__, desc->idVendor,
                             desc->idProduct);
                    flag = TRUE;
                    break;
               }
            }
            dlusb.usb_close_t (device_handle);
            device_handle = NULL;
        }
    }
   if (device_handle) {
       dlusb.usb_close_t (device_handle);
       device_handle = NULL;
    }
   if (dlusb.handle) {
       dlclose (dlusb.handle);
       dlusb.handle = NULL;
   }
   if (dlusb.usb_t) {
       dlusb.usb_t = NULL;
   }
   if (dlusb.usb_bus_t) {
       dlusb.usb_bus_t = NULL;
   }
   if (dlusb.usb_open_t) {
       dlusb.usb_open_t = NULL;
   }
   if (dlusb.usb_close_t) {
       dlusb.usb_close_t = NULL;
   }
   if (dlusb.usb_control_msg_t) {
       dlusb.usb_control_msg_t = NULL;
   }
   if (flag == TRUE)
       return GST_NVM_RESULT_OK;
   else
       GST_ERROR ("No devices connected");

   return GST_NVM_RESULT_FAIL;
}
