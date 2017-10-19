/*
 * Copyright © 2012 John Kåre Alsaker
 * Copyright © 2016-2017 NVIDIA Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include "compositor.h"

#ifdef ENABLE_EGL

#include <EGL/egl.h>
#include <EGL/eglext.h>

#else

typedef int EGLint;
typedef int EGLenum;
typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef void *EGLConfig;
typedef intptr_t EGLNativeDisplayType;
typedef intptr_t EGLNativeWindowType;
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)

#endif /* ENABLE_EGL */

enum gl_renderer_attribute {
	/*
	 * Sets whether all EGL applications should be composited to the primary
	 * plane instead of trying to find an overlay available.
	 */
	GL_RENDERER_ATTR_ALL_EGL_TO_PRIMARY = 1,

	/*
	 * Sets whether atomic page flips should be allowed.
	 */
	GL_RENDERER_ATTR_ALLOW_ATOMIC_PAGE_FLIPS,
};

enum gl_renderer_extension {
	GL_RENDERER_EXTENSION_EGL_STREAM_FIFO = 0,
	GL_RENDERER_EXTENSION_EGL_STREAM_FIFO_SYNCHRONOUS,
	GL_RENDERER_EXTENSION_EGL_STREAM_REMOTE,
};

struct gl_renderer_overlay_data {
	uint32_t id;
	EGLint possible_outputs;
};

#ifndef EGL_EXT_platform_base
typedef EGLDisplay (*PFNEGLGETPLATFORMDISPLAYEXTPROC) (EGLenum platform, void *native_display, const EGLint *attrib_list);
typedef EGLSurface (*PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC) (EGLDisplay dpy, EGLConfig config, void *native_window, const EGLint *attrib_list);
#endif

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif

#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif

#ifndef EGL_PLATFORM_DEVICE_EXT
#define EGL_PLATFORM_DEVICE_EXT 0x313F
#endif

#define NO_EGL_PLATFORM 0

enum gl_renderer_border_side {
	GL_RENDERER_BORDER_TOP = 0,
	GL_RENDERER_BORDER_LEFT = 1,
	GL_RENDERER_BORDER_RIGHT = 2,
	GL_RENDERER_BORDER_BOTTOM = 3,
};

struct gl_renderer_interface {
	const EGLint *opaque_attribs;
	const EGLint *alpha_attribs;
	const EGLint *opaque_stream_attribs;
	const EGLAttrib *egloutput_stream_attribs;

	int (*display_create)(struct weston_compositor *ec,
			      EGLenum platform,
			      void *native_window,
			      const EGLint *platform_attribs,
			      const EGLint *config_attribs,
			      const EGLint *visual_id,
			      const int n_ids);

	EGLDisplay (*display)(struct weston_compositor *ec);

	int (*output_window_create)(struct weston_output *output,
				    EGLNativeWindowType window_for_legacy,
				    void *window_for_platform,
				    const EGLint *config_attribs,
				    const EGLint *visual_id,
				    const int n_ids);

	int (*output_stream_create)(struct weston_output *output, int sock_id,
				    uint32_t plane_id, uint32_t crtc_id,
				    const EGLAttrib *stream_attribs);

	void (*output_destroy)(struct weston_output *output);

	EGLSurface (*output_surface)(struct weston_output *output);

	/* Sets the output border.
	 *
	 * The side specifies the side for which we are setting the border.
	 * The width and height are the width and height of the border.
	 * The tex_width patemeter specifies the width of the actual
	 * texture; this may be larger than width if the data is not
	 * tightly packed.
	 *
	 * The top and bottom textures will extend over the sides to the
	 * full width of the bordered window.  The right and left edges,
	 * however, will extend only to the top and bottom of the
	 * compositor surface.  This is demonstrated by the picture below:
	 *
	 * +-----------------------+
	 * |          TOP          |
	 * +-+-------------------+-+
	 * | |                   | |
	 * |L|                   |R|
	 * |E|                   |I|
	 * |F|                   |G|
	 * |T|                   |H|
	 * | |                   |T|
	 * | |                   | |
	 * +-+-------------------+-+
	 * |        BOTTOM         |
	 * +-----------------------+
	 */
	void (*output_set_border)(struct weston_output *output,
				  enum gl_renderer_border_side side,
				  int32_t width, int32_t height,
				  int32_t tex_width, unsigned char *data);

	void (*print_egl_error_state)(void);

	int (*get_devices)(EGLint max_devices,
			   EGLDeviceEXT *devices,
			   EGLint *num_devices);

	int (*get_drm_device_file)(EGLDeviceEXT device,
				   const char **drm_device_file);

	/*
	 * output_stream_flip() makes the EGLOutput consumer attached to the
	 * corresponding <output> stream acquire the new available frame
	 * (repaint_output() has been called previously) and queue a page flip.
	 * Whenever DRM is the underlying API and EGL_NV_output_drm_flip_event
	 * is supported, page flip notification can be requested by passing a
	 * non-NULL <flip_data> pointer. Otherwise, compositors should rely on a
	 * different mechanism in order to re-schedule output repaints.
	 */
	int (*output_stream_flip)(struct weston_output *output,
				  void *flip_data);

	int (*set_attribute)(struct weston_compositor *ec,
			     enum gl_renderer_attribute attribute,
			     int val);

	int (*has_extension)(struct weston_compositor *ec,
			     enum gl_renderer_extension extension);

	int (*get_assigned_overlay)(struct weston_surface *es, uint32_t *overlay_assigned);

	int (*register_overlays)(struct weston_compositor *c, EGLint num_overlays,
	                         struct gl_renderer_overlay_data *overlay_data);

	int (*overlay_ready_for_atomic_flip)(struct weston_surface *es);

	int (*prepare_overlay_atomic_flip)(struct weston_surface *es);

	int (*overlay_atomic_flip)(struct weston_surface *es,
				   void *atomic_req);

	int (*output_stream_enable_auto_acquire)(struct weston_output *output);
};

