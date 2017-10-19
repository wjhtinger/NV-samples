/*
 * win_wayland.c
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
#include <linux/input.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/timerfd.h>

#include "log_utils.h"
#include "egl_utils.h"
#include "eglstrm_setup.h"
#define MAX_ATTRIB 31
EXTENSION_LIST(EXTLST_EXTERN)

#include <xkbcommon/xkbcommon.h>
#include "wayland-client.h"
#include "wayland-egl.h"

#define MAX_DISPLAY_WIDTH 1920
#define MAX_DISPLAY_HEIGHT 1080

// Platform-specific state info
struct Window;

struct Display {
    struct wl_display *wlDisplay;
    struct wl_registry *wlRegistry;
    struct wl_compositor *wlCompositor;
    struct wl_shell *wlShell;
    struct wl_pointer *pointer;
    struct Window *window;
};

struct Geometry {
    int width, height;
};

struct Window {
    struct Display *display;
    struct wl_egl_window *wlEGLNativeWindow;
    struct wl_surface *wlSurface;
    struct wl_shell_surface *wlShellSurface;
    struct wl_callback *callback;
    int fullscreen, configured, opaque;
    struct Geometry geometry,window_size;
};

// Platform-specific state info
typedef struct _EGLUtilPlatformState
{
    struct Display display;
    int screen;
    struct Window window;
}EGLUtilPlatformState;

static void
handle_ping(void *data, struct wl_shell_surface *wlShellSurface,
            uint32_t serial)
{
    wl_shell_surface_pong(wlShellSurface, serial);
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
                 uint32_t edges, int32_t width, int32_t height)
{
    struct Window *window = data;

    if (window->wlEGLNativeWindow) {
        wl_egl_window_resize(window->wlEGLNativeWindow, width, height, 0, 0);
    }

    window->geometry.width = width;
    window->geometry.height = height;

    if (!window->fullscreen) {
        window->window_size = window->geometry;
    }
}

static const struct wl_shell_surface_listener shell_surface_listener =
{
    handle_ping,
    handle_configure,
};

static void
configure_callback(void *data, struct wl_callback *callback, uint32_t time)
{
    struct Window *window = data;

    wl_callback_destroy(callback);

    window->configured = 1;
}

static struct wl_callback_listener configure_callback_listener =
{
    configure_callback,
};

static void
toggle_fullscreen(struct Window *window, int fullscreen)
{

    struct wl_callback *callback;

    window->fullscreen = fullscreen;
    window->configured = 0;

    if (fullscreen) {
        wl_shell_surface_set_fullscreen(
            window->wlShellSurface,
            WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT,0, NULL);
    } else {
        wl_shell_surface_set_toplevel(window->wlShellSurface);
        handle_configure(window, window->wlShellSurface, 0,
            window->window_size.width,
            window->window_size.height);
    }

    callback = wl_display_sync(window->display->wlDisplay);
    wl_callback_add_listener(callback, &configure_callback_listener,
        window);

}

// Registry handling static function
static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface,
                       uint32_t version)
{
    struct Display *d = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        d->wlCompositor = wl_registry_bind(registry, name,
                                &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_shell") == 0) {
        d->wlShell = wl_registry_bind(registry, name,
                                &wl_shell_interface, 1);
    }
}
static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

//
// Window system startup/shutdown
//
static EGLUtilPlatformState *platform = NULL;

// Initialize access to the display system
NvBool WindowSystemInit(EglUtilState *state)
{
    char display_socket_name[20];

    // Allocate a structure for the platform-specific state
    platform =
        (EGLUtilPlatformState*)malloc(sizeof(EGLUtilPlatformState));

    if (!platform) {
        LOG_ERR("Could not allocate platform specific storage memory.\n");
        goto fail;
    }

    snprintf(display_socket_name, 20, "wayland-%d", state->displayId);

    memset(platform, 0, sizeof(EGLUtilPlatformState));

    platform->window.display = &platform->display;
    platform->display.window = &platform->window;
    // Open WAYLAND display
    platform->display.wlDisplay = wl_display_connect(NULL);
    if (platform->display.wlDisplay == NULL) {
        LOG_ERR("connect to a wayland socket failed.\n");
        goto fail;
    }

    platform->display.wlRegistry = wl_display_get_registry(
                                   platform->display.wlDisplay);
    if (platform->display.wlRegistry == NULL) {
        LOG_ERR("Failed to get registry.\n");
        goto fail;
    }

    wl_registry_add_listener(platform->display.wlRegistry,
                   &registry_listener, &platform->display);

    wl_display_dispatch(platform->display.wlDisplay);

    state->display = eglGetDisplay((NativeDisplayType)platform->display.wlDisplay);

    return NV_TRUE;

fail:
    WindowSystemTerminate();

    return NV_FALSE;
}

// Terminate access to the display system
void WindowSystemTerminate(void)
{
    if (!platform) {
        return;
    }

    if (platform->display.wlRegistry) {
        wl_registry_destroy(platform->display.wlRegistry);
    }

    if (platform->display.wlShell) {
        wl_shell_destroy(platform->display.wlShell);
    }

    if (platform->display.wlCompositor) {
        wl_compositor_destroy(platform->display.wlCompositor);
    }

    // Explicitly destroy pointer object as traditional(weston clients) way
    // to destroy them in seat_handle_capabilies (a wl_seat_listner) is not
    // triggered for reasons still to be discovered.
    if (platform->display.pointer) {
        wl_pointer_destroy(platform->display.pointer);
        platform->display.pointer = NULL;
    }

    if (platform->display.wlDisplay) {
        wl_display_flush(platform->display.wlDisplay);
        wl_display_disconnect(platform->display.wlDisplay);
        platform->display.wlDisplay = NULL;
    }

    free(platform);
    platform = NULL;
}

// Create the window
int WindowSystemWindowInit(EglUtilState *state)
{
    if (!platform) {
        return 0;
    }

    platform->window.wlSurface  =
       wl_compositor_create_surface(platform->display.wlCompositor);
    if (platform->window.wlSurface == NULL) {
        LOG_ERR("Failed to create wayland surface.\n");
        goto fail;
    }

    platform->window.wlShellSurface =
       wl_shell_get_shell_surface(platform->display.wlShell,
           platform->window.wlSurface);
    if (platform->window.wlShellSurface == NULL) {
        LOG_ERR("Failed to create wayland shell surface.\n");
        goto fail;
    }

    if (!(state->width) || (state->width > MAX_DISPLAY_WIDTH))
        state->width = MAX_DISPLAY_WIDTH;
    if (!(state->height) || (state->height > MAX_DISPLAY_HEIGHT))
        state->height = MAX_DISPLAY_HEIGHT;

    platform->window.geometry.width  = (state->width > 320) ? state->width : 320;
    platform->window.geometry.height = (state->height> 240) ? state->height: 240;
    platform->window.window_size = platform->window.geometry;
    platform->window.wlEGLNativeWindow =
        wl_egl_window_create(platform->window.wlSurface,
                             platform->window.geometry.width,
                             platform->window.geometry.height);
    if (platform->window.wlEGLNativeWindow == NULL) {
        LOG_ERR("Failed to create wayland EGL window.\n");
        goto fail;
    }
    toggle_fullscreen(&platform->window, platform->window.fullscreen);

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
void
WindowSystemWindowTerminate(EglUtilState *state)
{
    if (!platform) {
        return;
    }

    // Close the native window
    if (platform->window.wlEGLNativeWindow) {
        wl_egl_window_destroy(platform->window.wlEGLNativeWindow);
        platform->window.wlEGLNativeWindow = 0;
    }
    if (platform->window.wlShellSurface) {
        wl_shell_surface_destroy(platform->window.wlShellSurface);
        platform->window.wlShellSurface = 0;
    }

    if (platform->window.wlSurface) {
        wl_surface_destroy(platform->window.wlSurface);
        platform->window.wlSurface = 0;
    }

    if (platform->window.callback) {
        wl_callback_destroy(platform->window.callback);
        platform->window.callback = 0;
    }
}

NvBool WindowSystemEglSurfaceCreate(EglUtilState *state)
{
    EGLint srfAttrs[2*MAX_ATTRIB+1], srfAttrIndex=0;
    srfAttrs[srfAttrIndex++] = EGL_NONE;

    // Create EGL surface
    state->surface =
            eglCreateWindowSurface(state->display,
                                   state->config,
                                   (NativeWindowType)platform->window.wlEGLNativeWindow,
                                   srfAttrs);
    if (state->surface == EGL_NO_SURFACE) {
        LOG_ERR("EGL couldn't create window surface.\n");
        return NV_FALSE;
    }
    return NV_TRUE;
}
