/*
 * grutil_wayland.c
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "grutil.h"
#include "grutil_wayland.h"
#include "ivi-application-client-protocol.h"
#define IVI_SURFACE_ID 9000

static GrUtilCloseCB   closeCB   = NULL;
static GrUtilResizeCB  resizeCB  = NULL;
static GrUtilKeyCB     keyCB     = NULL;
static GrUtilPointerCB pointerCB = NULL;
static GrUtilButtonCB  buttonCB  = NULL;

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

    if(resizeCB) {
       resizeCB(width, height);
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
handle_ivi_surface_configure(void *data, struct ivi_surface *ivi_surface,
                             int32_t width, int32_t height)
{
        struct Window *window = data;

        wl_egl_window_resize(window->wlEGLNativeWindow, width, height, 0, 0);

        window->geometry.width = width;
        window->geometry.height = height;

        if (!window->fullscreen)
                window->window_size = window->geometry;
}


static const struct ivi_surface_listener ivi_surface_listener = {
        handle_ivi_surface_configure,
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

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface,
                     wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
                     uint32_t serial, struct wl_surface *surface)
{
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
                      uint32_t time, wl_fixed_t sx_w, wl_fixed_t sy_w)
{
    if (pointerCB) {
        float sx = wl_fixed_to_double(sx_w);
        float sy = wl_fixed_to_double(sy_w);
        pointerCB(sx, sy);
    }
}

static void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                      uint32_t serial, uint32_t time, uint32_t button,
                      uint32_t state)
{
    if (buttonCB) {
        buttonCB((button == BTN_LEFT) ? 1 : 0,
            (state == WL_POINTER_BUTTON_STATE_PRESSED) ? 1 : 0);
    }
}

static void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
                    uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static const struct wl_pointer_listener pointer_listener =
{
    pointer_handle_enter,
    pointer_handle_leave,
    pointer_handle_motion,
    pointer_handle_button,
    pointer_handle_axis,
};

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
                       uint32_t format, int fd, uint32_t size)
{
    struct Display *input = data;
    struct xkb_keymap *keymap;
    struct xkb_state *state;
    char *map_str;

    if (!data) {
        close(fd);
        return;
    }

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    keymap = xkb_map_new_from_string(input->xkb_context,
                            map_str, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap(map_str, size);
    close(fd);

    if (!keymap) {
        GrUtilLog("failed to compile keymap\n");
        return;
    }

    state = xkb_state_new(keymap);
    if (!state) {
        GrUtilLog("failed to create XKB state\n");
        xkb_map_unref(keymap);
        return;
    }

    xkb_keymap_unref(input->xkb.keymap);
    xkb_state_unref(input->xkb.state);
    input->xkb.keymap = keymap;
    input->xkb.state = state;

    input->xkb.control_mask =
        1 << xkb_map_mod_get_index(input->xkb.keymap, "Control");
    input->xkb.alt_mask =
        1 << xkb_map_mod_get_index(input->xkb.keymap, "Mod1");
    input->xkb.shift_mask =
        1 << xkb_map_mod_get_index(input->xkb.keymap, "Shift");
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface,
                      struct wl_array *keys)
{
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
                      uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
                    uint32_t serial, uint32_t time, uint32_t key,
                    uint32_t state)
{
    struct Display *input = data;
    uint32_t code, num_syms;
    const xkb_keysym_t *syms;
    xkb_keysym_t sym;
    input->serial = serial;
    code = key + 8;
    if (!input->xkb.state) {
        return;
    }

    num_syms = xkb_key_get_syms(input->xkb.state, code, &syms);

    sym = XKB_KEY_NoSymbol;
    if (num_syms == 1) {
        sym = syms[0];
    }

    if (keyCB) {
        if (sym) {
            char buf[16];
            xkb_keysym_to_utf8(sym, &buf[0], 16);
            keyCB(buf[0], (state == WL_KEYBOARD_KEY_STATE_PRESSED)? 1 : 0);
        }
    }
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, uint32_t mods_depressed,
                          uint32_t mods_latched, uint32_t mods_locked,
                          uint32_t group)
{
    struct Display *input = data;
    xkb_mod_mask_t mask;

    /* If we're not using a keymap, then we don't handle PC-style modifiers */
    if (!input->xkb.keymap) {
        return;
    }

    xkb_state_update_mask(input->xkb.state, mods_depressed, mods_latched,
        mods_locked, 0, 0, group);

    mask = xkb_state_serialize_mods(input->xkb.state,
        XKB_STATE_DEPRESSED |
        XKB_STATE_LATCHED);
    input->modifiers = 0;

    if (mask & input->xkb.control_mask) {
        input->modifiers |= MOD_CONTROL_MASK;
    }
    if (mask & input->xkb.alt_mask) {
        input->modifiers |= MOD_ALT_MASK;
    }
    if (mask & input->xkb.shift_mask) {
        input->modifiers |= MOD_SHIFT_MASK;
    }
}

