/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#include <stdlib.h>
#include <sys/keycodes.h>
#include <screen/screen.h>

#include "stdlib.h"
#include "log_utils.h"
#include "egl_utils.h"

#define MAX_ATTRIB 31

// Platform-specific state info
typedef struct _EGLScreenState {
    screen_context_t screen_ctx;
    screen_display_t screen_display;
    screen_window_t screen_win;
    screen_event_t screen_ev;
} EGLScreenState;

static int choose_format(void *disp, void *egl_conf)
{
    int ret;
    EGLint bufSize;
    EGLint alphaSize;

    EGLDisplay dpy = (EGLDisplay)(disp);
    EGLConfig cfg = (EGLConfig) (egl_conf);

    eglGetConfigAttrib(dpy, cfg, EGL_BUFFER_SIZE, &bufSize);
    eglGetConfigAttrib(dpy, cfg, EGL_ALPHA_SIZE, &alphaSize);

    if (bufSize == 16) {
        ret = SCREEN_FORMAT_RGB565;
    } else if (alphaSize == 8) {
        ret = SCREEN_FORMAT_RGBA8888;
    } else {
        ret = SCREEN_FORMAT_RGBX8888;
    }

    return ret;
}

static EGLScreenState *eglScreenState = NULL;

NvBool WindowSystemInit(EglUtilState *state) {
    int displayCount, attached = 0;
    screen_display_t displayHandle[16];

    // Allocate a structure for the platform-specific state
    eglScreenState = (EGLScreenState *)calloc(1, sizeof(EGLScreenState));
    if (!eglScreenState) {
        LOG_ERR("Could not allocate platform specific storage memory.\n");
        goto fail;
    }

    // Create QNX CAR 2.1  context
    int rc = screen_create_context(&(eglScreenState->screen_ctx), 0);
    if (rc) {
        LOG_ERR("Failed to create context.\n");
        goto fail;
    }

    // Set the requested display
    screen_get_context_property_iv(eglScreenState->screen_ctx,
                                   SCREEN_PROPERTY_DISPLAY_COUNT, &displayCount);
    if (state->displayId >= displayCount) {
        LOG_ERR("Failed to set the requested display %d.\n",
                    state->displayId);
        goto fail;
    }
    screen_get_context_property_pv(eglScreenState->screen_ctx,
                                   SCREEN_PROPERTY_DISPLAYS,
                                   (void **) displayHandle);
    screen_get_display_property_iv(displayHandle[state->displayId],
                                   SCREEN_PROPERTY_ATTACHED,
                                   &attached);
    if (attached) {
        eglScreenState->screen_display =
            displayHandle[state->displayId];
    } else {
        LOG_ERR("Requested display %d not attached.\n",
                    state->displayId);
        goto fail;
    }

    // Setting state->nativeDisplay = eglScreenState->screen_display
    // causes eglGetDisplay() to fail. Just use EGL_DEFAULT_DISPLAY.
    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    return NV_TRUE;

fail:
    WindowSystemTerminate();
    return NV_FALSE;
}

void WindowSystemTerminate(void) {
    if (!eglScreenState) {
        return;
    }

    WindowSystemWindowTerminate(NULL);

    if (eglScreenState->screen_ctx) {
        screen_destroy_context(eglScreenState->screen_ctx);
    }

    free(eglScreenState);
    eglScreenState = NULL;
}

