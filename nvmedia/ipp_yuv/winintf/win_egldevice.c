/*
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <assert.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "log_utils.h"
#include "egl_utils.h"

EXTENSION_LIST(EXTLST_EXTERN)

#define MAX_DISPLAY_CONNECTOR 5

#ifndef NVMEDIA_GHSI
#define DRM_LIBRARY "libdrm.so.2"
#define DRM_FUNC_DEF(name) typeof(drm ## name) (* name)
#define DRM_FUNC_LOAD_DEF(name) { (void **)&(s_DRMFuncs.name), "drm" #name }
#else  /* NVMEDIA_GHSI */
#define DRM_FUNC_DEF(name) __typeof__(drm ## name) (* name)
#endif  /* NVMEDIA_GHSI */

#define DRM_FUNC_LIST(DRM_MACRO) \
    DRM_MACRO(Open); \
    DRM_MACRO(ModeGetResources); \
    DRM_MACRO(ModeGetPlaneResources); \
    DRM_MACRO(ModeGetPlane); \
    DRM_MACRO(ModeGetConnector); \
    DRM_MACRO(ModeGetEncoder); \
    DRM_MACRO(ModeFreeEncoder); \
    DRM_MACRO(ModeGetCrtc); \
    DRM_MACRO(ModeFreeCrtc); \
    DRM_MACRO(ModeSetCrtc); \
    DRM_MACRO(ModeSetPlane); \
    DRM_MACRO(ModeFreeConnector); \
    DRM_MACRO(ModeFreeResources)

#define CLIP3(low,hi,x) (((x) < (low)) ? (low) : (((x) > (hi)) ? (hi) : (x)))

typedef struct {
    NvU32 refCountLoaded;
    void *drmLibraryHandle;
    DRM_FUNC_LIST(DRM_FUNC_DEF);
} DRMFunctions;

DRMFunctions s_DRMFuncs;

#ifndef NVMEDIA_GHSI
typedef struct {
    void **func;
    char *funcName;
} DRMFuncLoadList;
DRMFuncLoadList s_DRMFuncLoadList[] = {
    DRM_FUNC_LOAD_DEF(Open),
    DRM_FUNC_LOAD_DEF(ModeGetResources),
    DRM_FUNC_LOAD_DEF(ModeGetPlaneResources),
    DRM_FUNC_LOAD_DEF(ModeGetPlane),
    DRM_FUNC_LOAD_DEF(ModeGetConnector),
    DRM_FUNC_LOAD_DEF(ModeGetEncoder),
    DRM_FUNC_LOAD_DEF(ModeFreeEncoder),
    DRM_FUNC_LOAD_DEF(ModeGetCrtc),
    DRM_FUNC_LOAD_DEF(ModeFreeCrtc),
    DRM_FUNC_LOAD_DEF(ModeSetCrtc),
    DRM_FUNC_LOAD_DEF(ModeSetPlane),
    DRM_FUNC_LOAD_DEF(ModeFreeConnector),
    DRM_FUNC_LOAD_DEF(ModeFreeResources),
    { NULL, NULL }
};
#else
#define DRM_FUNC_LOAD(name)                             \
    s_DRMFuncs.##name = drm##name;
#endif
typedef struct _EGLDeviceState {
    EGLDeviceEXT egl_dev;
    int          drm_fd;
    uint32_t     drm_crtc_id;
    uint32_t     drm_conn_id;
    EGLStreamKHR egl_str;
    EGLOutputLayerEXT egl_lyr;
    EGLDeviceEXT egl_devdGPU;
} EGLDeviceState;

EGLDeviceState *eglDeviceState = NULL;

// Initialize access to the display system
NvBool WindowSystemInit(EglUtilState *state)
{
    EGLDeviceEXT devices[16];
    EGLint num_devices;

    LOG_INFO("Use EGLoutput\n");

    eglQueryDevicesEXT(5, devices, &num_devices);
    if (num_devices == 0)
        return NV_FALSE;

    // Allocate a structure for the platform-specific state
    eglDeviceState =
        (EGLDeviceState*) malloc(sizeof(EGLDeviceState));
    if (!eglDeviceState) {
        LOG_ERR("Could not allocate platform specific storage memory.\n");
        return NV_FALSE;
    }
    memset(eglDeviceState, 0, sizeof(EGLDeviceState));

    eglDeviceState->egl_dev = devices[0];
    state->display = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT,
                                    (void*)devices[0], NULL);
    return NV_TRUE;
}