static const struct wl_keyboard_listener keyboard_listener =
{
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
                         enum wl_seat_capability caps)
{
    struct Display *d = data;

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !d->pointer) {
        d->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(d->pointer, &pointer_listener, d);
    }

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !d->keyboard) {
        d->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(d->keyboard, &keyboard_listener, d);
    }
}

static const struct wl_seat_listener seat_listener =
{
    seat_handle_capabilities,
};

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
        if (d->wlShell == NULL) {
            GrUtilLog("Failed to registry bind wlShell!!!!\n");
            return;
        }
    } else if (strcmp(interface, "wl_seat") == 0) {
        d->wlSeat = wl_registry_bind(registry, name,
                                &wl_seat_interface, 1);
        wl_seat_add_listener(d->wlSeat, &seat_listener, d);
    } else if (strcmp(interface, "ivi_application") == 0) {
        d->ivi_application = wl_registry_bind(registry, name,
                                &ivi_application_interface, 1);
        if (d->ivi_application == NULL) {
            GrUtilLog("Failed to registry bind ivi_application\n");
            return;
        }
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

static void
signal_int(int signum)
{
    if (closeCB) {
        closeCB();
    }
}


int GrUtilDisplayInit(void)
{
    // Allocate a structure for the platform-specific state
    grUtilState.platform =
        (GrUtilPlatformState*)malloc(sizeof(GrUtilPlatformState));

    if (!grUtilState.platform) {
        GrUtilLog("Could not allocate platform specific storage memory.\n");
        goto fail;
    }

    memset(grUtilState.platform, 0, sizeof(GrUtilPlatformState));

    grUtilState.platform->window.display = &grUtilState.platform->display;
    grUtilState.platform->display.window = &grUtilState.platform->window;

    // Open WAYLAND display
    grUtilState.platform->display.wlDisplay = wl_display_connect(NULL);
    if (grUtilState.platform->display.wlDisplay == NULL) {
        GrUtilLog("connect to a wayland socket failed.\n");
        goto fail;
    }

    grUtilState.platform->display.xkb_context = xkb_context_new(0);
    if (grUtilState.platform->display.xkb_context == NULL) {
        GrUtilLog("Failed to create XKB context\n");
        goto fail;
    }

    grUtilState.platform->display.wlRegistry = wl_display_get_registry(
                                   grUtilState.platform->display.wlDisplay);
    if (grUtilState.platform->display.wlRegistry == NULL) {
        GrUtilLog("Failed to get registry.\n");
        goto fail;
    }

    wl_registry_add_listener(grUtilState.platform->display.wlRegistry,
                   &registry_listener, &grUtilState.platform->display);

    wl_display_dispatch(grUtilState.platform->display.wlDisplay);

    grUtilState.nativeDisplay = (NativeDisplayType)grUtilState.platform->display.wlDisplay;

    // If display option is specified, but isn't supported, then exit.
    if (grUtilOptions.displayName[0]) {
        GrUtilLog("Setting display output is not supported. Exiting.\n");
        goto fail;
    }

    if (grUtilOptions.displayRate) {
        GrUtilLog("Setting display refresh is not supported. Exiting.\n");
        goto fail;
    }

    if ((grUtilOptions.displayBlend >= GrUtilDisplayBlend_None) ||
        (grUtilOptions.displayAlpha >= 0.0) ||
        (grUtilOptions.displayColorKey[0] >= 0.0) ||
        (grUtilOptions.displayLayer > 0)) {
        GrUtilLog("Display layers are not supported. Exiting.\n");
        goto fail;
    }

    // But do not fail on the unsupported "displaysize" and "desktopsize"
    // options (that are also a side effect of the "res" option).
    // We will ignore these on WAYLAND.

    if (grUtilOptions.displaySize[0]) {
        GrUtilLog("Setting display size is not supported. Ignoring.\n");
    }

    if (grUtilOptions.desktopSize[0]) {
        GrUtilLog("Setting desktop size is not supported. Ignoring.\n");
    }

    grUtilState.platform->display.sigint.sa_handler = signal_int;
    sigemptyset(&grUtilState.platform->display.sigint.sa_mask);
    grUtilState.platform->display.sigint.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &grUtilState.platform->display.sigint, NULL);

    grUtilState.display = eglGetDisplay(grUtilState.nativeDisplay);

    return 1;

fail:
    GrUtilDisplayTerm();
    return 0;
}