int WindowSystemWindowInit(EglUtilState *state) {
    int usage = SCREEN_USAGE_OPENGL_ES2;
    int nbuffers = 3;
    EGLint interval = 1;
    int rc, format;
    int tempSize[2];
    int pipelineId, defaultpipelineId = 0;
    const int NUM_PIPELINES = 3;

    if(!state) {
        return 0;
    }

    if (!eglScreenState) {
        return 0;
    }

    if (!eglScreenState->screen_ctx) {
        return 0;
    }

    // If not specified, use default window size
    if (!state->width)
        state->width = 800;
    if (!state->height)
        state->height = 480;

    // Create a native window
    rc = screen_create_window(&eglScreenState->screen_win, eglScreenState->screen_ctx);
    if (rc) {
        LOG_ERR("Failed to create native window.\n");
        goto fail;
    }

    // Set the requested display
    screen_set_window_property_pv(eglScreenState->screen_win,
                                  SCREEN_PROPERTY_DISPLAY,
                                  (void **) &eglScreenState->screen_display);

    // Configure the requested layer.
    // Screen API does not have much support for querying the display pipeline information.
    // Hard coding the pipeline ID's and the number of pipelines supported per display for now.
    if (state->windowId < 0 || state->windowId >= NUM_PIPELINES) {
        LOG_ERR("Failed to set the requested layer %d.\n", state->windowId);
        goto fail;
    }

    if (!state->vidConsumer) {
        // Query defualt pipelineId from Screen compositor
        rc = screen_get_window_property_iv(eglScreenState->screen_win, SCREEN_PROPERTY_PIPELINE,
                                                                                &defaultpipelineId);
        if (rc) {
            LOG_ERR("Query the default layer info is failed Req[%d].\n", state->windowId);
            goto fail;
        }
        // Each display can have maximum 10 pipeline ids
        state->displayId = defaultpipelineId / 10;

        pipelineId = (10 * state->displayId) + state->windowId + 1;
        rc = screen_set_window_property_iv(eglScreenState->screen_win, SCREEN_PROPERTY_PIPELINE, &pipelineId);
        if (rc) {
            LOG_ERR("Failed to set the requested layer %d.\n", state->windowId);
            goto fail;
        }

        // defaultpipelineId used by QNX Screen compositor
        if (pipelineId != defaultpipelineId) {
            usage |= SCREEN_USAGE_OVERLAY;
        }
    }

    // Select and set window property SCREEN_PROPERTY_FORMAT
    format = choose_format(state->display, state->config);
    rc = screen_set_window_property_iv(eglScreenState->screen_win,
            SCREEN_PROPERTY_FORMAT, &format);
    if (rc) {
        LOG_ERR("Failed to set window property SCREEN_PROPERTY_FORMAT.\n");
        goto fail;
    }

    // Set window property SCREEN_PROPERTY_USAGE
    rc = screen_set_window_property_iv(eglScreenState->screen_win,
            SCREEN_PROPERTY_USAGE, &usage);
    if (rc) {
        LOG_ERR("Failed to set window property SCREEN_PROPERTY_USAGE.\n");
        goto fail;
    }

    // Set window property SCREEN_PROPERTY_SWAP_INTERVAL
    rc = screen_set_window_property_iv(eglScreenState->screen_win,
            SCREEN_PROPERTY_SWAP_INTERVAL, &interval);
    if (rc) {
        LOG_ERR("Failed to set window property SCREEN_PROPERTY_SWAP_INTERVAL.\n");
        goto fail;
    }

    if (state->width > 0 && state->height > 0) {
        // Set window property SCREEN_PROPERTY_SIZE
        tempSize[0] = state->width;
        tempSize[1] = state->height;
        rc = screen_set_window_property_iv(eglScreenState->screen_win,
                SCREEN_PROPERTY_SIZE, tempSize);
        if (rc) {
            LOG_ERR("Failed to set window property SCREEN_PROPERTY_SIZE.\n");
            goto fail;
        }
    }

    if (state->xoffset != 0 && state->yoffset != 0) {
        // Set window property SCREEN_PROPERTY_POSTION
        tempSize[0] = state->xoffset;
        tempSize[1] = state->yoffset;
        rc = screen_set_window_property_iv(eglScreenState->screen_win,
                SCREEN_PROPERTY_POSITION, tempSize);
        if (rc) {
            LOG_ERR("Failed to set window property SCREEN_PROPERTY_POSITION.\n");
            goto fail;
        }
    }

    int transperency = SCREEN_TRANSPARENCY_SOURCE;
    rc = screen_set_window_property_iv(eglScreenState->screen_win,
            SCREEN_PROPERTY_TRANSPARENCY, &transperency);
    if (rc) {
        LOG_ERR("Failed to set window property SCREEN_TRANSPARENCY_SOURCE_OVER.\n");
        goto fail;
    }

    // Create QNX CAR 2.1 window buffers
    rc = screen_create_window_buffers(eglScreenState->screen_win, nbuffers);
    if (rc) {
        LOG_ERR("Failed to create window buffers.\n");
        goto fail;
    }

    return 1;

fail:
    WindowSystemWindowTerminate(state);
    return 0;
}