// Initialize access to the display system
NvBool WindowSystemInit_dGPU(EglUtilState *state)
{
    EGLDeviceEXT devices[16];
    EGLint num_devices;

    if(!state) {
        LOG_ERR("EglUtilState is NULL\n");
        return NV_FALSE;
    }
    eglQueryDevicesEXT(5, devices, &num_devices);
    //when dGPU is also present, then num devices must be atleast 2
    if (num_devices < 2)
        return NV_FALSE;
    LOG_INFO("%s :num devices from eglQueryDevicesEXT is %d\n", __func__, num_devices);
    // Allocate a structure for the platform-specific state

    eglDeviceState->egl_devdGPU = devices[1];
    state->display_dGPU = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT,
                                    (void*)devices[1], NULL);
    return NV_TRUE;
}

void
WindowSystemTerminate(void)
{
    if (eglDeviceState) {
        if (eglDeviceState->egl_dev) {
#ifndef NVMEDIA_GHSI
            if (s_DRMFuncs.drmLibraryHandle) {
#endif
                s_DRMFuncs.ModeSetCrtc(eglDeviceState->drm_fd, eglDeviceState->drm_crtc_id,
                               0, 0, 0, &eglDeviceState->drm_conn_id, 1, NULL);
#ifndef NVMEDIA_GHSI
            }
#endif
            eglDeviceState->egl_dev = 0;
        }

        free(eglDeviceState);
        eglDeviceState = NULL;
    }
#ifndef NVMEDIA_GHSI
    if (s_DRMFuncs.drmLibraryHandle) {
        dlclose(s_DRMFuncs.drmLibraryHandle);
#endif
        // Cleanup function pointers
        memset(&s_DRMFuncs, 0, sizeof(DRMFunctions));
        s_DRMFuncs.refCountLoaded --;
#ifndef NVMEDIA_GHSI
    }
#endif

}

static int getOutputLayer(EglUtilState *state)
{
    EGLOutputLayerEXT egl_lyr;
    EGLAttribKHR layer_attr[] = {EGL_NONE, EGL_NONE, EGL_NONE};
    int n;

    // Get the layer for this crtc (no plane support currently)
    layer_attr[0] = EGL_DRM_CRTC_EXT;
    layer_attr[1] = (EGLAttribKHR)eglDeviceState->drm_crtc_id;

    if (!eglGetOutputLayersEXT(state->display, layer_attr, &egl_lyr, 1, &n) || !n) {
        LOG_ERR("Unable to obtain EGLOutputLayer for %s 0x%x\n",
                  "crtc", (int)layer_attr[1]);
        return 0;
    }

    eglDeviceState->egl_lyr = egl_lyr;

    return 1;
}
void
WindowSystemTerminate_dGPU(void)
{
    eglDeviceState->egl_devdGPU = 0;
}

#define DRM_CLEANUP() { \
        if (drm_enc_info != NULL)  { \
                s_DRMFuncs.ModeFreeEncoder(drm_enc_info);   \
        } \
        if (drm_crtc_info != NULL) { \
                s_DRMFuncs.ModeFreeCrtc(drm_crtc_info);    \
        } \
        for (conn = 0; conn < drm_res_info->count_connectors; conn++)  {  \
                if (drm_conn_info_array[conn] != NULL) s_DRMFuncs.ModeFreeConnector(drm_conn_info_array[conn]); \
        } \
        if (drm_res_info) { \
                s_DRMFuncs.ModeFreeResources(drm_res_info); \
        } \
}