void GrUtilDisplayTerm(void)
{
    if (!grUtilState.platform) {
        return;
    }

    if (grUtilState.platform->display.wlRegistry) {
        wl_registry_destroy(grUtilState.platform->display.wlRegistry);
    }

    if (grUtilState.platform->display.wlSeat) {
        wl_seat_destroy(grUtilState.platform->display.wlSeat);
    }

    if (grUtilState.platform->display.wlShell) {
        wl_shell_destroy(grUtilState.platform->display.wlShell);
    }

    if (grUtilState.platform->display.wlCompositor) {
        wl_compositor_destroy(grUtilState.platform->display.wlCompositor);
    }

    // Explicitly destroy pointer object as traditional(weston clients) way
    // to destroy them in seat_handle_capabilies (a wl_seat_listner) is not
    // triggered for reasons still to be discovered.
    if (grUtilState.platform->display.pointer) {
        wl_pointer_destroy(grUtilState.platform->display.pointer);
        grUtilState.platform->display.pointer = NULL;
    }

    // Explicitly destroy keyboard object as traditional(weston clients) way
    // to destroy them in seat_handle_capabilies (a wl_seat_listner) is not
    // triggered for reasons still to be discovered.
    if (grUtilState.platform->display.keyboard) {
        wl_keyboard_destroy(grUtilState.platform->display.keyboard);
        grUtilState.platform->display.keyboard = NULL;
    }

    if (grUtilState.platform->display.xkb_context) {

        if (grUtilState.platform->display.xkb.keymap) {
            xkb_keymap_unref(grUtilState.platform->display.xkb.keymap);
        }

        if (grUtilState.platform->display.xkb.state) {
            xkb_state_unref(grUtilState.platform->display.xkb.state);
        }

        xkb_context_unref(grUtilState.platform->display.xkb_context);
    }

    if (grUtilState.platform->display.wlDisplay) {
        wl_display_roundtrip(grUtilState.platform->display.wlDisplay);
        wl_display_flush(grUtilState.platform->display.wlDisplay);
        wl_display_disconnect(grUtilState.platform->display.wlDisplay);
        grUtilState.platform->display.wlDisplay = NULL;
    }

    free(grUtilState.platform);
    grUtilState.platform = NULL;
}

int GrUtilWindowInit(int *argc, char **argv, const char *appName)
{
    if (!grUtilState.platform) {
        return 0;
    }

    if (!grUtilState.platform->display.wlCompositor ||
            (!grUtilState.platform->display.wlShell &&
             !grUtilState.platform->display.ivi_application)) {
        return 0;
    }

    // If not specified, use default window size
    if (!grUtilOptions.windowSize[0])
        grUtilOptions.windowSize[0] = 800;
    if (!grUtilOptions.windowSize[1])
        grUtilOptions.windowSize[1] = 480;

    grUtilState.platform->window.wlSurface  =
       wl_compositor_create_surface(grUtilState.platform->display.wlCompositor);
    if (grUtilState.platform->window.wlSurface == NULL) {
        GrUtilLog("Failed to create wayland surface.\n");
        goto fail;
    }

    if (grUtilState.platform->display.wlShell) {
        grUtilState.platform->window.wlShellSurface =
        wl_shell_get_shell_surface(grUtilState.platform->display.wlShell,
        grUtilState.platform->window.wlSurface);
       if (grUtilState.platform->window.wlShellSurface == NULL) {
           GrUtilLog("Failed to create wayland shell surface.\n");
           goto fail;
       }
       wl_shell_surface_add_listener(grUtilState.platform->window.wlShellSurface,
           &shell_surface_listener, &grUtilState.platform->window);
    }

    grUtilState.platform->window.geometry.width = grUtilOptions.windowSize[0];
    grUtilState.platform->window.geometry.height = grUtilOptions.windowSize[1];
    grUtilState.platform->window.window_size = grUtilState.platform->window.geometry;
    grUtilState.platform->window.wlEGLNativeWindow =
        wl_egl_window_create(grUtilState.platform->window.wlSurface,
                             grUtilState.platform->window.geometry.width,
                             grUtilState.platform->window.geometry.height);
    if (grUtilState.platform->window.wlEGLNativeWindow == NULL) {
        GrUtilLog("Failed to create wayland EGL window.\n");
        goto fail;
    }
    uint32_t id_ivisurface = IVI_SURFACE_ID + (uint32_t)getpid();
    if (grUtilState.platform->display.ivi_application) {
        grUtilState.platform->window.ivi_surface =
            ivi_application_surface_create(grUtilState.platform->display.ivi_application,
                                            id_ivisurface,
                                            grUtilState.platform->window.wlSurface);

        if (grUtilState.platform->window.ivi_surface == NULL)
            GrUtilLog("Failed to create ivi_surface.\n");
        ivi_surface_add_listener(grUtilState.platform->window.ivi_surface,
                                         &ivi_surface_listener, &grUtilState.platform->window);
    }
    if (grUtilState.platform->window.wlShellSurface && !(grUtilState.platform->window.ivi_surface))
        toggle_fullscreen(&grUtilState.platform->window, grUtilState.platform->window.fullscreen);

    grUtilState.nativeWindow = (NativeWindowType)grUtilState.platform->window.wlEGLNativeWindow;

    return 1;

fail:
    GrUtilWindowTerm();
    return 0;
}