void WindowSystemWindowTerminate(EglUtilState *state) {
    if (!eglScreenState) {
        return;
    }

    if (!eglScreenState->screen_ctx) {
        return;
    }

    if (!eglScreenState->screen_win) {
        return;
    }

    // Destroy native window & event
    screen_destroy_window(eglScreenState->screen_win);
    screen_destroy_event(eglScreenState->screen_ev);
    eglScreenState->screen_win = NULL;
    eglScreenState->screen_ev = NULL;
}

NvBool WindowSystemEglSurfaceCreate(EglUtilState *state) {
    int depthbits = 8, stencilbits=0;
    int glversion = 2;
    EGLint cfgAttrs[2*MAX_ATTRIB+1], cfgAttrIndex=0;
    EGLint srfAttrs[2*MAX_ATTRIB+1], srfAttrIndex=0;
    EGLConfig* configList = NULL;
    EGLint     configCount;
    EGLBoolean eglStatus;

    // Bind GL API
    eglBindAPI(EGL_OPENGL_ES_API);

    // Request GL version
    cfgAttrs[cfgAttrIndex++] = EGL_RENDERABLE_TYPE;
    cfgAttrs[cfgAttrIndex++] = (glversion == 2) ? EGL_OPENGL_ES2_BIT
                                                    : EGL_OPENGL_ES_BIT;

    // Request a minimum of 1 bit each for red, green, and blue
    // Setting these to anything other than DONT_CARE causes the returned
    //   configs to be sorted with the largest bit counts first.
    cfgAttrs[cfgAttrIndex++] = EGL_RED_SIZE;
    cfgAttrs[cfgAttrIndex++] = 1;
    cfgAttrs[cfgAttrIndex++] = EGL_GREEN_SIZE;
    cfgAttrs[cfgAttrIndex++] = 1;
    cfgAttrs[cfgAttrIndex++] = EGL_BLUE_SIZE;
    cfgAttrs[cfgAttrIndex++] = 1;

    // If application requires depth or stencil, request them
    if (depthbits) {
        cfgAttrs[cfgAttrIndex++] = EGL_DEPTH_SIZE;
        cfgAttrs[cfgAttrIndex++] = depthbits;
    }
    if (stencilbits) {
        cfgAttrs[cfgAttrIndex++] = EGL_STENCIL_SIZE;
        cfgAttrs[cfgAttrIndex++] = stencilbits;
    }
    // Request antialiasing
    cfgAttrs[cfgAttrIndex++] = EGL_SAMPLES;
    cfgAttrs[cfgAttrIndex++] = 0;

    // Terminate attribute lists
    cfgAttrs[cfgAttrIndex++] = EGL_NONE;
    srfAttrs[srfAttrIndex++] = EGL_NONE;

    // Find out how many configurations suit our needs
    eglStatus = eglChooseConfig(state->display, cfgAttrs,
                                NULL, 0, &configCount);
    if (!eglStatus || !configCount) {
        LOG_ERR("EGL failed to return any matching configurations.\n");
        goto fail;
    }

    // Allocate room for the list of matching configurations
    configList = (EGLConfig*)malloc(configCount * sizeof(EGLConfig));
    if (!configList) {
        LOG_ERR("Allocation failure obtaining configuration list.\n");
        goto fail;
    }

    // Obtain the configuration list from EGL
    eglStatus = eglChooseConfig(state->display, cfgAttrs,
                                configList, configCount, &configCount);
    if (!eglStatus || !configCount) {
        LOG_ERR("EGL failed to populate configuration list.\n");
        goto fail;
    }

    // Select an EGL configuration that matches the native window
    // Currently we just choose the first one, but we could search
    //   the list based on other criteria.
    state->config = configList[0];
    free(configList);
    configList = 0;

    // Create EGL surface
    state->surface = eglCreateWindowSurface(state->display,
                                            state->config,
                                            (NativeWindowType)(eglScreenState->screen_win),
                                            srfAttrs);
    if (state->surface == EGL_NO_SURFACE) {
        LOG_ERR("EGL couldn't create window surface.\n");
        goto fail;
    }

    return NV_TRUE;
fail:
    if (configList) free(configList);
    return NV_FALSE;
}