int WindowSystemWindowInit(EglUtilState *state)
{
    int conn = 0;
    int xsurfsize = 0, ysurfsize = 0;
    int xmodesize = 0, ymodesize = 0;

    const char* drm_name;
    int drm_fd;
    uint32_t drm_conn_id, drm_enc_id, drm_crtc_id = 0;
    uint32_t drm_conn_id_array[MAX_DISPLAY_CONNECTOR] = {0};
    uint32_t crtc_mask = 0;
    drmModeRes* drm_res_info = NULL;
    drmModeCrtc* drm_crtc_info = NULL;
    drmModeConnector* drm_conn_info = NULL;
    drmModeConnector* drm_conn_info_array[MAX_DISPLAY_CONNECTOR] = {NULL};
    drmModeEncoder* drm_enc_info = NULL;
    int drm_mode_index = 0;
    int index[MAX_DISPLAY_CONNECTOR];
    int id = 0;

    NvBool set_mode  = NV_FALSE;
    int i, n, j;
    int numDisplays = 0;

    if(!state) {
        return 0;
    }

    // Check DRM shared library is loaded
    if(!s_DRMFuncs.refCountLoaded) {
#ifndef NVMEDIA_GHSI
        DRMFuncLoadList *loadList = s_DRMFuncLoadList;
        // Load DRM library
        s_DRMFuncs.drmLibraryHandle = dlopen(DRM_LIBRARY, RTLD_NOW);
        if(!s_DRMFuncs.drmLibraryHandle) {
            LOG_ERR("eglOutput: Cannot load %s library\n", DRM_LIBRARY);
            return 0;
        }
        // Get the function pointers
        while(loadList->func) {
            *loadList->func = dlsym(s_DRMFuncs.drmLibraryHandle, loadList->funcName);
            if(!*loadList->func) {
                LOG_ERR("eglOutput: Cannot get function: %s\n",
                    loadList->funcName);
                // Unload library
                dlclose(s_DRMFuncs.drmLibraryHandle);
                // Cleanup function pointers
                memset(&s_DRMFuncs, 0, sizeof(DRMFunctions));
                return 0;
            }
            loadList++;
        }
#else
     DRM_FUNC_LIST(DRM_FUNC_LOAD);
#endif
    }

    s_DRMFuncs.refCountLoaded++;

    // Obtain and open DRM device file
    drm_name = eglQueryDeviceStringEXT(eglDeviceState->egl_dev, EGL_DRM_DEVICE_FILE_EXT);
    if (!drm_name) {
        LOG_ERR("Couldn't obtain device file from 0x%p\n",
               (void*)(uintptr_t)eglDeviceState->egl_dev);
        return 0;
    }
    drm_fd = s_DRMFuncs.Open(drm_name, NULL);
    if (drm_fd == 0) {
        LOG_ERR("Couldn't open device file '%s'\n", drm_name);
        return 0;
    }
    eglDeviceState->drm_fd = drm_fd;

    // Obtain DRM-KMS resources
    drm_res_info = s_DRMFuncs.ModeGetResources(drm_fd);
    if (!drm_res_info) {
        LOG_ERR("Couldn't obtain DRM-KMS resources\n");
        return 0;
    }

    // We don't draw to a plane, don't need plan info

    // Query info for requested connector
    for(conn = 0; conn < drm_res_info->count_connectors; conn++) {
        drm_conn_id_array[conn] = drm_res_info->connectors[conn];
        drm_conn_info_array[conn] = s_DRMFuncs.ModeGetConnector(drm_fd, drm_conn_id_array[conn]);
        if(!drm_conn_info_array[conn] ||
           (drm_conn_info_array[conn]->connection != DRM_MODE_CONNECTED) ||
           (drm_conn_info_array[conn]->count_modes < 0))
            continue;

        index[numDisplays++] = conn;
    }

    if (numDisplays == 0) {
        LOG_ERR("%s: failed to choose display %d from %d connected displays \n", __func__, state->displayId, numDisplays);
        DRM_CLEANUP();
        return 0;
    } else {
        for (i = 0; i < numDisplays; i++)
        {
            if (index[i] == state->displayId) {
                id = index[i];
                break;
            }
        }
        if (i == numDisplays) {
            LOG_WARN("%s: Use first available display\n", __func__);
            id = index[0];
        }
    }

    drm_conn_id = drm_conn_id_array[id];
    eglDeviceState->drm_conn_id = drm_conn_id_array[id];
    drm_conn_info = drm_conn_info_array[id];

    // Find the possible crtcs
    for(j=0; j<drm_conn_info->count_encoders; j++) {
        drmModeEncoder *enc = s_DRMFuncs.ModeGetEncoder(drm_fd, drm_conn_info->encoders[j]);
        crtc_mask |= enc->possible_crtcs;
        s_DRMFuncs.ModeFreeEncoder(enc);
    }

    // If there is already an encoder attached to the connector, choose
    //   it unless not compatible with crtc/plane
    drm_enc_id = drm_conn_info->encoder_id;
    drm_enc_info = s_DRMFuncs.ModeGetEncoder(drm_fd, drm_enc_id);
    if (drm_enc_info) {
        if (!(drm_enc_info->possible_crtcs & crtc_mask)) {
            s_DRMFuncs.ModeFreeEncoder(drm_enc_info);
            drm_enc_info = NULL;
        }
    }

    // If we didn't have a suitable encoder, find one
    if (!drm_enc_info) {
        for (i=0; i<drm_conn_info->count_encoders; ++i) {
            drm_enc_id = drm_conn_info->encoders[i];
            drm_enc_info = s_DRMFuncs.ModeGetEncoder(drm_fd, drm_enc_id);
            if (drm_enc_info) {
                if (crtc_mask & drm_enc_info->possible_crtcs) {
                    crtc_mask &= drm_enc_info->possible_crtcs;
                    break;
                }
                s_DRMFuncs.ModeFreeEncoder(drm_enc_info);
                drm_enc_info = NULL;
            }
        }
        if (i == drm_conn_info->count_encoders) {
            LOG_ERR("Unable to find suitable encoder\n");
            DRM_CLEANUP();
            return 0;
        }
    }

    // Select a suitable crtc. Give preference to any that's already
    //   attached to the encoder. (Could make this more sophisticated
    //   by finding one not already bound to any other encoders. But
    //   this is just a basic test, so we don't really care that much.)
    assert(crtc_mask);
    for (i=0; i<drm_res_info->count_crtcs; ++i) {
        if (crtc_mask & (1 << i)) {
            drm_crtc_id = drm_res_info->crtcs[i];
            if (drm_res_info->crtcs[i] == drm_enc_info->crtc_id) {
                break;
            }
        }
    }

    eglDeviceState->drm_crtc_id = drm_crtc_id;

    // Query info for crtc
    drm_crtc_info = s_DRMFuncs.ModeGetCrtc(drm_fd, drm_crtc_id);
    if (!drm_crtc_info) {
        LOG_ERR("Unable to obtain info for crtc (%d)\n", drm_crtc_id);
        DRM_CLEANUP();
        return 0;
    }

    // If dimensions are specified and not using a plane, find closest mode
    if ((xmodesize || ymodesize)) {

        // Find best fit among available modes
        int best_index = 0;
        int best_fit = 0x7fffffff;
        for (i=0; i<drm_conn_info->count_modes; ++i) {
            drmModeModeInfoPtr mode = drm_conn_info->modes + i;
            int fit = 0;

            if (xmodesize) {
                fit += abs((int)mode->hdisplay - xmodesize) * (int)mode->vdisplay;
            }
            if (ymodesize) {
                fit += abs((int)mode->vdisplay - ymodesize) * (int)mode->hdisplay;
            }

            if (fit < best_fit) {
                best_index = i;
                best_fit = fit;
            }
        }

        // Choose this size/mode
        drm_mode_index = best_index;
        xmodesize = (int)drm_conn_info->modes[best_index].hdisplay;
        ymodesize = (int)drm_conn_info->modes[best_index].vdisplay;
    }

    // We'll only set the mode if we have to. This hopefully allows
    //   multiple instances of this application to run, writing to
    //   separate planes of the same display, as long as they don't
    //   specifiy incompatible settings.
    if ((drm_conn_info->encoder_id != drm_enc_id) ||
        (drm_enc_info->crtc_id != drm_crtc_id) ||
        !drm_crtc_info->mode_valid ||
        (xmodesize && (xmodesize!=(int)drm_crtc_info->mode.hdisplay)) ||
        (ymodesize && (ymodesize!=(int)drm_crtc_info->mode.vdisplay))) {
        set_mode = NV_TRUE;
    }

    // If dimensions haven't been specified, figure out good values to use
    if (!xmodesize || !ymodesize) {

        // If mode requires reset, just pick the first one available
        //   from the connector
        if (set_mode) {
            xmodesize = (int)drm_conn_info->modes[0].hdisplay;
            ymodesize = (int)drm_conn_info->modes[0].vdisplay;
        }

        // Otherwise get it from the current crtc settings
        else if (drm_crtc_info->mode_valid) {
            xmodesize = (int)drm_crtc_info->mode.hdisplay;
            ymodesize = (int)drm_crtc_info->mode.vdisplay;
        }
    }

    // If mode is not set, the display is not valid
    if (!xmodesize || !ymodesize) {
        printf("%s: failed to choose display %d from %d connected displays \n", __func__, state->displayId, numDisplays);
        DRM_CLEANUP();
        return 0;
    }

    s_DRMFuncs.ModeFreeCrtc(drm_crtc_info);
    drm_crtc_info = NULL;

    xsurfsize = xmodesize;
    ysurfsize = ymodesize;

    if(!state->width || !state->height) {
        state->width = xsurfsize;
        state->height = ysurfsize;
    } else {
        state->width = CLIP3(0, xsurfsize, state->width);
        state->height = CLIP3(0, ysurfsize, state->height);
    }

    if(state->xoffset || state->yoffset)
        s_DRMFuncs.ModeSetCrtc(drm_fd, drm_crtc_id, -1, state->xoffset, state->yoffset, &drm_conn_id, 1,
                           NULL);

    // If necessary, set the mode
    if (set_mode) {
        s_DRMFuncs.ModeSetCrtc(drm_fd, drm_crtc_id, -1, state->xoffset, state->yoffset, &drm_conn_id, 1,
                       drm_conn_info->modes + drm_mode_index);
    }

    // Choose a config and create a context
    EGLint cfg_attr[] = {
           EGL_SURFACE_TYPE, EGL_STREAM_BIT_KHR,
           EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
           EGL_ALPHA_SIZE, 1,
           EGL_NONE
    };

    if (!eglChooseConfig(state->display, cfg_attr, &(state->config), 1, &n) || !n) {
        LOG_ERR("Unable to obtain config that supports stream rendering (error 0x%x)\n", eglGetError());
        DRM_CLEANUP();
        return 0;
    }

    // Get EGLOutputLayerEXT corresponding to chosen drm layer
    if (!getOutputLayer(state)) {
        DRM_CLEANUP();
        return 0;
    }
    DRM_CLEANUP();
    return 1;
}