void GrUtilWindowTerm(void)
{
    if (!grUtilState.platform) {
        return;
    }

    // Close the native window
    if (grUtilState.platform->window.ivi_surface)
        ivi_surface_destroy(grUtilState.platform->window.ivi_surface);

    if (grUtilState.platform->display.ivi_application)
        ivi_application_destroy(grUtilState.platform->display.ivi_application);

    if (grUtilState.platform->window.wlEGLNativeWindow) {
        wl_egl_window_destroy(grUtilState.platform->window.wlEGLNativeWindow);
        grUtilState.platform->window.wlEGLNativeWindow = 0;
    }

    if (grUtilState.platform->window.wlShellSurface) {
        wl_shell_surface_destroy(grUtilState.platform->window.wlShellSurface);
        grUtilState.platform->window.wlShellSurface = 0;
    }

    if (grUtilState.platform->window.wlSurface) {
        wl_surface_destroy(grUtilState.platform->window.wlSurface);
        grUtilState.platform->window.wlSurface = 0;
    }

    if (grUtilState.platform->window.callback) {
        wl_callback_destroy(grUtilState.platform->window.callback);
        grUtilState.platform->window.callback = 0;
    }

    grUtilState.nativeWindow = 0;
}

int GrUtilCreatOutputSurface(void)
{
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
    cfgAttrs[cfgAttrIndex++] = grUtilOptions.msaa;

    // Terminate attribute lists
    cfgAttrs[cfgAttrIndex++] = EGL_NONE;
    srfAttrs[srfAttrIndex++] = EGL_NONE;

    // Find out how many configurations suit our needs
    eglStatus = eglChooseConfig(grUtilState.display, cfgAttrs,
                                NULL, 0, &configCount);
    if (!eglStatus || !configCount) {
        GrUtilLog("EGL failed to return any matching configurations.\n");
        goto fail;
    }

    // Allocate room for the list of matching configurations
    configList = (EGLConfig*)malloc(configCount * sizeof(EGLConfig));
    if (!configList) {
        GrUtilLog("Allocation failure obtaining configuration list.\n");
        goto fail;
    }

    // Obtain the configuration list from EGL
    eglStatus = eglChooseConfig(grUtilState.display, cfgAttrs,
                                configList, configCount, &configCount);
    if (!eglStatus || !configCount) {
        GrUtilLog("EGL failed to populate configuration list.\n");
        goto fail;
    }

    // Select an EGL configuration that matches the native window
    // Currently we just choose the first one, but we could search
    //   the list based on other criteria.
    grUtilState.config = configList[0];
    free(configList);
    configList = 0;

    // Create EGL surface
    grUtilState.surface =
            eglCreateWindowSurface(grUtilState.display,
                               grUtilState.config,
                               grUtilState.nativeWindow,
                               srfAttrs);
    if (grUtilState.surface == EGL_NO_SURFACE) {
        GrUtilLog("EGL couldn't create window surface.\n");
        goto fail;
    }

    return 1;
fail:
    if (configList) free(configList);
    return 0;
}


//
// Callback handling
//

void GrUtilSetCloseCB(GrUtilCloseCB cb)
{
    closeCB  = cb;
}

void GrUtilSetResizeCB(GrUtilResizeCB cb)
{
    resizeCB = cb;
}

void GrUtilSetKeyCB(GrUtilKeyCB cb)
{
    keyCB = cb;
}

void GrUtilSetPointerCB(GrUtilPointerCB cb)
{
    pointerCB = cb;
}

void GrUtilSetButtonCB(GrUtilButtonCB cb)
{
    buttonCB = cb;
}

void GrUtilCheckEvents(void)
{
    if (grUtilState.platform && grUtilState.platform->display.wlDisplay) {
        wl_display_dispatch_pending(grUtilState.platform->display.wlDisplay);
    }
}
