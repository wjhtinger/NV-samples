/*
 * win_x11.c
 *
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

#include "log_utils.h"
#include "egl_utils.h"
#define MAX_ATTRIB 31
EXTENSION_LIST(EXTLST_EXTERN)

#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// Platform-specific state info
typedef struct _EGLUtilPlatformState
{
    Display* XDisplay;
    int      XScreen;
    Window   XWindow;
}EGLUtilPlatformState;
//
// Window system startup/shutdown
//
static EGLUtilPlatformState *platform = NULL;


// Initialize access to the display system
NvBool WindowSystemInit(EglUtilState *state)
{
    // Allocate a structure for the platform-specific state
    platform = (EGLUtilPlatformState*)malloc(sizeof(EGLUtilPlatformState));
    if (!platform) {
        LOG_ERR("Could not allocate platform specific storage memory.\n");
        goto fail;
    }
    platform->XDisplay = NULL;
    platform->XScreen  = 0;
    platform->XWindow  = (Window)0;

    // Open X display
    platform->XDisplay = XOpenDisplay(NULL);
    if (platform->XDisplay == NULL) {
        LOG_ERR("X failed to open display.\n");
        goto fail;
    }
    platform->XScreen = DefaultScreen(platform->XDisplay);
    state->display = eglGetDisplay((EGLNativeDisplayType)platform->XDisplay);
    return NV_TRUE;

fail:
    free(platform);
    platform = NULL;
    return NV_FALSE;
}

// Terminate access to the display system
void WindowSystemTerminate(void)
{
    if (platform) {
        if (platform->XDisplay) {
            XCloseDisplay(platform->XDisplay);
            platform->XDisplay = NULL;
        }

        free(platform);
        platform = NULL;
    }
}

// Create the window
int WindowSystemWindowInit(EglUtilState *state)
{
    XSizeHints size_hints;
    Atom       wm_destroy_window;
    EGLint dWidth = 0, dHeight = 0;

    if(!state) {
        return 0;
    }

    dWidth = DisplayWidth(platform->XDisplay, platform->XScreen);
    dHeight = DisplayHeight(platform->XDisplay, platform->XScreen);

    // If not specified, window size defaults to display resolution
    if(!(state->width) || (state->width > dWidth))
        state->width = dWidth;
    if(!(state->height) || (state->height > dHeight))
        state->height = dHeight;

    // Create a native window
    platform->XWindow =
        XCreateSimpleWindow(platform->XDisplay,
                            RootWindow(platform->XDisplay,
                                       platform->XScreen),
                            state->xoffset,
                            state->yoffset,
                            state->width,
                            state->height,
                            0,
                            BlackPixel(platform->XDisplay,
                                       platform->XScreen),
                            WhitePixel(platform->XDisplay,
                                       platform->XScreen));
    if (!platform->XWindow) {
        LOG_ERR("Error creating native window.\n");
        goto fail;
    }

    // Set up window properties
    size_hints.flags = PPosition | PSize | PMinSize;
    size_hints.x = 0;
    size_hints.y = 0;
    size_hints.min_width  = (state->width < 320) ? state->width : 320;
    size_hints.min_height = (state->height < 240) ? state->height : 240;
    size_hints.width  = state->width;
    size_hints.height = state->height;

    XSetStandardProperties(platform->XDisplay,
                           platform->XWindow,
                           "window", "icon", None,
                           NULL, 0, &size_hints);

    XSetWindowBackgroundPixmap(platform->XDisplay,
                               platform->XWindow,
                               None);

    wm_destroy_window = XInternAtom(platform->XDisplay,
                                    "WM_DELETE_WINDOW", False);
    XSetWMProtocols(platform->XDisplay,
                    platform->XWindow,
                    &wm_destroy_window,
                    True);

    // Make sure the MapNotify event goes into the queue
    XSelectInput(platform->XDisplay,
                 platform->XWindow,
                 StructureNotifyMask);

    XMapWindow(platform->XDisplay,
               platform->XWindow);


    int depthbits = 8, stencilbits=0;
    int glversion = 2;
    EGLint cfgAttrs[2*MAX_ATTRIB+1], cfgAttrIndex=0;

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
    cfgAttrs[cfgAttrIndex++] = EGL_ALPHA_SIZE;
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

    return 1;

    fail:
    WindowSystemTerminate();
    return 0;
}

// Close the window
void WindowSystemWindowTerminate(EglUtilState *state)
{
    // Close the native window
    if (platform && platform->XWindow) {
        if (platform->XDisplay) {
        XDestroyWindow(platform->XDisplay,
                       platform->XWindow);
        }
        platform->XWindow = (Window)0;
    }
}

NvBool WindowSystemEglSurfaceCreate(EglUtilState *state)
{
    EGLint srfAttrs[2*MAX_ATTRIB+1], srfAttrIndex=0;
    srfAttrs[srfAttrIndex++] = EGL_NONE;

    // Create EGL surface
    state->surface = eglCreateWindowSurface(state->display, state->config,
                               (NativeWindowType)platform->XWindow,
                               srfAttrs);
    if (state->surface == EGL_NO_SURFACE) {
        LOG_ERR("EGL couldn't create window surface.\n");
        return NV_FALSE;
    }
    return NV_TRUE;
}
