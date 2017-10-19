/*
 * Copyright (c) 2015, NVIDIA Corporation.	All Rights Reserved.
 *
 * BY INSTALLING THE SOFTWARE THE USER AGREES TO THE TERMS BELOW.
 *
 * User agrees to use the software under carefully controlled conditions
 * and to inform all employees and contractors who have access to the software
 * that the source code of the software is confidential and proprietary
 * information of NVIDIA and is licensed to user as such.  User acknowledges
 * and agrees that protection of the source code is essential and user shall
 * retain the source code in strict confidence.  User shall restrict access to
 * the source code of the software to those employees and contractors of user
 * who have agreed to be bound by a confidentiality obligation which
 * incorporates the protections and restrictions substantially set forth
 * herein, and who have a need to access the source code in order to carry out
 * the business purpose between NVIDIA and user.  The software provided
 * herewith to user may only be used so long as the software is used solely
 * with NVIDIA products and no other third party products (hardware or
 * software).	The software must carry the NVIDIA copyright notice shown
 * above.  User must not disclose, copy, duplicate, reproduce, modify,
 * publicly display, create derivative works of the software other than as
 * expressly authorized herein.  User must not under any circumstances,
 * distribute or in any way disseminate the information contained in the
 * source code and/or the source code itself to third parties except as
 * expressly agreed to by NVIDIA.  In the event that user discovers any bugs
 * in the software, such bugs must be reported to NVIDIA and any fixes may be
 * inserted into the source code of the software by NVIDIA only.  User shall
 * not modify the source code of the software in any way.  User shall be fully
 * responsible for the conduct of all of its employees, contractors and
 * representatives who may in any way violate these restrictions.
 *
 * NO WARRANTY
 * THE ACCOMPANYING SOFTWARE (INCLUDING OBJECT AND SOURCE CODE) PROVIDED BY
 * NVIDIA TO USER IS PROVIDED "AS IS."	NVIDIA DISCLAIMS ALL WARRANTIES,
 * EXPRESS, IMPLIED OR STATUTORY, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF TITLE, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.

 * LIMITATION OF LIABILITY
 * NVIDIA SHALL NOT BE LIABLE TO USER, USERS CUSTOMERS, OR ANY OTHER PERSON
 * OR ENTITY CLAIMING THROUGH OR UNDER USER FOR ANY LOSS OF PROFITS, INCOME,
 * SAVINGS, OR ANY OTHER CONSEQUENTIAL, INCIDENTAL, SPECIAL, PUNITIVE, DIRECT
 * OR INDIRECT DAMAGES (WHETHER IN AN ACTION IN CONTRACT, TORT OR BASED ON A
 * WARRANTY), EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGES.  THESE LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF THE
 * ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.  IN NO EVENT SHALL NVIDIAS
 * AGGREGATE LIABILITY TO USER OR ANY OTHER PERSON OR ENTITY CLAIMING THROUGH
 * OR UNDER USER EXCEED THE AMOUNT OF MONEY ACTUALLY PAID BY USER TO NVIDIA
 * FOR THE SOFTWARE PROVIDED HEREWITH.
 */

#include <stdio.h>

#include <string.h>
#include <unistd.h>
#include "ogl-wayland.h"

// 100 milliseconds
#define WAYLAND_CONNECT_USLEEP_INC 100 * 1000
// 60 seconds
#define WAYLAND_CONNECT_MAX_SEC 60

typedef struct _WaylandInformation {
	struct wl_compositor *wlCompositor;
	struct wl_shell *wlShell;
} WaylandInformation;

static void globalRegistryHandler(void *data, struct wl_registry *wlRegistry,
	uint32_t name, const char *interface, uint32_t version)
{
	WaylandInformation *info = (WaylandInformation*) data;

	if (strcmp(interface, "wl_compositor") == 0) {
		info->wlCompositor = (struct wl_compositor*) wl_registry_bind(wlRegistry, name,
		&wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_shell") == 0) {
		info->wlShell = (struct wl_shell*) wl_registry_bind(wlRegistry, name,
		&wl_shell_interface, 1);
	}
}

static void globalRegistryHandlerRemove(void *data, struct wl_registry *wlRegistry,
	uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
		globalRegistryHandler,
		globalRegistryHandlerRemove
};

int waylandDisplayInit(struct wl_display **wlDisplay)
{
	int retryTime = 0;

	printf("Connecting to wayland compositor\n");
	printf("Will try up to %d seconds...\n", WAYLAND_CONNECT_MAX_SEC);
	while (!(*wlDisplay = wl_display_connect(NULL))) {
		usleep(WAYLAND_CONNECT_USLEEP_INC);
		retryTime += WAYLAND_CONNECT_USLEEP_INC;
		if (retryTime / 1000000 > WAYLAND_CONNECT_MAX_SEC) {
			printf("Couldn't connect to wayland compositor.\n");
			return 0;
		}
	}
	printf("Connected to wayland compositor.\n");
	return 1;
}

EGLSurface waylandCreateSurface(struct wl_display *wlDisplay, EGLConfig eglConfig,
		EGLDisplay eglDisplay, EGLContext eglContext, int width, int height)
{
	EGLSurface eglSurface;
	struct wl_surface *wlSurface;
	struct wl_shell_surface *wlShellSurface;
	struct wl_egl_window *wlWindow;
	struct wl_registry *registry;
	WaylandInformation waylandInformation = {0};

	registry = wl_display_get_registry(wlDisplay);
	wl_registry_add_listener(registry, &registry_listener, &waylandInformation);
	wl_display_roundtrip(wlDisplay);

	wlSurface = wl_compositor_create_surface(waylandInformation.wlCompositor);
	wlShellSurface = wl_shell_get_shell_surface(waylandInformation.wlShell,
		wlSurface);
	wl_shell_surface_set_toplevel(wlShellSurface);

	wlWindow = wl_egl_window_create(wlSurface, width, height);

	eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig,
		(EGLNativeWindowType)wlWindow, NULL);

	return eglSurface;
}