void
WindowSystemWindowTerminate(EglUtilState *state)
{
    if (eglDeviceState->egl_str != EGL_NO_STREAM_KHR) {
        eglDestroyStreamKHR(state->display, eglDeviceState->egl_str);
    }
}

NvBool WindowSystemEglSurfaceCreate(EglUtilState *state)
{
    EGLOutputLayerEXT egl_lyr;
    EGLStreamKHR egl_str;
    EGLAttribKHR layer_attr[] = {EGL_NONE, EGL_NONE, EGL_NONE};
    EGLint stream_attr[] = {EGL_NONE};
    EGLint srf_attr[] = {EGL_WIDTH, state->width, EGL_HEIGHT, state->height, EGL_NONE};
    int n;

    // Get the layer for this crtc/plane
    //no plane case
    layer_attr[0] = EGL_DRM_CRTC_EXT;
    layer_attr[1] = (EGLAttribKHR)eglDeviceState->drm_crtc_id;
    if (!eglGetOutputLayersEXT(state->display, layer_attr, &egl_lyr, 1, &n) || !n) {
        LOG_ERR("Unable to obtain EGLOutputLayer for %s 0x%x\n",
                  "crtc", (int)layer_attr[1]);
        return NV_FALSE;
    }

    // Create a stream and connect to the output
    egl_str = eglCreateStreamKHR(state->display, stream_attr);
    if (egl_str == EGL_NO_STREAM_KHR) {
        LOG_ERR("Unable to create output stream (error 0x%x)\n", eglGetError());
        return NV_FALSE;
    }
    eglDeviceState->egl_str = egl_str;
    if (!eglStreamConsumerOutputEXT(state->display, egl_str, egl_lyr)) {
        LOG_ERR("Unable to connect stream (error 0x%x)\n", eglGetError());
        return NV_FALSE;
    }
    // Create a surface to feed the stream
    state->surface = eglCreateStreamProducerSurfaceKHR(state->display, state->config, egl_str,srf_attr);
    return NV_TRUE;
}
