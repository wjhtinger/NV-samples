/*
 * nvgldemo_win_wayland.h
 *
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Exposes wayland display system objects created by nvgldemo to applications
//   which demonstrate wayland-specific features.

#ifndef __NVGLDEMO_WIN_WAYLAND_H
#define __NVGLDEMO_WIN_WAYLAND_H

#include <linux/input.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <xkbcommon/xkbcommon.h>
#include "wayland-client.h"
#include "wayland-egl.h"
#include "ivi-application-client-protocol.h"

#define ENABLE_IVI_CONTROLLER 1
#ifdef ENABLE_IVI_CONTROLLER
#include <ilm_common.h>
#include <ilm_client.h>
#include <ilm_control.h>
#endif

#define MOD_SHIFT_MASK    0x01
#define MOD_ALT_MASK    0x02
#define MOD_CONTROL_MASK    0x04

struct Window;

struct Display {
    struct wl_display *wlDisplay;
    struct wl_registry *wlRegistry;
    struct wl_compositor *wlCompositor;
    struct wl_shell *wlShell;
    struct ivi_application *ivi_application;
    uint32_t has_ivi_controller;
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
};

struct Geometry {
    int width, height;
};

struct Window {
    struct Display *display;
    struct wl_egl_window *wlEGLNativeWindow;
    struct wl_surface *wlSurface;
    struct wl_shell_surface *wlShellSurface;
    struct ivi_surface *ivi_surface;
    struct wl_callback *callback;
    int fullscreen, configured, opaque;
    unsigned int ivi_surfaceId;
    struct Geometry geometry,window_size;
};

// Platform-specific state info
struct NvGlDemoPlatformState
{
    struct Display display;
    int screen;
    struct Window window;
};
#endif // __NVGLDEMO_WIN_WAYLAND_H
