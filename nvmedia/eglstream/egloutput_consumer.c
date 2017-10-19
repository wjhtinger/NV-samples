/*
 * egloutput_consumer.c
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Direct egloutput consumer.  Nvmedia frames are consumed by
//                a libdrm crtc or plane.
//

#include <egloutput_consumer.h>
#include <nvmedia.h>
#include <log_utils.h>
#include <misc_utils.h>
#include "egl_utils.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

typedef struct _EGLDeviceState {
    EGLDeviceEXT egl_dev;
    int          drm_fd;
    uint32_t     drm_crtc_id;
    uint32_t     drm_conn_id;
    EGLStreamKHR egl_str;
    EGLOutputLayerEXT egl_lyr;
} EGLDeviceState;

extern EGLDeviceState *eglDeviceState;

NvMediaStatus egloutputConsumer_init(EGLDisplay display, EGLStreamKHR eglStream)
{
    if (!eglStreamConsumerOutputEXT(display, eglStream,
            eglDeviceState->egl_lyr)) {
        LOG_ERR("EglOutput consumer: eglStreamConsumerOutputEXT failed\n");
        return NVMEDIA_STATUS_ERROR;
    }

    return NVMEDIA_STATUS_OK;
}
