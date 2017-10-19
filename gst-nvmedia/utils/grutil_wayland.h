/*
 * grutil_wayland.h
 *
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Exposes Wayland display system objects which demonstrate wayland-specific features.

#ifndef __GRUTIL_WAYLAND_H
#define __GRUTIL_WAYLAND_H

#include <linux/input.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <xkbcommon/xkbcommon.h>
#include "wayland-client.h"
#include "wayland-egl.h"

#define MOD_SHIFT_MASK    0x01
#define MOD_ALT_MASK      0x02
#define MOD_CONTROL_MASK  0x04

struct Window;

struct Display {
    struct wl_display *wlDisplay;
    struct wl_registry *wlRegistry;
    struct wl_compositor *wlCompositor;
    struct wl_shell *wlShell;
    struct wl_seat *wlSeat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xkb_context *xkb_context;
    struct {
        struct xkb_keymap *keymap;
        struct xkb_state *state;
        xkb_mod_mask_t control_mask;
        xkb_mod_mask_t alt_mask;
        xkb_mod_mask_t shift_mask;
    } xkb;
    uint32_t modifiers;
    uint32_t serial;
    struct sigaction sigint;
    struct Window *window;
    struct ivi_application *ivi_application;
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
    struct ivi_surface *ivi_surface;
};

// Platform-specific state info
struct GrUtilPlatformState
{
    struct Display display;
    int screen;
    struct Window window;
};

#endif // __GRUTIL_WAYLAND_H
