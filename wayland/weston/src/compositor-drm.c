/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
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

#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/vt.h>
#include <assert.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <time.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#ifdef USE_GBM
#include <gbm.h>
#endif
#include <libudev.h>

#include "compositor.h"
#include "compositor-drm.h"
#include "shared/helpers.h"
#include "shared/timespec-util.h"
#include "gl-renderer.h"
#include "pixman-renderer.h"
#include "libbacklight.h"
#include "libinput-seat.h"
#include "launcher-util.h"
#include "vaapi-recorder.h"
#include "presentation-time-server-protocol.h"
#include "linux-dmabuf.h"

#ifndef DRM_CAP_TIMESTAMP_MONOTONIC
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x6
#endif

#ifndef DRM_CLIENT_CAP_ATOMIC
#define DRM_CLIENT_CAP_ATOMIC 3
#endif

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif

#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

#ifndef GBM_BO_USE_CURSOR
#define GBM_BO_USE_CURSOR GBM_BO_USE_CURSOR_64X64
#endif

#ifndef EGL_DRM_MASTER_FD_EXT
#define EGL_DRM_MASTER_FD_EXT 0x333C
#endif

/*
 * Padding set to 10 because it is the spacing used in drm-nvdc.
 * Nothing special about the number, just needs some space between
 * layers so they can be reordered flexibly
 */
#define ZPOS_PADDING 10
#define MAX_ZPOS 255

#define MAX_ALPHA 255

#define REPAINT_TIME_MS 5
/*
 * List of properties attached to DRM planes and corresponding names
 */
enum wplane_property {
	WPLANE_PROP_SRC_X = 0,
	WPLANE_PROP_SRC_Y,
	WPLANE_PROP_SRC_W,
	WPLANE_PROP_SRC_H,
	WPLANE_PROP_CRTC_X,
	WPLANE_PROP_CRTC_Y,
	WPLANE_PROP_CRTC_W,
	WPLANE_PROP_CRTC_H,
	WPLANE_PROP_CRTC_ID,
	WPLANE_PROP_FB_ID,
	WPLANE_PROP_ZPOS,
	WPLANE_PROP_ALPHA,

	WPLANE_PROP_COUNT
};

static const char * const wplane_prop_names[] = {
	[WPLANE_PROP_SRC_X]   = "SRC_X",
	[WPLANE_PROP_SRC_Y]   = "SRC_Y",
	[WPLANE_PROP_SRC_W]   = "SRC_W",
	[WPLANE_PROP_SRC_H]   = "SRC_H",
	[WPLANE_PROP_CRTC_X]  = "CRTC_X",
	[WPLANE_PROP_CRTC_Y]  = "CRTC_Y",
	[WPLANE_PROP_CRTC_W]  = "CRTC_W",
	[WPLANE_PROP_CRTC_H]  = "CRTC_H",
	[WPLANE_PROP_CRTC_ID] = "CRTC_ID",
	[WPLANE_PROP_FB_ID]   = "FB_ID",
	[WPLANE_PROP_ZPOS]    = "zpos",
	[WPLANE_PROP_ALPHA]   = "alpha"
};

/*
 * Holding structure for DRM plane properties
 */
struct wplane_properties {
	uint32_t ids[WPLANE_PROP_COUNT];
	uint32_t valid;
};

struct drm_backend {
	struct weston_backend base;
	struct weston_compositor *compositor;

	struct udev *udev;
	struct wl_event_source *drm_source;

	struct udev_monitor *udev_monitor;
	struct wl_event_source *udev_drm_source;

	struct {
		int id;
		int fd;
		char *filename;
	} drm;

	EGLDeviceEXT egldevice;
	struct gbm_device *gbm;

	uint32_t *crtcs;
	int num_crtcs;
	uint32_t crtc_allocator;
	uint32_t connector_allocator;
	struct wl_listener session_listener;
	uint32_t gbm_format;

	/* we need these parameters in order to not fail drmModeAddFB2()
	 * due to out of bounds dimensions, and then mistakenly set
	 * sprites_are_broken:
	 */
	uint32_t min_width, max_width;
	uint32_t min_height, max_height;
	int no_addfb2;

	struct wl_list sprite_list;
	int sprites_are_broken;
	int sprites_hidden;

	int cursors_are_broken;

	int atomic_modeset;

	int use_pixman;
	int use_egldevice;
	int preferred_plane;

	uint32_t prev_state;

	struct udev_input input;

	int32_t cursor_width;
	int32_t cursor_height;

        /** Callback used to configure the outputs.
	 *
         * This function will be called by the backend when a new DRM
         * output needs to be configured.
         */
        enum weston_drm_backend_output_mode
	(*configure_output)(struct weston_compositor *compositor,
			    int32_t use_current_mode,
			    const char *name,
			    struct weston_drm_backend_output_config *output_config);
	int32_t use_current_mode;
	bool drm_nvdc;
};

struct drm_mode {
	struct weston_mode base;
	drmModeModeInfo mode_info;
};

struct drm_fb {
	uint32_t fb_id, stride, handle, size;
	int fd;
	int is_client_buffer;
	struct weston_buffer_reference buffer_ref;

	/* Used by gbm fbs */
	struct gbm_bo *bo;

	/* Used by dumb fbs */
	void *map;
};

struct drm_bo {
	uint32_t pitch;
	uint32_t bo_handle;
	uint32_t size;
	uint8_t *buf;
};

struct drm_edid {
	char eisa_id[13];
	char monitor_name[13];
	char pnp_id[5];
	char serial_number[13];
};

struct drm_output {
	struct weston_output   base;

	struct wl_event_source *finish_timer_source;

	uint32_t crtc_id;
	int pipe;
	uint32_t plane_id;
	uint32_t connector_id;
	drmModeCrtcPtr original_crtc;
	struct drm_edid edid;
	drmModePropertyPtr dpms_prop;
	uint32_t gbm_format;

	enum dpms_enum dpms;

	int vblank_pending;
	int page_flip_pending;
	int destroy_pending;

	struct gbm_surface *gbm_surface;
	struct gbm_bo *gbm_cursor_bo[2];
	struct weston_plane cursor_plane;
	struct weston_plane fb_plane;
	struct weston_view *cursor_view;
	int current_cursor;
	struct drm_fb *current, *next;
	struct backlight *backlight;

	struct drm_fb *dumb[2];
	pixman_image_t *image[2];
	int current_image;
	pixman_region32_t previous_damage;

	struct vaapi_recorder *recorder;
	struct wl_listener recorder_frame_listener;
	struct drm_bo cursors[2];
};

/*
 * An output has a primary display plane plus zero or more sprites for
 * blending display contents.
 */
struct drm_sprite {
	struct wl_list link;

	struct weston_plane plane;

	struct drm_fb *current, *next;
	struct drm_output *output;
	struct drm_backend *backend;

	uint32_t possible_crtcs;
	uint32_t plane_id;
	uint32_t count_formats;

	int32_t src_x, src_y;
	uint32_t src_w, src_h;
	uint32_t dest_x, dest_y;
	uint32_t dest_w, dest_h;
	uint32_t zpos, alpha;

	struct wplane_properties props;

	uint32_t update_sprite;
	uint32_t atomic_page_flip;
	uint32_t disabled;
	uint32_t used;

	uint32_t formats[];
};

static struct gl_renderer_interface *gl_renderer;

static const char default_seat[] = "seat0";

static void
drm_output_set_cursor(struct drm_output *output);

static void
drm_output_update_msc(struct drm_output *output, unsigned int seq);

static int
drm_sprite_crtc_supported(struct drm_output *output, uint32_t supported)
{
	struct weston_compositor *ec = output->base.compositor;
	struct drm_backend *b =(struct drm_backend *)ec->backend;
	int crtc;

	for (crtc = 0; crtc < b->num_crtcs; crtc++) {
		if (b->crtcs[crtc] != output->crtc_id)
			continue;

		if (supported & (1 << crtc))
			return -1;
	}

	return 0;
}

static void
disable_sprite(struct drm_sprite *sprite)
{
	struct drm_backend *b;
	struct drm_output *output;

	b = sprite->backend;
	output = sprite->output;

	if (sprite->disabled)
		return;

	drmModeSetPlane(b->drm.fd,
			sprite->plane_id,
			output->crtc_id, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0);

	sprite->disabled = 1;

	/*
	 * Make sure drmModeSetPlane() will be called the next time we use
	 * this sprite.
	 */
	sprite->update_sprite = 1;

	/*
	 * Reset damage and clip extents to prevent them from expanding from the
	 * previous application's geometry.
	 */
	pixman_region32_init(&sprite->plane.damage);
	pixman_region32_init(&sprite->plane.clip);
}

#ifdef USE_GBM
static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = data;
	struct gbm_device *gbm = gbm_bo_get_device(bo);

	if (fb->fb_id)
		drmModeRmFB(gbm_device_get_fd(gbm), fb->fb_id);

	weston_buffer_reference(&fb->buffer_ref, NULL);

	free(data);
}
#endif

static int drm_dumb_buffer_create(struct drm_backend *b, int width,
				  int height, int bpp,
				  struct drm_bo *bo) //OUT
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	struct drm_mode_map_dumb mreq;
	int ret;
	uint8_t* map = NULL;

	/* create dumb buffer */
	memset(&creq, 0, sizeof(creq));
	creq.width = width;
	creq.height = height;
	creq.bpp = bpp;
	ret = drmIoctl(b->drm.fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		printf("cannot create dumb buffer\n");
		return 0;
	}

	/* prepare buffer for memory mapping */
	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = creq.handle;
	ret = drmIoctl(b->drm.fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		printf("cannot map dumb buffer\n");
		goto err_destroy;
	}

	/* perform actual memory mapping */
	if (b->drm_nvdc) {
		/* Workaround a wart in drm-nvdc caused by being a userspace
		 * drm driver
		 */
		map = (uint8_t*)(mreq.offset);
	} else {
		map = (uint8_t*)mmap(0, creq.size, PROT_READ | PROT_WRITE,
				     MAP_SHARED, b->drm.fd, mreq.offset);
		if (map == MAP_FAILED) {
			printf("cannot mmap dumb buffer\n");
			goto err_destroy;
		}
	}

	/* clear the buffer object */
	memset(map, 0, creq.size);

	bo->bo_handle = creq.handle;
	bo->pitch = creq.pitch;
	bo->size = creq.size;
	bo->buf = map;

	return 1;

err_destroy:
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = creq.handle;
	drmIoctl(b->drm.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

	return 0;
}

static struct drm_fb *
drm_fb_create_dumb(struct drm_backend *b, unsigned width, unsigned height)
{
	struct drm_fb *fb;
	int ret;
	struct drm_bo bo;

	fb = zalloc(sizeof *fb);
	if (!fb)
		return NULL;

	ret = drm_dumb_buffer_create(b, width, height, 32, &bo);

	if(!ret)
		goto err_fb;

	fb->handle = bo.bo_handle;
	fb->stride = bo.pitch;
	fb->size = bo.size;
	fb->map = bo.buf;
	fb->fd = b->drm.fd;

	ret = drmModeAddFB(b->drm.fd, width, height, 24, 32,
			   fb->stride, fb->handle, &fb->fb_id);

	if(ret)
		goto err_add_fb;

	return fb;

err_add_fb:
	drmModeRmFB(b->drm.fd, fb->fb_id);
err_fb:
	free(fb);
	return NULL;
}

static void drm_dumb_buffer_destroy(int fd, struct drm_bo *bo)
{
	struct drm_mode_destroy_dumb dreq;

	munmap(bo->buf, bo->size);
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = bo->bo_handle;

	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
}

static void
drm_fb_destroy_dumb(struct drm_fb *fb)
{
	struct drm_bo bo;

	if (!fb->map)
		return;

	if (fb->fb_id)
		drmModeRmFB(fb->fd, fb->fb_id);

	weston_buffer_reference(&fb->buffer_ref, NULL);

	bo.buf = fb->map;
	bo.size = fb->size;
	bo.bo_handle = fb->handle;

	drm_dumb_buffer_destroy(fb->fd, &bo);
	free(fb);
}


/*
 * Add plane DRM property to the given atomic request
 */
static int
atomic_add_plane_prop(drmModeAtomicReqPtr req,
		      struct drm_sprite *sprite,
		      enum wplane_property prop,
		      uint64_t value)
{
	if ((sprite->props.ids[prop] == 0) ||
	    !(sprite->props.valid & (1 << prop)))
		return -1;

	return drmModeAtomicAddProperty(req,
					sprite->plane_id,
					sprite->props.ids[prop],
					value);
}

static void
atomic_fill_plane_props(drmModeAtomicReqPtr req,
			struct drm_output *o,
			struct drm_sprite *s)
{
	int ret = 0;

	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_SRC_X,        s->src_x);
	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_SRC_Y,        s->src_y);
	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_SRC_W,        s->src_w);
	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_SRC_H,        s->src_h);
	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_CRTC_X,       s->dest_x);
	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_CRTC_Y,       s->dest_y);
	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_CRTC_W,       s->dest_w);
	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_CRTC_H,       s->dest_h);
	ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_CRTC_ID,      o->crtc_id);
	if (s->props.valid & (1 << WPLANE_PROP_ZPOS))
		ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_ZPOS, s->zpos);
	if (s->props.valid & (1 << WPLANE_PROP_ALPHA))
		ret |= atomic_add_plane_prop(req, s, WPLANE_PROP_ALPHA, s->alpha);

	if (ret)
		weston_log("Unable to add all properties for plane %u\n", s->plane_id);
}

#ifdef USE_GBM
static struct drm_fb *
drm_fb_get_from_bo(struct gbm_bo *bo,
		   struct drm_backend *backend, uint32_t format)
{
	struct drm_fb *fb = gbm_bo_get_user_data(bo);
	uint32_t width, height;
	uint32_t handles[4], pitches[4], offsets[4];
	int ret;

	if (fb)
		return fb;

	fb = zalloc(sizeof *fb);
	if (fb == NULL)
		return NULL;

	fb->bo = bo;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	fb->stride = gbm_bo_get_stride(bo);
	fb->handle = gbm_bo_get_handle(bo).u32;
	fb->size = fb->stride * height;
	fb->fd = backend->drm.fd;

	if (backend->min_width > width || width > backend->max_width ||
	    backend->min_height > height ||
	    height > backend->max_height) {
		weston_log("bo geometry out of bounds\n");
		goto err_free;
	}

	ret = -1;

	if (format && !backend->no_addfb2) {
		handles[0] = fb->handle;
		pitches[0] = fb->stride;
		offsets[0] = 0;

		ret = drmModeAddFB2(backend->drm.fd, width, height,
				    format, handles, pitches, offsets,
				    &fb->fb_id, 0);
		if (ret) {
			weston_log("addfb2 failed: %m\n");
			backend->no_addfb2 = 1;
			backend->sprites_are_broken = 1;
		}
	}

	if (ret)
		ret = drmModeAddFB(backend->drm.fd, width, height, 24, 32,
				   fb->stride, fb->handle, &fb->fb_id);

	if (ret) {
		weston_log("failed to create kms fb: %m\n");
		goto err_free;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

	return fb;

err_free:
	free(fb);
	return NULL;
}
#endif

static void
drm_fb_set_buffer(struct drm_fb *fb, struct weston_buffer *buffer)
{
	assert(fb->buffer_ref.buffer == NULL);

	fb->is_client_buffer = 1;

	weston_buffer_reference(&fb->buffer_ref, buffer);
}

static void
drm_output_release_fb(struct drm_output *output, struct drm_fb *fb)
{
	if (!fb)
		return;

	if (fb->map &&
            (fb != output->dumb[0] && fb != output->dumb[1])) {
		drm_fb_destroy_dumb(fb);
	} else if (fb->bo) {
#ifdef USE_GBM
		if (fb->is_client_buffer)
			gbm_bo_destroy(fb->bo);
		else
			gbm_surface_release_buffer(output->gbm_surface,
						   fb->bo);
#endif
	}
}

#ifdef USE_GBM
static uint32_t
drm_output_check_scanout_format(struct drm_output *output,
				struct weston_surface *es, struct gbm_bo *bo)
{
	uint32_t format;
	pixman_region32_t r;

	format = gbm_bo_get_format(bo);

	if (format == GBM_FORMAT_ARGB8888) {
		/* We can scanout an ARGB buffer if the surface's
		 * opaque region covers the whole output, but we have
		 * to use XRGB as the KMS format code. */
		pixman_region32_init_rect(&r, 0, 0,
					  output->base.width,
					  output->base.height);
		pixman_region32_subtract(&r, &r, &es->opaque);

		if (!pixman_region32_not_empty(&r))
			format = GBM_FORMAT_XRGB8888;

		pixman_region32_fini(&r);
	}

	if (output->gbm_format == format)
		return format;

	return 0;
}
#endif

static struct weston_plane *
drm_output_prepare_scanout_view(struct drm_output *output,
				struct weston_view *ev)
{
#ifdef USE_GBM
	struct drm_backend *b =
		(struct drm_backend *)output->base.compositor->backend;
	struct weston_buffer *buffer = ev->surface->buffer_ref.buffer;
	struct weston_buffer_viewport *viewport = &ev->surface->buffer_viewport;
	struct gbm_bo *bo;
	uint32_t format;

	if (ev->geometry.x != output->base.x ||
	    ev->geometry.y != output->base.y ||
	    buffer == NULL || b->gbm == NULL ||
	    buffer->width != output->base.current_mode->width ||
	    buffer->height != output->base.current_mode->height ||
	    output->base.transform != viewport->buffer.transform ||
	    ev->transform.enabled)
		return NULL;

	if (ev->geometry.scissor_enabled)
		return NULL;

	bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_WL_BUFFER,
			   buffer->resource, GBM_BO_USE_SCANOUT);

	/* Unable to use the buffer for scanout */
	if (!bo)
		return NULL;

	format = drm_output_check_scanout_format(output, ev->surface, bo);
	if (format == 0) {
		gbm_bo_destroy(bo);
		return NULL;
	}

	output->next = drm_fb_get_from_bo(bo, b, format);
	if (!output->next) {
		gbm_bo_destroy(bo);
		return NULL;
	}

	drm_fb_set_buffer(output->next, buffer);

	return &output->fb_plane;
#else
	return NULL;
#endif
}

static void
drm_output_render_gl(struct drm_output *output, pixman_region32_t *damage)
{
	struct drm_backend *b =
		(struct drm_backend *)output->base.compositor->backend;
	struct gbm_bo *bo;

	output->base.compositor->renderer->repaint_output(&output->base,
							  damage);

	if (b->use_egldevice)
		output->next = output->dumb[0];
#ifdef USE_GBM
	else {
		bo = gbm_surface_lock_front_buffer(output->gbm_surface);
		if (!bo) {
			weston_log("failed to lock front buffer: %m\n");
			return;
		}

		output->next = drm_fb_get_from_bo(bo, b, output->gbm_format);
		if (!output->next) {
			weston_log("failed to get drm_fb for bo\n");
			gbm_surface_release_buffer(output->gbm_surface, bo);
			return;
		}
	}
#endif
}

static void
drm_output_render_pixman(struct drm_output *output, pixman_region32_t *damage)
{
	struct weston_compositor *ec = output->base.compositor;
	pixman_region32_t total_damage, previous_damage;

	pixman_region32_init(&total_damage);
	pixman_region32_init(&previous_damage);

	pixman_region32_copy(&previous_damage, damage);

	pixman_region32_union(&total_damage, damage, &output->previous_damage);
	pixman_region32_copy(&output->previous_damage, &previous_damage);

	output->current_image ^= 1;

	output->next = output->dumb[output->current_image];
	pixman_renderer_output_set_buffer(&output->base,
					  output->image[output->current_image]);

	ec->renderer->repaint_output(&output->base, &total_damage);

	pixman_region32_fini(&total_damage);
	pixman_region32_fini(&previous_damage);
}

static void
drm_output_render(struct drm_output *output, pixman_region32_t *damage)
{
	struct weston_compositor *c = output->base.compositor;
	struct drm_backend *b = (struct drm_backend *)c->backend;

	if (b->use_pixman)
		drm_output_render_pixman(output, damage);
	else
		drm_output_render_gl(output, damage);

	pixman_region32_subtract(&c->primary_plane.damage,
				 &c->primary_plane.damage, damage);
}

static void
drm_output_set_gamma(struct weston_output *output_base,
		     uint16_t size, uint16_t *r, uint16_t *g, uint16_t *b)
{
	int rc;
	struct drm_output *output = (struct drm_output *) output_base;
	struct drm_backend *backend =
		(struct drm_backend *) output->base.compositor->backend;

	/* check */
	if (output_base->gamma_size != size)
		return;
	if (!output->original_crtc)
		return;

	rc = drmModeCrtcSetGamma(backend->drm.fd,
				 output->crtc_id,
				 size, r, g, b);
	if (rc)
		weston_log("set gamma failed: %m\n");
}

/* Determine the type of vblank synchronization to use for the output.
 *
 * The pipe parameter indicates which CRTC is in use.  Knowing this, we
 * can determine which vblank sequence type to use for it.  Traditional
 * cards had only two CRTCs, with CRTC 0 using no special flags, and
 * CRTC 1 using DRM_VBLANK_SECONDARY.  The first bit of the pipe
 * parameter indicates this.
 *
 * Bits 1-5 of the pipe parameter are 5 bit wide pipe number between
 * 0-31.  If this is non-zero it indicates we're dealing with a
 * multi-gpu situation and we need to calculate the vblank sync
 * using DRM_BLANK_HIGH_CRTC_MASK.
 */
static unsigned int
drm_waitvblank_pipe(struct drm_output *output)
{
	if (output->pipe > 1)
		return (output->pipe << DRM_VBLANK_HIGH_CRTC_SHIFT) &
				DRM_VBLANK_HIGH_CRTC_MASK;
	else if (output->pipe > 0)
		return DRM_VBLANK_SECONDARY;
	else
		return 0;
}

static struct weston_surface*
get_surface_from_overlay_id(struct weston_output *output,
			    uint32_t overlay_id)
{
	struct weston_view *ev, *next;

	wl_list_for_each_safe(ev, next, &output->compositor->view_list, link) {
		uint32_t assigned;
		uint32_t id;

		id = gl_renderer->get_assigned_overlay(ev->surface, &assigned);
		if (assigned && (id == overlay_id))
			return ev->surface;
	}

	return NULL;
}

static int
drm_output_repaint(struct weston_output *output_base,
		   pixman_region32_t *damage)
{
	struct drm_output *output = (struct drm_output *) output_base;
	struct drm_backend *backend =
		(struct drm_backend *)output->base.compositor->backend;
	struct drm_sprite *s;
	struct drm_mode *mode;
	int ret = 0;
	uint32_t fb_id = 0;

	if (output->destroy_pending)
		return -1;

	if (!output->next)
		drm_output_render(output, damage);
	if (!output->next)
		return -1;

	if (backend->drm_nvdc)
		fb_id = -1;
	else
		fb_id = output->next->fb_id;

	mode = container_of(output->base.current_mode, struct drm_mode, base);
	if (!output->current ||
	    output->current->stride != output->next->stride) {
		ret = drmModeSetCrtc(backend->drm.fd, output->crtc_id,
				     fb_id, 0, 0,
				     &output->connector_id, 1,
				     &mode->mode_info);
		if (!ret && backend->preferred_plane >= 0) {
			ret = drmModeSetPlane(backend->drm.fd, output->plane_id,
					      output->crtc_id, fb_id, 0,
					      0, 0,
					      mode->mode_info.hdisplay,
					      mode->mode_info.vdisplay,
					      0, 0,
					      (mode->mode_info.hdisplay << 16),
					      (mode->mode_info.vdisplay << 16));
			gl_renderer->output_stream_enable_auto_acquire(&output->base);
		}
		if (ret) {
			weston_log("set mode failed: %m\n");
			goto err_pageflip;
		}
		output_base->set_dpms(output_base, WESTON_DPMS_ON);
	}

	/* Auto-acquire is enabled with preferred plane so we skip page flip */
	if (backend->preferred_plane < 0) {
		if (backend->use_egldevice)
			ret = gl_renderer->output_stream_flip(&output->base, output);
		else
			ret = drmModePageFlip(backend->drm.fd, output->crtc_id,
					      output->next->fb_id,
					      DRM_MODE_PAGE_FLIP_EVENT, output);

		if (ret < 0) {
			weston_log("queueing pageflip failed: %m\n");
			goto err_pageflip;
		}
		output->page_flip_pending = 1;
	}

	drm_output_set_cursor(output);

	/*
	 * Now, update all the sprite surfaces
	 */
	wl_list_for_each(s, &backend->sprite_list, link) {
		uint32_t flags = 0;
		drmVBlank vbl = {
			.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT,
			.request.sequence = 1,
		};

		if (!drm_sprite_crtc_supported(output, s->possible_crtcs))
				continue;

		if (!s->used) {
			/*
			 * If previously used disable the overlay
			 * for this repaint to remove the last frame
			 * of any egl client that may still be
			 * displayed
			 */
			disable_sprite(s);

			continue;
		}

		if (backend->drm_nvdc)
			fb_id = -1;
		else if (s->next && !backend->sprites_hidden)
			fb_id = s->next->fb_id;

		/*
		 * Reset to unused. The next assign plane will set this to 1
		 * if it discovers that the overlay is being used and its
		 * position/size needs changing
		 */
		s->used = 0;

		/*
		 * Mark overlay as being used
		 */
		s->disabled = 0;

		if (!s->update_sprite)
			continue;

		ret = 1;

		if (s->atomic_page_flip) {
			/*
			 * Find surface for this sprite. If found and ready for atomic
			 * flipping, create an atomic request and perform atomic page
			 * flip
			 */
			struct weston_surface *es = get_surface_from_overlay_id(output_base,
										s->plane_id);
			if (es && gl_renderer->overlay_ready_for_atomic_flip(es)) {
				drmModeAtomicReqPtr req = drmModeAtomicAlloc();
				atomic_fill_plane_props(req, output, s);
				ret = gl_renderer->overlay_atomic_flip(es, req);
				drmModeAtomicFree(req);
			}
		} else
			ret = drmModeSetPlane(backend->drm.fd, s->plane_id,
					      output->crtc_id, fb_id, flags,
					      s->dest_x, s->dest_y,
					      s->dest_w, s->dest_h,
					      s->src_x, s->src_y,
					      s->src_w, s->src_h);
		if (ret < 0)
			weston_log("setplane failed: %d: %s\n",
				   ret, strerror(errno));

		/*
		 * Keep update_sprite flag and skip vblank request
		 * if modesetting failed. We will retry on next repaint
		 */
		if ((s->update_sprite = !!ret))
			continue;

		vbl.request.type |= drm_waitvblank_pipe(output);

		/*
		 * Queue a vblank signal so we know when the surface
		 * becomes active on the display or has been replaced.
		 */
		vbl.request.signal = (unsigned long)s;
		ret = drmWaitVBlank(backend->drm.fd, &vbl);
		if (ret) {
			weston_log("vblank event request failed: %d: %s\n",
				ret, strerror(errno));
		}

		s->output = output;
		output->vblank_pending = 1;
	}

	/* Schedule a repaint for this output and handle buffers
	 * if not compositing to CRTC
	 */
	if (backend->preferred_plane >= 0) {
		drm_output_release_fb(output, output->current);
		output->current = output->next;
		output->next = NULL;
		wl_event_source_timer_update(output->finish_timer_source,
					     REPAINT_TIME_MS);
	}

	return 0;

err_pageflip:
	output->cursor_view = NULL;
	if (output->next) {
		drm_output_release_fb(output, output->next);
		output->next = NULL;
	}

	return -1;
}

static void
drm_output_start_repaint_loop(struct weston_output *output_base)
{
	static int warning = 0;
	struct drm_output *output = (struct drm_output *) output_base;
	struct drm_backend *backend = (struct drm_backend *)
		output_base->compositor->backend;
	struct timespec ts, tnow;
	struct timespec vbl2now;
	int64_t refresh_nsec;
	int ret;
	drmVBlank vbl = {
		.request.type = DRM_VBLANK_RELATIVE,
		.request.sequence = 0,
		.request.signal = 0,
	};

	if (backend->preferred_plane >= 0) {
		goto finish_frame;
	}

	if (output->destroy_pending)
		return;

	if (!output->current) {
		/* We can't page flip if there's no mode set */
		goto finish_frame;
	}

	/* Try to get current msc and timestamp via instant query */
	vbl.request.type |= drm_waitvblank_pipe(output);
	ret = drmWaitVBlank(backend->drm.fd, &vbl);

	if (ret) {
		/* Immediate query failed. It may always fail so we'll never get
		 * a valid timestamp to update msc and call into finish frame.
		 * Hence, jump to finish frame here.
		 */
		goto finish_frame;
	}

	/* Zero timestamp means failure to get valid timestamp */
	if (vbl.reply.tval_sec > 0 || vbl.reply.tval_usec > 0) {
		ts.tv_sec = vbl.reply.tval_sec;
		ts.tv_nsec = vbl.reply.tval_usec * 1000;

		/* Valid timestamp for most recent vblank - not stale?
		 * Stale ts could happen on Linux 3.17+, so make sure it
		 * is not older than 1 refresh duration since now.
		 */
		weston_compositor_read_presentation_clock(backend->compositor,
							  &tnow);
		timespec_sub(&vbl2now, &tnow, &ts);
		refresh_nsec =
			millihz_to_nsec(output->base.current_mode->refresh);
		if (timespec_to_nsec(&vbl2now) < refresh_nsec) {
			drm_output_update_msc(output, vbl.reply.sequence);
			weston_output_finish_frame(output_base, &ts,
						WP_PRESENTATION_FEEDBACK_INVALID);
			return;
		}
	}

	/* Immediate query succeeded, but didn't provide valid timestamp.
	 * Use pageflip fallback.
	 */
	if (backend->use_egldevice)
		ret = gl_renderer->output_stream_flip(&output->base, output);
	else
		ret = drmModePageFlip(backend->drm.fd, output->crtc_id,
				      output->current->fb_id,
				      DRM_MODE_PAGE_FLIP_EVENT, output);

	if (ret < 0) {
		if (!warning) {
			weston_log("queueing pageflip failed: %m\n");
			warning = 1;
		}
		goto finish_frame;
	}

	return;

finish_frame:
	/* if we cannot page-flip, immediately finish frame */
	weston_compositor_read_presentation_clock(output_base->compositor, &ts);
	weston_output_finish_frame(output_base, &ts,
				   WP_PRESENTATION_FEEDBACK_INVALID);
}

static void
drm_output_update_msc(struct drm_output *output, unsigned int seq)
{
	uint64_t msc_hi = output->base.msc >> 32;

	if (seq < (output->base.msc & 0xffffffff))
		msc_hi++;

	output->base.msc = (msc_hi << 32) + seq;
}

static int
on_finish_frame(void *arg)
{
	struct drm_output *output = (struct drm_output*) arg;
	struct timespec ts;

	weston_compositor_read_presentation_clock(output->base.compositor, &ts);
	weston_output_finish_frame(&output->base, &ts, 0);
	return 0;
}

static void
vblank_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec,
	       void *data)
{
	struct drm_sprite *s = (struct drm_sprite *)data;
	struct drm_output *output = s->output;
	struct timespec ts;
	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION |
			 WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

	drm_output_update_msc(output, frame);
	output->vblank_pending = 0;

	drm_output_release_fb(output, s->current);
	s->current = s->next;
	s->next = NULL;

	if (!output->page_flip_pending) {
		ts.tv_sec = sec;
		ts.tv_nsec = usec * 1000;
		weston_output_finish_frame(&output->base, &ts, flags);
	}
}

static void
drm_output_destroy(struct weston_output *output_base);

static void
page_flip_handler(int fd, unsigned int frame,
		  unsigned int sec, unsigned int usec, void *data)
{
	struct drm_output *output = (struct drm_output *) data;
	struct timespec ts;
	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
			 WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION |
			 WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

	drm_output_update_msc(output, frame);

	/* We don't set page_flip_pending on start_repaint_loop, in that case
	 * we just want to page flip to the current buffer to get an accurate
	 * timestamp */
	if (output->page_flip_pending) {
		drm_output_release_fb(output, output->current);
		output->current = output->next;
		output->next = NULL;
	}

	output->page_flip_pending = 0;

	if (output->destroy_pending)
		drm_output_destroy(&output->base);
	else if (!output->vblank_pending) {
		ts.tv_sec = sec;
		ts.tv_nsec = usec * 1000;

		/* Zero timestamp means failure to get valid timestamp, so
		 * immediately finish frame
		 *
		 * FIXME: Driver should never return an invalid page flip
		 *        timestamp */
		if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
			weston_compositor_read_presentation_clock(
							output->base.compositor,
							&ts);
			flags = WP_PRESENTATION_FEEDBACK_INVALID;
		}

		weston_output_finish_frame(&output->base, &ts, flags);

		/* We can't call this from frame_notify, because the output's
		 * repaint needed flag is cleared just after that */
		if (output->recorder)
			weston_output_schedule_repaint(&output->base);
	}
}

#ifdef USE_GBM
static uint32_t
drm_output_check_sprite_format(struct drm_sprite *s,
			       struct weston_view *ev, struct gbm_bo *bo)
{
	uint32_t i, format;

	format = gbm_bo_get_format(bo);

	if (format == GBM_FORMAT_ARGB8888) {
		pixman_region32_t r;

		pixman_region32_init_rect(&r, 0, 0,
					  ev->surface->width,
					  ev->surface->height);
		pixman_region32_subtract(&r, &r, &ev->surface->opaque);

		if (!pixman_region32_not_empty(&r))
			format = GBM_FORMAT_XRGB8888;

		pixman_region32_fini(&r);
	}

	for (i = 0; i < s->count_formats; i++)
		if (s->formats[i] == format)
			return format;

	return 0;
}
#endif

static int
drm_view_transform_supported(struct weston_view *ev)
{
	return !ev->transform.enabled ||
		(ev->transform.matrix.type < WESTON_MATRIX_TRANSFORM_ROTATE);
}

static struct weston_plane *
drm_output_prepare_overlay_view(struct drm_output *output,
				struct weston_view *ev)
{
	struct weston_compositor *ec = output->base.compositor;
	struct drm_backend *b = (struct drm_backend *)ec->backend;
	struct weston_buffer_viewport *viewport = &ev->surface->buffer_viewport;
	struct wl_resource *buffer_resource;
	struct drm_sprite *s;
	struct linux_dmabuf_buffer *dmabuf;
	struct weston_view *iter;
	int found = 0;
	struct gbm_bo *bo;
	pixman_region32_t dest_rect, src_rect;
	pixman_box32_t *box, tbox;
	uint32_t format;
	uint32_t count = 1;
	wl_fixed_t sx1, sy1, sx2, sy2;

#ifdef USE_GBM
	if (b->gbm == NULL)
		return NULL;
#endif

	if (viewport->buffer.transform != output->base.transform)
		return NULL;

	if (viewport->buffer.scale != output->base.current_scale)
		return NULL;

	if (b->sprites_are_broken)
		return NULL;

	if (ev->output_mask != (1u << output->base.id))
		return NULL;

	if (ev->surface->buffer_ref.buffer == NULL)
		return NULL;
	buffer_resource = ev->surface->buffer_ref.buffer->resource;

	if (ev->alpha != 1.0f)
		return NULL;

	if (wl_shm_buffer_get(buffer_resource))
		return NULL;

	if (!drm_view_transform_supported(ev))
		return NULL;

	wl_list_for_each(s, &b->sprite_list, link) {
		uint32_t overlay_assigned;
		uint32_t overlay_id;

		if (!drm_sprite_crtc_supported(output, s->possible_crtcs))
			continue;

		/*
		 * At this point the weston egl surfaces should have already
		 * been assigned an overlay. We just need to find a match
		 */
		overlay_id = gl_renderer->get_assigned_overlay(ev->surface, &overlay_assigned);

		if (overlay_assigned && (s->plane_id == overlay_id)) {
			s->used = 1;
			found = 1;
			break;
		}
	}

	/* No sprites available */
	if (!found)
		return NULL;

#ifdef USE_GBM
	if ((dmabuf = linux_dmabuf_buffer_get(buffer_resource))) {
#ifdef HAVE_GBM_FD_IMPORT
		/* XXX: TODO:
		 *
		 * Use AddFB2 directly, do not go via GBM.
		 * Add support for multiplanar formats.
		 * Both require refactoring in the DRM-backend to
		 * support a mix of gbm_bos and drmfbs.
		 */
		struct gbm_import_fd_data gbm_dmabuf = {
			.fd     = dmabuf->attributes.fd[0],
			.width  = dmabuf->attributes.width,
			.height = dmabuf->attributes.height,
			.stride = dmabuf->attributes.stride[0],
			.format = dmabuf->attributes.format
		};

		if (dmabuf->attributes.n_planes != 1 || dmabuf->attributes.offset[0] != 0)
			return NULL;

		bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_FD, &gbm_dmabuf,
				   GBM_BO_USE_SCANOUT);
#else
		return NULL;
#endif /* HAVE_GBM_FD_IMPORT */
	} else {
		bo = gbm_bo_import(b->gbm, GBM_BO_IMPORT_WL_BUFFER,
				   buffer_resource, GBM_BO_USE_SCANOUT);
	}
	if (!bo)
		return NULL;

	format = drm_output_check_sprite_format(s, ev, bo);
	if (format == 0) {
		gbm_bo_destroy(bo);
		return NULL;
	}

	s->next = drm_fb_get_from_bo(bo, b, format);
	if (!s->next) {
		gbm_bo_destroy(bo);
		return NULL;
	}

	drm_fb_set_buffer(s->next, ev->surface->buffer_ref.buffer);
#endif /* USE_GBM */

	box = pixman_region32_extents(&ev->transform.boundingbox);
	s->plane.x = box->x1;
	s->plane.y = box->y1;

	/*
	 * Calculate the source & dest rects properly based on actual
	 * position (note the caller has called weston_view_update_transform()
	 * for us already).
	 */
	pixman_region32_init(&dest_rect);
	pixman_region32_intersect(&dest_rect, &ev->transform.boundingbox,
				  &output->base.region);
	pixman_region32_translate(&dest_rect, -output->base.x, -output->base.y);
	box = pixman_region32_extents(&dest_rect);
	tbox = weston_transformed_rect(output->base.width,
				       output->base.height,
				       output->base.transform,
				       output->base.current_scale,
				       *box);

	if (s->dest_x != tbox.x1 ||
	    s->dest_y != tbox.y1 ||
	    s->dest_w != tbox.x2 - tbox.x1 ||
	    s->dest_h != tbox.y2 - tbox.y1) {
	    s->update_sprite = 1;
	}
	s->dest_x = tbox.x1;
	s->dest_y = tbox.y1;
	s->dest_w = tbox.x2 - tbox.x1;
	s->dest_h = tbox.y2 - tbox.y1;
	pixman_region32_fini(&dest_rect);

	pixman_region32_init(&src_rect);
	pixman_region32_intersect(&src_rect, &ev->transform.boundingbox,
				  &output->base.region);
	box = pixman_region32_extents(&src_rect);

	weston_view_from_global_fixed(ev,
				      wl_fixed_from_int(box->x1),
				      wl_fixed_from_int(box->y1),
				      &sx1, &sy1);
	weston_view_from_global_fixed(ev,
				      wl_fixed_from_int(box->x2),
				      wl_fixed_from_int(box->y2),
				      &sx2, &sy2);

	if (sx1 < 0)
		sx1 = 0;
	if (sy1 < 0)
		sy1 = 0;
	if (sx2 > wl_fixed_from_int(ev->surface->width))
		sx2 = wl_fixed_from_int(ev->surface->width);
	if (sy2 > wl_fixed_from_int(ev->surface->height))
		sy2 = wl_fixed_from_int(ev->surface->height);

	tbox.x1 = sx1;
	tbox.y1 = sy1;
	tbox.x2 = sx2;
	tbox.y2 = sy2;

	tbox = weston_transformed_rect(wl_fixed_from_int(ev->surface->width),
				       wl_fixed_from_int(ev->surface->height),
				       viewport->buffer.transform,
				       viewport->buffer.scale,
				       tbox);

	if (s->src_x != tbox.x1 << 8 ||
	    s->src_y != tbox.y1 << 8 ||
	    s->src_w != (tbox.x2 - tbox.x1) << 8 ||
	    s->src_h != (tbox.y2 - tbox.y1) << 8) {
	    s->update_sprite = 1;
	}
	s->src_x = tbox.x1 << 8;
	s->src_y = tbox.y1 << 8;
	s->src_w = (tbox.x2 - tbox.x1) << 8;
	s->src_h = (tbox.y2 - tbox.y1) << 8;
	pixman_region32_fini(&src_rect);

	/*
	 * Check to see if the stacking order has changed
	 *
	 * Update current sprite's zpos to match its weston_view stacking order
	 * as well as the current sprite's alpha value
	 */
	wl_list_for_each_reverse(iter,
				 &output->base.compositor->view_list,
				 link) {
		if (iter == ev) {
			if (s->props.valid & (1 << WPLANE_PROP_ZPOS)) {
				if (s->zpos != count * ZPOS_PADDING) {
					s->zpos = count * ZPOS_PADDING;
					s->update_sprite = 1;
				}
			}

			/*
			 * ev->alpha appears to range from [0.0, 1.0]
			 * This has been deduced by how ev->alpha is
			 * used in desktop-shell.
			 */
			if (s->props.valid & (1 << WPLANE_PROP_ALPHA)) {
				if (s->alpha != MAX_ALPHA * ev->alpha) {
					s->alpha = (ev->alpha > 1) ?
						    MAX_ALPHA :
						    MAX_ALPHA * ev->alpha;
					s->update_sprite = 1;
				}
			}

			break;
		}
		count++;
	}

	if (s->update_sprite && b->atomic_modeset)
		s->atomic_page_flip =
			(gl_renderer->prepare_overlay_atomic_flip(ev->surface) == 0);

	return &s->plane;
}

static struct weston_plane *
drm_output_prepare_cursor_view(struct drm_output *output,
			       struct weston_view *ev)
{
	struct drm_backend *b =
		(struct drm_backend *)output->base.compositor->backend;
	struct weston_buffer_viewport *viewport = &ev->surface->buffer_viewport;
	struct wl_shm_buffer *shmbuf;

	if (ev->transform.enabled &&
	    (ev->transform.matrix.type > WESTON_MATRIX_TRANSFORM_TRANSLATE))
		return NULL;
#ifdef USE_GBM
	if (b->gbm == NULL)
		return NULL;
#endif
	if (output->base.transform != WL_OUTPUT_TRANSFORM_NORMAL)
		return NULL;
	if (viewport->buffer.scale != output->base.current_scale)
		return NULL;
	if (output->cursor_view)
		return NULL;
	if (ev->output_mask != (1u << output->base.id))
		return NULL;
	if (b->cursors_are_broken)
		return NULL;
	if (ev->geometry.scissor_enabled)
		return NULL;
	if (ev->surface->buffer_ref.buffer == NULL)
		return NULL;
	shmbuf = wl_shm_buffer_get(ev->surface->buffer_ref.buffer->resource);
	if (!shmbuf)
		return NULL;
	if (wl_shm_buffer_get_format(shmbuf) != WL_SHM_FORMAT_ARGB8888)
		return NULL;
	if (ev->surface->width > b->cursor_width ||
	    ev->surface->height > b->cursor_height)
		return NULL;

	output->cursor_view = ev;

	return &output->cursor_plane;
}

/**
 * Update the image for the current cursor surface
 *
 * @param b DRM backend structure
 * @param bo GBM buffer object to write into
 * @param ev View to use for cursor image
 */
static void
cursor_bo_update(struct drm_backend *b, struct gbm_bo *bo,
		 struct weston_view *ev)
{
	struct weston_buffer *buffer = ev->surface->buffer_ref.buffer;
	uint32_t buf[b->cursor_width * b->cursor_height];
	int32_t stride;
	uint8_t *s;
	int i;

	assert(buffer && buffer->shm_buffer);
	assert(buffer->shm_buffer == wl_shm_buffer_get(buffer->resource));
	assert(ev->surface->width <= b->cursor_width);
	assert(ev->surface->height <= b->cursor_height);

	memset(buf, 0, sizeof buf);
	stride = wl_shm_buffer_get_stride(buffer->shm_buffer);
	s = wl_shm_buffer_get_data(buffer->shm_buffer);

	wl_shm_buffer_begin_access(buffer->shm_buffer);
	for (i = 0; i < ev->surface->height; i++)
		memcpy(buf + i * b->cursor_width,
		       s + i * stride,
		       ev->surface->width * 4);
	wl_shm_buffer_end_access(buffer->shm_buffer);

#ifdef USE_GBM
	if (gbm_bo_write(bo, buf, sizeof buf) < 0)
		weston_log("failed update cursor: %m\n");
#endif
}

static void
drm_output_set_cursor(struct drm_output *output)
{
	struct weston_view *ev = output->cursor_view;
	struct weston_buffer *buffer;
	struct drm_backend *b =
		(struct drm_backend *) output->base.compositor->backend;
	EGLint handle;
	struct gbm_bo *bo;
	float x, y;
	uint32_t stride = 0;
	uint8_t *buf = NULL;
	unsigned char *s = NULL;
	int i = 0;

	output->cursor_view = NULL;
	if (ev == NULL) {
		drmModeSetCursor(b->drm.fd, output->crtc_id, 0, 0, 0);
		output->cursor_plane.x = INT32_MIN;
		output->cursor_plane.y = INT32_MIN;
		return;
	}

	buffer = ev->surface->buffer_ref.buffer;

	if (buffer &&
	    pixman_region32_not_empty(&output->cursor_plane.damage)) {
		pixman_region32_fini(&output->cursor_plane.damage);
		pixman_region32_init(&output->cursor_plane.damage);
		output->current_cursor ^= 1;
#ifdef USE_GBM
		bo = output->gbm_cursor_bo[output->current_cursor];

		cursor_bo_update(b, bo, ev);
		handle = gbm_bo_get_handle(bo).s32;
#else
		handle = output->cursors[output->current_cursor].bo_handle;
		buf = output->cursors[output->current_cursor].buf;
		memset(buf, 0, b->cursor_width * b->cursor_height * 4);
		/* Download the new cursor image from SHM
		 * Download line by line to account for pitch differences
		 */
		stride = wl_shm_buffer_get_stride(buffer->shm_buffer);
		s = wl_shm_buffer_get_data(buffer->shm_buffer);
		wl_shm_buffer_begin_access(buffer->shm_buffer);
		for (i = 0; i < ev->surface->height; i++)
			memcpy(buf + i * b->cursor_width * 4, s + i * stride,
			       ev->surface->width * 4);
		wl_shm_buffer_end_access(buffer->shm_buffer);
#endif
		if (drmModeSetCursor(b->drm.fd, output->crtc_id, handle,
				b->cursor_width, b->cursor_height)) {
			weston_log("failed to set cursor: %m\n");
			b->cursors_are_broken = 1;
		}
	}

	weston_view_to_global_float(ev, 0, 0, &x, &y);

	/* From global to output space, output transform is guaranteed to be
	 * NORMAL by drm_output_prepare_cursor_view().
	 */
	x = (x - output->base.x) * output->base.current_scale;
	y = (y - output->base.y) * output->base.current_scale;

	if (output->cursor_plane.x != x || output->cursor_plane.y != y) {
		if (drmModeMoveCursor(b->drm.fd, output->crtc_id, x, y)) {
			weston_log("failed to move cursor: %m\n");
			b->cursors_are_broken = 1;
		}

		output->cursor_plane.x = x;
		output->cursor_plane.y = y;
	}
}

static void
drm_assign_planes(struct weston_output *output_base)
{
	struct drm_backend *b =
		(struct drm_backend *)output_base->compositor->backend;
	struct drm_output *output = (struct drm_output *)output_base;
	struct weston_view *ev, *next;
	pixman_region32_t overlap, surface_overlap;
	struct weston_plane *primary, *next_plane;

	/*
	 * Find a surface for each sprite in the output using some heuristics:
	 * 1) size
	 * 2) frequency of update
	 * 3) opacity (though some hw might support alpha blending)
	 * 4) clipping (this can be fixed with color keys)
	 *
	 * The idea is to save on blitting since this should save power.
	 * If we can get a large video surface on the sprite for example,
	 * the main display surface may not need to update at all, and
	 * the client buffer can be used directly for the sprite surface
	 * as we do for flipping full screen surfaces.
	 */
	pixman_region32_init(&overlap);
	primary = &output_base->compositor->primary_plane;

	wl_list_for_each_safe(ev, next, &output_base->compositor->view_list, link) {
		struct weston_surface *es = ev->surface;

		/* Test whether this buffer can ever go into a plane:
		 * non-shm, or small enough to be a cursor.
		 *
		 * Also, keep a reference when using the pixman renderer.
		 * That makes it possible to do a seamless switch to the GL
		 * renderer and since the pixman renderer keeps a reference
		 * to the buffer anyway, there is no side effects.
		 */
		if (b->use_pixman ||
		    (es->buffer_ref.buffer &&
		    (!wl_shm_buffer_get(es->buffer_ref.buffer->resource) ||
		     (ev->surface->width <= b->cursor_width &&
		      ev->surface->height <= b->cursor_height))))
			es->keep_buffer = true;
		else
			es->keep_buffer = false;

		pixman_region32_init(&surface_overlap);

		/* Commented fallback which tries to use sw composition when
		 * surfaces overlap. Because of the limitations of EGLStreams
		 * the fallback path does not work and causes unwanted
		 * side effects
		 */
		//pixman_region32_intersect(&surface_overlap, &overlap,
		//			    &ev->transform.boundingbox);

		next_plane = NULL;

		if (pixman_region32_not_empty(&surface_overlap))
			next_plane = primary;
		if (next_plane == NULL)
			next_plane = drm_output_prepare_cursor_view(output, ev);
		if (next_plane == NULL)
			next_plane = drm_output_prepare_scanout_view(output, ev);
		if (next_plane == NULL)
			next_plane = drm_output_prepare_overlay_view(output, ev);
		if (next_plane == NULL)
			next_plane = primary;

		weston_view_move_to_plane(ev, next_plane);

		if (next_plane == primary)
			pixman_region32_union(&overlap, &overlap,
					      &ev->transform.boundingbox);

		if (next_plane == primary ||
		    next_plane == &output->cursor_plane) {
			/* cursor plane involves a copy */
			ev->psf_flags = 0;
		} else {
			/* All other planes are a direct scanout of a
			 * single client buffer.
			 */
			ev->psf_flags = WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY;
		}

		pixman_region32_fini(&surface_overlap);
	}
	pixman_region32_fini(&overlap);
}

static void
drm_output_fini_pixman(struct drm_output *output);

static void
drm_output_fini_egl(struct drm_output *output);

static void
drm_output_destroy(struct weston_output *output_base)
{
	struct drm_output *output = (struct drm_output *) output_base;
	struct drm_backend *b =
		(struct drm_backend *)output->base.compositor->backend;
	drmModeCrtcPtr origcrtc = output->original_crtc;

	if (output->page_flip_pending) {
		output->destroy_pending = 1;
		weston_log("destroy output while page flip pending\n");
		return;
	}

	if (output->backlight)
		backlight_destroy(output->backlight);

	drmModeFreeProperty(output->dpms_prop);

	/* Turn off hardware cursor */
	drmModeSetCursor(b->drm.fd, output->crtc_id, 0, 0, 0);

	/* Restore original CRTC state */
	drmModeSetCrtc(b->drm.fd, origcrtc->crtc_id, origcrtc->buffer_id,
		       origcrtc->x, origcrtc->y,
		       &output->connector_id, 1, &origcrtc->mode);
	if (b->preferred_plane >= 0)
		drmModeSetPlane(b->drm.fd,
			        output->plane_id,
			        output->crtc_id, 0, 0,
			        0, 0, 0, 0, 0, 0, 0, 0);
	drmModeFreeCrtc(origcrtc);

	b->crtc_allocator &= ~(1 << output->pipe);
	b->connector_allocator &= ~(1 << output->connector_id);

	if (b->use_pixman)
		drm_output_fini_pixman(output);
	else
		drm_output_fini_egl(output);

	weston_plane_release(&output->fb_plane);
	weston_plane_release(&output->cursor_plane);

	weston_output_destroy(&output->base);

	free(output);
}

/**
 * Find the closest-matching mode for a given target
 *
 * Given a target mode, find the most suitable mode amongst the output's
 * current mode list to use, preferring the current mode if possible, to
 * avoid an expensive mode switch.
 *
 * @param output DRM output
 * @param target_mode Mode to attempt to match
 * @returns Pointer to a mode from the output's mode list
 */
static struct drm_mode *
choose_mode (struct drm_output *output, struct weston_mode *target_mode)
{
	struct drm_mode *tmp_mode = NULL, *mode;

	if (output->base.current_mode->width == target_mode->width &&
	    output->base.current_mode->height == target_mode->height &&
	    (output->base.current_mode->refresh == target_mode->refresh ||
	     target_mode->refresh == 0))
		return (struct drm_mode *)output->base.current_mode;

	wl_list_for_each(mode, &output->base.mode_list, base.link) {
		if (mode->mode_info.hdisplay == target_mode->width &&
		    mode->mode_info.vdisplay == target_mode->height) {
			if (mode->base.refresh == target_mode->refresh ||
			    target_mode->refresh == 0) {
				return mode;
			} else if (!tmp_mode)
				tmp_mode = mode;
		}
	}

	return tmp_mode;
}

static int
drm_output_init_egl(struct drm_output *output, struct drm_backend *b);
static int
drm_output_init_pixman(struct drm_output *output, struct drm_backend *b);

static int
drm_output_switch_mode(struct weston_output *output_base, struct weston_mode *mode)
{
	struct drm_output *output;
	struct drm_mode *drm_mode;
	struct drm_backend *b;

	if (output_base == NULL) {
		weston_log("output is NULL.\n");
		return -1;
	}

	if (mode == NULL) {
		weston_log("mode is NULL.\n");
		return -1;
	}

	b = (struct drm_backend *)output_base->compositor->backend;
	output = (struct drm_output *)output_base;
	drm_mode  = choose_mode (output, mode);

	if (!drm_mode) {
		weston_log("%s, invalid resolution:%dx%d\n", __func__, mode->width, mode->height);
		return -1;
	}

	if (&drm_mode->base == output->base.current_mode)
		return 0;

	output->base.current_mode->flags = 0;

	output->base.current_mode = &drm_mode->base;
	output->base.current_mode->flags =
		WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;

	/* reset rendering stuff. */
	drm_output_release_fb(output, output->current);
	drm_output_release_fb(output, output->next);
	output->current = output->next = NULL;

	if (b->use_pixman) {
		drm_output_fini_pixman(output);
		if (drm_output_init_pixman(output, b) < 0) {
			weston_log("failed to init output pixman state with "
				   "new mode\n");
			return -1;
		}
	} else {
		drm_output_fini_egl(output);
		if (drm_output_init_egl(output, b) < 0) {
			weston_log("failed to init output egl state with "
				   "new mode");
			return -1;
		}
	}

	return 0;
}

static int
on_drm_input(int fd, uint32_t mask, void *data)
{
	drmEventContext evctx;

	memset(&evctx, 0, sizeof evctx);
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.page_flip_handler = page_flip_handler;
	evctx.vblank_handler = vblank_handler;
	drmHandleEvent(fd, &evctx);

	return 1;
}

/*
 * Use the EGL_EXT_device_drm extension to query the DRM device file
 * corresponding to the EGLDeviceEXT.
 */
static int
drm_open_egldevice(struct drm_backend *b)
{
	const char *drm_path = NULL;
	int fd = -1;

	if (b) {
		gl_renderer->get_drm_device_file(b->egldevice, &drm_path);
	} else {
		return -1;
	}

	if (drm_path == NULL) {
		weston_log("No DRM device file found for EGL device.\n");
		return -1;
	}

	if (!strcmp(drm_path, "drm-nvdc")) {
		b->drm_nvdc = 1;
		fd = drmOpen(drm_path, NULL);
	} else
		fd = open(drm_path, O_RDWR, 0);

	if (fd < 0) {
		weston_log("Unable to open DRM device file: %s\n", drm_path);
		return -1;
	}

	b->drm.fd = fd;
	b->drm.filename = strdup(drm_path);

	return fd;
}

static int
drm_open_udev(struct drm_backend *b, struct udev_device *device)
{
	const char *filename, *sysnum;
	int fd;

	sysnum = udev_device_get_sysnum(device);
	if (sysnum)
		b->drm.id = atoi(sysnum);
	if (!sysnum || b->drm.id < 0) {
		weston_log("cannot get device sysnum\n");
		return -1;
	}

	filename = udev_device_get_devnode(device);
	fd = weston_launcher_open(b->compositor->launcher, filename, O_RDWR);
	if (fd < 0) {
		/* Probably permissions error */
		weston_log("couldn't open %s, skipping\n",
			udev_device_get_devnode(device));
		return -1;
	}

	weston_log("using %s\n", filename);

	b->drm.fd = fd;
	b->drm.filename = strdup(filename);

	return fd;
}

static void
drm_cursors_init(struct drm_backend *b)
{
	struct drm_output *output;
	int i;

	if (b->cursors_are_broken)
		return;

	wl_list_for_each(output, &b->compositor->output_list, base.link) {

		/* Create buffer objects for the HW cursor */
		for (i = 0; i < 2; i++) {
			if (output->cursors[i].bo_handle)
				continue;

			if (!drm_dumb_buffer_create(b,
						    b->cursor_width,
						    b->cursor_height,
						    32,
						    &output->cursors[i])) {
				/* Fall back to no cursors */
				output->cursors[i].bo_handle = 0;
				weston_log("cursor buffers unavailable, using gl cursors\n");
				b->cursors_are_broken = 1;
				break;
			}
		}
	}
}

static void
drm_cursors_destroy(struct drm_backend *b)
{
	struct drm_output *output = NULL;
	int i = 0;

	wl_list_for_each(output, &b->compositor->output_list, base.link)
		for (i = 0; i < 2; i++) {
			if (!output->cursors[i].bo_handle)
				continue;

			/* Disable cursor and destroy its corresponding
			 * buffer object
			 */
			drmModeSetCursor(b->drm.fd, output->crtc_id, 0, 0, 0);
			drm_dumb_buffer_destroy(b->drm.fd,
						&output->cursors[i]);
		}
}

static int
init_drm(struct drm_backend *b)
{
	uint64_t cap;
	int fd, ret;
	clockid_t clk_id;

	fd = b->drm.fd;

	ret = drmGetCap(fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	if (ret == 0 && cap == 1)
		clk_id = CLOCK_MONOTONIC;
	else
		clk_id = CLOCK_REALTIME;

	if (weston_compositor_set_presentation_clock(b->compositor, clk_id) < 0) {
		weston_log("Error: failed to set presentation clock %d.\n",
			   clk_id);
		return -1;
	}

	ret = drmGetCap(fd, DRM_CAP_CURSOR_WIDTH, &cap);
	if (ret == 0)
		b->cursor_width = cap;
	else
		b->cursor_width = 64;

	ret = drmGetCap(fd, DRM_CAP_CURSOR_HEIGHT, &cap);
	if (ret == 0)
		b->cursor_height = cap;
	else
		b->cursor_height = 64;

	/* Check for atomics support and enable them */
	ret = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	b->atomic_modeset = (ret == 0);
	weston_log("DRM: %s atomic modesetting\n",
		   b->atomic_modeset ? "supports" : "does not support");
	return 0;
}

#ifdef USE_GBM
static struct gbm_device *
create_gbm_device(int fd)
{
	struct gbm_device *gbm;

	/* GBM will load a dri driver, but even though they need symbols from
	 * libglapi, in some version of Mesa they are not linked to it. Since
	 * only the gl-renderer module links to it, the call above won't make
	 * these symbols globally available, and loading the DRI driver fails.
	 * Workaround this by dlopen()'ing libglapi with RTLD_GLOBAL. */
	dlopen("libglapi.so.0", RTLD_LAZY | RTLD_GLOBAL);

	gbm = gbm_create_device(fd);

	return gbm;
}
#endif

static EGLDeviceEXT
find_primary_egldevice(const char *filename)
{
	EGLDeviceEXT egldevice = EGL_NO_DEVICE_EXT;
	EGLDeviceEXT *devices;
	EGLint num_devices;
	const char *drm_path;
	int i;

	if (gl_renderer->get_devices(0, NULL, &num_devices) < 0 ||
	    num_devices < 1)
		return EGL_NO_DEVICE_EXT;

	devices = zalloc(num_devices * sizeof *devices);
	if (!devices)
		return EGL_NO_DEVICE_EXT;

	if (gl_renderer->get_devices(num_devices, devices, &num_devices) < 0) {
		free(devices);
		return EGL_NO_DEVICE_EXT;
	}

	for (i = 0; i < num_devices; i++)
		if (gl_renderer->get_drm_device_file(devices[i],
						     &drm_path) == 0 &&
		    (!filename || strcmp(filename, drm_path) == 0)) {
			egldevice = devices[i];
			break;
		}

	free(devices);
	return egldevice;
}

/* When initializing EGL, if the preferred buffer format isn't available
 * we may be able to substitute an ARGB format for an XRGB one.
 *
 * This returns 0 if substitution isn't possible, but 0 might be a
 * legitimate format for other EGL platforms, so the caller is
 * responsible for checking for 0 before calling gl_renderer->create().
 *
 * This works around https://bugs.freedesktop.org/show_bug.cgi?id=89689
 * but it's entirely possible we'll see this again on other implementations.
 */
#ifdef USE_GBM
static int
fallback_format_for(uint32_t format)
{
	switch (format) {
	case GBM_FORMAT_XRGB8888:
		return GBM_FORMAT_ARGB8888;
	case GBM_FORMAT_XRGB2101010:
		return GBM_FORMAT_ARGB2101010;
	default:
		return 0;
	}
}
#endif

static int
drm_backend_create_gl_renderer(struct drm_backend *b)
{
	if (b->use_egldevice) {
		EGLint device_platform_attribs[] = {
			EGL_DRM_MASTER_FD_EXT, b->drm.fd,
			EGL_NONE
		};

		return gl_renderer->display_create(
					b->compositor,
					EGL_PLATFORM_DEVICE_EXT,
					(void *)b->egldevice,
					device_platform_attribs,
					gl_renderer->opaque_stream_attribs,
					NULL,
					0);
	}
    else {
#ifdef USE_GBM
		EGLint format[3] = {
			b->gbm_format,
			fallback_format_for(b->gbm_format),
			0,
		};
		int n_formats = 2;

		if (format[1])
			n_formats = 3;

		return gl_renderer->display_create(b->compositor,
						   EGL_PLATFORM_GBM_KHR,
						   (void *)b->gbm,
						   NULL,
						   gl_renderer->opaque_attribs,
						   format,
						   n_formats);
#else
		weston_log("failed to create gl_renderer: "
			   "use_egldevice not set\n");
		return -1;
#endif
	}
}

static int
init_egl(struct drm_backend *b)
{
	if (!b->use_egldevice) {
#ifdef USE_GBM
		b->gbm = create_gbm_device(b->drm.fd);
		if (!b->gbm)
#endif
			return -1;
	}

	if (drm_backend_create_gl_renderer(b) < 0) {
#ifdef USE_GBM
		if (b->gbm)
			gbm_device_destroy(b->gbm);
#endif
		return -1;
	}

	return 0;
}

static int
init_pixman(struct drm_backend *b)
{
	return pixman_renderer_init(b->compositor);
}

/**
 * Add a mode to output's mode list
 *
 * Copy the supplied DRM mode into a Weston mode structure, and add it to the
 * output's mode list.
 *
 * @param output DRM output to add mode to
 * @param info DRM mode structure to add
 * @returns Newly-allocated Weston/DRM mode structure
 */
static struct drm_mode *
drm_output_add_mode(struct drm_output *output, const drmModeModeInfo *info)
{
	struct drm_mode *mode;
	uint64_t refresh;

	mode = malloc(sizeof *mode);
	if (mode == NULL)
		return NULL;

	mode->base.flags = 0;
	mode->base.width = info->hdisplay;
	mode->base.height = info->vdisplay;

	/* Calculate higher precision (mHz) refresh rate */
	refresh = (info->clock * 1000000LL / info->htotal +
		   info->vtotal / 2) / info->vtotal;

	if (info->flags & DRM_MODE_FLAG_INTERLACE)
		refresh *= 2;
	if (info->flags & DRM_MODE_FLAG_DBLSCAN)
		refresh /= 2;
	if (info->vscan > 1)
	    refresh /= info->vscan;

	mode->base.refresh = refresh;
	mode->mode_info = *info;

	if (info->type & DRM_MODE_TYPE_PREFERRED)
		mode->base.flags |= WL_OUTPUT_MODE_PREFERRED;

	wl_list_insert(output->base.mode_list.prev, &mode->base.link);

	return mode;
}

static int
drm_subpixel_to_wayland(int drm_value)
{
	switch (drm_value) {
	default:
	case DRM_MODE_SUBPIXEL_UNKNOWN:
		return WL_OUTPUT_SUBPIXEL_UNKNOWN;
	case DRM_MODE_SUBPIXEL_NONE:
		return WL_OUTPUT_SUBPIXEL_NONE;
	case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
		return WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB;
	case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
		return WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR;
	case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
		return WL_OUTPUT_SUBPIXEL_VERTICAL_RGB;
	case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
		return WL_OUTPUT_SUBPIXEL_VERTICAL_BGR;
	}
}

/* returns a value between 0-255 range, where higher is brighter */
static uint32_t
drm_get_backlight(struct drm_output *output)
{
	long brightness, max_brightness, norm;

	brightness = backlight_get_brightness(output->backlight);
	max_brightness = backlight_get_max_brightness(output->backlight);

	/* convert it on a scale of 0 to 255 */
	norm = (brightness * 255)/(max_brightness);

	return (uint32_t) norm;
}

/* values accepted are between 0-255 range */
static void
drm_set_backlight(struct weston_output *output_base, uint32_t value)
{
	struct drm_output *output = (struct drm_output *) output_base;
	long max_brightness, new_brightness;

	if (!output->backlight)
		return;

	if (value > 255)
		return;

	max_brightness = backlight_get_max_brightness(output->backlight);

	/* get denormalized value */
	new_brightness = (value * max_brightness) / 255;

	backlight_set_brightness(output->backlight, new_brightness);
}

static drmModePropertyPtr
drm_get_prop(int fd, drmModeConnectorPtr connector, const char *name)
{
	drmModePropertyPtr props;
	int i;

	for (i = 0; i < connector->count_props; i++) {
		props = drmModeGetProperty(fd, connector->props[i]);
		if (!props)
			continue;

		if (!strcmp(props->name, name))
			return props;

		drmModeFreeProperty(props);
	}

	return NULL;
}

static void
drm_set_dpms(struct weston_output *output_base, enum dpms_enum level)
{
	struct drm_output *output = (struct drm_output *) output_base;
	struct weston_compositor *ec = output_base->compositor;
	struct drm_backend *b = (struct drm_backend *)ec->backend;
	int ret;

	if (!output->dpms_prop)
		return;

	ret = drmModeConnectorSetProperty(b->drm.fd, output->connector_id,
				 	  output->dpms_prop->prop_id, level);
	if (ret) {
		weston_log("DRM: DPMS: failed property set for %s\n",
			   output->base.name);
		return;
	}

	output->dpms = level;
}

static const char * const connector_type_names[] = {
	[DRM_MODE_CONNECTOR_Unknown]     = "Unknown",
	[DRM_MODE_CONNECTOR_VGA]         = "VGA",
	[DRM_MODE_CONNECTOR_DVII]        = "DVI-I",
	[DRM_MODE_CONNECTOR_DVID]        = "DVI-D",
	[DRM_MODE_CONNECTOR_DVIA]        = "DVI-A",
	[DRM_MODE_CONNECTOR_Composite]   = "Composite",
	[DRM_MODE_CONNECTOR_SVIDEO]      = "SVIDEO",
	[DRM_MODE_CONNECTOR_LVDS]        = "LVDS",
	[DRM_MODE_CONNECTOR_Component]   = "Component",
	[DRM_MODE_CONNECTOR_9PinDIN]     = "DIN",
	[DRM_MODE_CONNECTOR_DisplayPort] = "DP",
	[DRM_MODE_CONNECTOR_HDMIA]       = "HDMI-A",
	[DRM_MODE_CONNECTOR_HDMIB]       = "HDMI-B",
	[DRM_MODE_CONNECTOR_TV]          = "TV",
	[DRM_MODE_CONNECTOR_eDP]         = "eDP",
#ifdef DRM_MODE_CONNECTOR_DSI
	[DRM_MODE_CONNECTOR_VIRTUAL]     = "Virtual",
	[DRM_MODE_CONNECTOR_DSI]         = "DSI",
#endif
};

static char *
make_connector_name(const drmModeConnector *con)
{
	char name[32];
	const char *type_name = NULL;

	if (con->connector_type < ARRAY_LENGTH(connector_type_names))
		type_name = connector_type_names[con->connector_type];

	if (!type_name)
		type_name = "UNNAMED";

	snprintf(name, sizeof name, "%s-%d", type_name, con->connector_type_id);

	return strdup(name);
}

static int
find_crtc_for_connector(struct drm_backend *b,
			drmModeRes *resources, drmModeConnector *connector)
{
	drmModeEncoder *encoder;
	uint32_t possible_crtcs;
	int i, j;

	for (j = 0; j < connector->count_encoders; j++) {
		encoder = drmModeGetEncoder(b->drm.fd, connector->encoders[j]);
		if (encoder == NULL) {
			weston_log("Failed to get encoder.\n");
			return -1;
		}
		possible_crtcs = encoder->possible_crtcs;
		drmModeFreeEncoder(encoder);

		for (i = 0; i < resources->count_crtcs; i++) {
			if (possible_crtcs & (1 << i) &&
			    !(b->crtc_allocator & (1 << i)))
				return i;
		}
	}

	return -1;
}

static int
find_plane_for_output(struct drm_backend *b,
		      drmModePlaneRes *plane_resources,
		      struct drm_output *output)
{
	drmModePlane *plane;
	uint32_t possible_crtcs;
	int plane_global_idx = -1;
	int plane_local_idx = -1;
	int i, count = -1;

	for (i = 0; i < plane_resources->count_planes; i++) {
		plane = drmModeGetPlane(b->drm.fd, plane_resources->planes[i]);
		if (!plane)
			continue;

		possible_crtcs = plane->possible_crtcs;
		drmModeFreePlane(plane);

		if (possible_crtcs & (1 << output->pipe)) {
			count++;
			if ((plane_global_idx == -1 || count == b->preferred_plane)) {
				plane_global_idx = i;
				plane_local_idx = count;

				if (plane_local_idx == b->preferred_plane)
					break;
			}
		}
	}

	if (plane_local_idx != b->preferred_plane && plane_local_idx != -1) {
		weston_log("unable to use preferred plane %d. "
			   "try using %d instead\n",
			   b->preferred_plane, plane_local_idx);
		return -1;
	}

	return plane_global_idx;
}

/* Init output state that depends on gl or gbm */
static int
drm_output_init_egl(struct drm_output *output, struct drm_backend *b)
{
	if (b->use_egldevice) {
		int w = output->base.current_mode->width;
		int h = output->base.current_mode->height;

		/* Create a black dumb fb for modesetting */
		output->dumb[0] = drm_fb_create_dumb(b, w, h);
		if (!output->dumb[0]) {
			weston_log("failed to create dumb framebuffer\n");
			return -1;
		}
		memset(output->dumb[0]->map, 0, output->dumb[0]->size);

		if (b->preferred_plane >= 0) {
			/* Try to composite to a plane layer */
			drmModePlaneRes *plane_resources;

			plane_resources = drmModeGetPlaneResources(b->drm.fd);
			if (!plane_resources) {
				/* Fall back to composite to CRTC layer */
				weston_log("failed to get plane resources: %m\n");
				b->preferred_plane = -1;
			} else {
				int j = find_plane_for_output(b,
							      plane_resources,
							      output);
				if (j >= 0) {
					output->plane_id =
						 plane_resources->planes[j];
				} else {
					/* Fall back to composite to CRTC layer */
					weston_log("unable to find a suitable "
						   "plane resource. compositing"
						   " to primary plane.\n");
					return -1;
				}
				drmModeFreePlaneResources(plane_resources);
			}
		}

		if (gl_renderer->output_stream_create(&output->base, -1,
						      output->plane_id,
						      output->crtc_id,
						      gl_renderer->egloutput_stream_attribs) < 0) {
			weston_log("failed to create gl renderer output "
				   "stream state\n");
			drm_fb_destroy_dumb(output->dumb[0]);
			output->dumb[0] = NULL;
			return -1;
		}

	} else {
#ifdef USE_GBM
		EGLint format[2] = {
			output->gbm_format,
			fallback_format_for(output->gbm_format),
		};
		int i, flags, n_formats = 1;

		output->gbm_surface = gbm_surface_create(
					b->gbm,
					output->base.current_mode->width,
					output->base.current_mode->height,
					format[0],
					GBM_BO_USE_SCANOUT |
					GBM_BO_USE_RENDERING);
		if (!output->gbm_surface) {
			weston_log("failed to create gbm surface\n");
			return -1;
		}

		if (format[1])
			n_formats = 2;
		if (gl_renderer->output_window_create(
				&output->base,
				(EGLNativeWindowType)output->gbm_surface,
				output->gbm_surface,
				gl_renderer->opaque_attribs,
				format,
				n_formats) < 0) {
			weston_log("failed to create gl renderer output "
				   "state\n");
			gbm_surface_destroy(output->gbm_surface);
			return -1;
		}

		flags = GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE;

		for (i = 0; i < 2; i++) {
			if (output->gbm_cursor_bo[i])
				continue;

			output->gbm_cursor_bo[i] =
				gbm_bo_create(b->gbm,
					      b->cursor_width,
					      b->cursor_height,
					      GBM_FORMAT_ARGB8888,
					      flags);
		}

		if (output->gbm_cursor_bo[0] == NULL || output->gbm_cursor_bo[1] == NULL) {
			weston_log("cursor buffers unavailable, using gl cursors\n");
			b->cursors_are_broken = 1;
		}
#endif
	}

	return 0;
}

static void
drm_output_fini_egl(struct drm_output *output)
{
	gl_renderer->output_destroy(&output->base);

	if (output->dumb[0]) {
		drm_fb_destroy_dumb(output->dumb[0]);
		output->dumb[0] = NULL;
	}

#ifdef USE_GBM
	if (output->gbm_surface)
		gbm_surface_destroy(output->gbm_surface);
#endif
}

static int
drm_output_init_pixman(struct drm_output *output, struct drm_backend *b)
{
	int w = output->base.current_mode->width;
	int h = output->base.current_mode->height;
	unsigned int i;

	/* FIXME error checking */

	for (i = 0; i < ARRAY_LENGTH(output->dumb); i++) {
		output->dumb[i] = drm_fb_create_dumb(b, w, h);
		if (!output->dumb[i])
			goto err;

		output->image[i] =
			pixman_image_create_bits(PIXMAN_x8r8g8b8, w, h,
						 output->dumb[i]->map,
						 output->dumb[i]->stride);
		if (!output->image[i])
			goto err;
	}

	if (pixman_renderer_output_create(&output->base) < 0)
		goto err;

	pixman_region32_init_rect(&output->previous_damage,
				  output->base.x, output->base.y, output->base.width, output->base.height);

	return 0;

err:
	for (i = 0; i < ARRAY_LENGTH(output->dumb); i++) {
		if (output->dumb[i])
			drm_fb_destroy_dumb(output->dumb[i]);
		if (output->image[i])
			pixman_image_unref(output->image[i]);

		output->dumb[i] = NULL;
		output->image[i] = NULL;
	}

	return -1;
}

static void
drm_output_fini_pixman(struct drm_output *output)
{
	unsigned int i;

	pixman_renderer_output_destroy(&output->base);
	pixman_region32_fini(&output->previous_damage);

	for (i = 0; i < ARRAY_LENGTH(output->dumb); i++) {
		drm_fb_destroy_dumb(output->dumb[i]);
		pixman_image_unref(output->image[i]);
		output->dumb[i] = NULL;
		output->image[i] = NULL;
	}
}

static void
edid_parse_string(const uint8_t *data, char text[])
{
	int i;
	int replaced = 0;

	/* this is always 12 bytes, but we can't guarantee it's null
	 * terminated or not junk. */
	strncpy(text, (const char *) data, 12);

	/* guarantee our new string is null-terminated */
	text[12] = '\0';

	/* remove insane chars */
	for (i = 0; text[i] != '\0'; i++) {
		if (text[i] == '\n' ||
		    text[i] == '\r') {
			text[i] = '\0';
			break;
		}
	}

	/* ensure string is printable */
	for (i = 0; text[i] != '\0'; i++) {
		if (!isprint(text[i])) {
			text[i] = '-';
			replaced++;
		}
	}

	/* if the string is random junk, ignore the string */
	if (replaced > 4)
		text[0] = '\0';
}

#define EDID_DESCRIPTOR_ALPHANUMERIC_DATA_STRING	0xfe
#define EDID_DESCRIPTOR_DISPLAY_PRODUCT_NAME		0xfc
#define EDID_DESCRIPTOR_DISPLAY_PRODUCT_SERIAL_NUMBER	0xff
#define EDID_OFFSET_DATA_BLOCKS				0x36
#define EDID_OFFSET_LAST_BLOCK				0x6c
#define EDID_OFFSET_PNPID				0x08
#define EDID_OFFSET_SERIAL				0x0c

static int
edid_parse(struct drm_edid *edid, const uint8_t *data, size_t length)
{
	int i;
	uint32_t serial_number;

	/* check header */
	if (length < 128)
		return -1;
	if (data[0] != 0x00 || data[1] != 0xff)
		return -1;

	/* decode the PNP ID from three 5 bit words packed into 2 bytes
	 * /--08--\/--09--\
	 * 7654321076543210
	 * |\---/\---/\---/
	 * R  C1   C2   C3 */
	edid->pnp_id[0] = 'A' + ((data[EDID_OFFSET_PNPID + 0] & 0x7c) / 4) - 1;
	edid->pnp_id[1] = 'A' + ((data[EDID_OFFSET_PNPID + 0] & 0x3) * 8) + ((data[EDID_OFFSET_PNPID + 1] & 0xe0) / 32) - 1;
	edid->pnp_id[2] = 'A' + (data[EDID_OFFSET_PNPID + 1] & 0x1f) - 1;
	edid->pnp_id[3] = '\0';

	/* maybe there isn't a ASCII serial number descriptor, so use this instead */
	serial_number = (uint32_t) data[EDID_OFFSET_SERIAL + 0];
	serial_number += (uint32_t) data[EDID_OFFSET_SERIAL + 1] * 0x100;
	serial_number += (uint32_t) data[EDID_OFFSET_SERIAL + 2] * 0x10000;
	serial_number += (uint32_t) data[EDID_OFFSET_SERIAL + 3] * 0x1000000;
	if (serial_number > 0)
		sprintf(edid->serial_number, "%lu", (unsigned long) serial_number);

	/* parse EDID data */
	for (i = EDID_OFFSET_DATA_BLOCKS;
	     i <= EDID_OFFSET_LAST_BLOCK;
	     i += 18) {
		/* ignore pixel clock data */
		if (data[i] != 0)
			continue;
		if (data[i+2] != 0)
			continue;

		/* any useful blocks? */
		if (data[i+3] == EDID_DESCRIPTOR_DISPLAY_PRODUCT_NAME) {
			edid_parse_string(&data[i+5],
					  edid->monitor_name);
		} else if (data[i+3] == EDID_DESCRIPTOR_DISPLAY_PRODUCT_SERIAL_NUMBER) {
			edid_parse_string(&data[i+5],
					  edid->serial_number);
		} else if (data[i+3] == EDID_DESCRIPTOR_ALPHANUMERIC_DATA_STRING) {
			edid_parse_string(&data[i+5],
					  edid->eisa_id);
		}
	}
	return 0;
}

static void
find_and_parse_output_edid(struct drm_backend *b,
			   struct drm_output *output,
			   drmModeConnector *connector)
{
	drmModePropertyBlobPtr edid_blob = NULL;
	drmModePropertyPtr property;
	int i;
	int rc;

	for (i = 0; i < connector->count_props && !edid_blob; i++) {
		property = drmModeGetProperty(b->drm.fd, connector->props[i]);
		if (!property)
			continue;
		if ((property->flags & DRM_MODE_PROP_BLOB) &&
		    !strcmp(property->name, "EDID")) {
			edid_blob = drmModeGetPropertyBlob(b->drm.fd,
							   connector->prop_values[i]);
		}
		drmModeFreeProperty(property);
	}
	if (!edid_blob)
		return;

	rc = edid_parse(&output->edid,
			edid_blob->data,
			edid_blob->length);
	if (!rc) {
		weston_log("EDID data '%s', '%s', '%s'\n",
			   output->edid.pnp_id,
			   output->edid.monitor_name,
			   output->edid.serial_number);
		if (output->edid.pnp_id[0] != '\0')
			output->base.make = output->edid.pnp_id;
		if (output->edid.monitor_name[0] != '\0')
			output->base.model = output->edid.monitor_name;
		if (output->edid.serial_number[0] != '\0')
			output->base.serial_number = output->edid.serial_number;
	}
	drmModeFreePropertyBlob(edid_blob);
}



static int
parse_modeline(const char *s, drmModeModeInfo *mode)
{
	char hsync[16];
	char vsync[16];
	float fclock;

	mode->type = DRM_MODE_TYPE_USERDEF;
	mode->hskew = 0;
	mode->vscan = 0;
	mode->vrefresh = 0;
	mode->flags = 0;

	if (sscanf(s, "%f %hd %hd %hd %hd %hd %hd %hd %hd %15s %15s",
		   &fclock,
		   &mode->hdisplay,
		   &mode->hsync_start,
		   &mode->hsync_end,
		   &mode->htotal,
		   &mode->vdisplay,
		   &mode->vsync_start,
		   &mode->vsync_end,
		   &mode->vtotal, hsync, vsync) != 11)
		return -1;

	mode->clock = fclock * 1000;
	if (strcmp(hsync, "+hsync") == 0)
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	else if (strcmp(hsync, "-hsync") == 0)
		mode->flags |= DRM_MODE_FLAG_NHSYNC;
	else
		return -1;

	if (strcmp(vsync, "+vsync") == 0)
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
	else if (strcmp(vsync, "-vsync") == 0)
		mode->flags |= DRM_MODE_FLAG_NVSYNC;
	else
		return -1;

	snprintf(mode->name, sizeof mode->name, "%dx%d@%.3f",
		 mode->hdisplay, mode->vdisplay, fclock);

	return 0;
}

static void
setup_output_seat_constraint(struct drm_backend *b,
			     struct weston_output *output,
			     const char *s)
{
	if (strcmp(s, "") != 0) {
		struct weston_pointer *pointer;
		struct udev_seat *seat;

		seat = udev_seat_get_named(&b->input, s);
		if (!seat)
			return;

		seat->base.output = output;

		pointer = weston_seat_get_pointer(&seat->base);
		if (pointer)
			weston_pointer_clamp(pointer,
					     &pointer->x,
					     &pointer->y);
	}
}

#ifdef USE_GBM
static int
parse_gbm_format(const char *s, uint32_t default_value, uint32_t *gbm_format)
{
	int ret = 0;

	if (s == NULL)
		*gbm_format = default_value;
	else if (strcmp(s, "xrgb8888") == 0)
		*gbm_format = GBM_FORMAT_XRGB8888;
	else if (strcmp(s, "rgb565") == 0)
		*gbm_format = GBM_FORMAT_RGB565;
	else if (strcmp(s, "xrgb2101010") == 0)
		*gbm_format = GBM_FORMAT_XRGB2101010;
	else {
		weston_log("fatal: unrecognized pixel format: %s\n", s);
		ret = -1;
	}

	return ret;
}
#endif

/**
 * Choose suitable mode for an output
 *
 * Find the most suitable mode to use for initial setup (or reconfiguration on
 * hotplug etc) for a DRM output.
 *
 * @param output DRM output to choose mode for
 * @param kind Strategy and preference to use when choosing mode
 * @param width Desired width for this output
 * @param height Desired height for this output
 * @param current_mode Mode currently being displayed on this output
 * @param modeline Manually-entered mode (may be NULL)
 * @returns A mode from the output's mode list, or NULL if none available
 */
static struct drm_mode *
drm_output_choose_initial_mode(struct drm_backend *backend,
			       struct drm_output *output,
			       enum weston_drm_backend_output_mode mode,
			       struct weston_drm_backend_output_config *config,
			       const drmModeModeInfo *current_mode)
{
	struct drm_mode *preferred = NULL;
	struct drm_mode *current = NULL;
	struct drm_mode *configured = NULL;
	struct drm_mode *best = NULL;
	struct drm_mode *drm_mode;
	drmModeModeInfo modeline;
	int32_t width = 0;
	int32_t height = 0;

	if (mode == WESTON_DRM_BACKEND_OUTPUT_PREFERRED && config->modeline) {
		if (sscanf(config->modeline, "%dx%d", &width, &height) != 2) {
			width = -1;

			if (parse_modeline(config->modeline, &modeline) == 0) {
				configured = drm_output_add_mode(output, &modeline);
				if (!configured)
					return NULL;
			} else {
				weston_log("Invalid modeline \"%s\" for output %s\n",
					   config->modeline, output->base.name);
			}
		}
	}

	wl_list_for_each_reverse(drm_mode, &output->base.mode_list, base.link) {
		if (width == drm_mode->base.width &&
		    height == drm_mode->base.height)
			configured = drm_mode;

		if (memcmp(current_mode, &drm_mode->mode_info,
			   sizeof *current_mode) == 0)
			current = drm_mode;

		if (drm_mode->base.flags & WL_OUTPUT_MODE_PREFERRED)
			preferred = drm_mode;

		best = drm_mode;
	}

	if (current == NULL && current_mode->clock != 0) {
		current = drm_output_add_mode(output, current_mode);
		if (!current)
			return NULL;
	}

	if (mode == WESTON_DRM_BACKEND_OUTPUT_CURRENT)
		configured = current;

	if (configured)
		return configured;

	if (preferred)
		return preferred;

	if (current)
		return current;

	if (best)
		return best;

	weston_log("no available modes for %s\n", output->base.name);
	return NULL;
}

static int
connector_get_current_mode(drmModeConnector *connector, int drm_fd,
			   drmModeModeInfo *mode)
{
	drmModeEncoder *encoder;
	drmModeCrtc *crtc;

	/* Get the current mode on the crtc that's currently driving
	 * this connector. */
	encoder = drmModeGetEncoder(drm_fd, connector->encoder_id);
	memset(mode, 0, sizeof *mode);
	if (encoder != NULL) {
		crtc = drmModeGetCrtc(drm_fd, encoder->crtc_id);
		drmModeFreeEncoder(encoder);
		if (crtc == NULL)
			return -1;
		if (crtc->mode_valid)
			*mode = crtc->mode;
		drmModeFreeCrtc(crtc);
	}

	return 0;
}

/**
 * Create and configure a Weston output structure
 *
 * Given a DRM connector, create a matching drm_output structure and add it
 * to Weston's output list.
 *
 * @param b Weston backend structure structure
 * @param resources DRM resources for this device
 * @param connector DRM connector to use for this new output
 * @param x Horizontal offset to use into global co-ordinate space
 * @param y Vertical offset to use into global co-ordinate space
 * @param drm_device udev device pointer
 * @returns 0 on success, or -1 on failure
 */
static int
create_output_for_connector(struct drm_backend *b,
			    drmModeRes *resources,
			    drmModeConnector *connector,
			    int x, int y, struct udev_device *drm_device)
{
	struct drm_output *output;
	struct drm_mode *drm_mode, *next, *current;
	struct weston_mode *m;

	drmModeModeInfo crtc_mode;
	int i;
	enum weston_drm_backend_output_mode mode;
	struct weston_drm_backend_output_config config = {{ 0 }};

	i = find_crtc_for_connector(b, resources, connector);
	if (i < 0) {
		weston_log("No usable crtc/encoder pair for connector.\n");
		return -1;
	}

	output = zalloc(sizeof *output);
	if (output == NULL)
		return -1;

	output->base.subpixel = drm_subpixel_to_wayland(connector->subpixel);
	output->base.name = make_connector_name(connector);
	output->base.make = "unknown";
	output->base.model = "unknown";
	output->base.serial_number = "unknown";
	wl_list_init(&output->base.mode_list);

	mode = b->configure_output(b->compositor, b->use_current_mode,
				   output->base.name, &config);
#ifdef USE_GBM
	if (parse_gbm_format(config.gbm_format, b->gbm_format, &output->gbm_format) == -1)
		output->gbm_format = b->gbm_format;
#endif

	setup_output_seat_constraint(b, &output->base,
				     config.seat ? config.seat : "");
	free(config.seat);

	output->crtc_id = resources->crtcs[i];
	output->pipe = i;
	b->crtc_allocator |= (1 << output->pipe);
	output->connector_id = connector->connector_id;
	b->connector_allocator |= (1 << output->connector_id);

	output->plane_id = ~0u;

	output->original_crtc = drmModeGetCrtc(b->drm.fd, output->crtc_id);
	output->dpms_prop = drm_get_prop(b->drm.fd, connector, "DPMS");

	if (connector_get_current_mode(connector, b->drm.fd, &crtc_mode) < 0)
		goto err_free;

	for (i = 0; i < connector->count_modes; i++) {
		drm_mode = drm_output_add_mode(output, &connector->modes[i]);
		if (!drm_mode)
			goto err_free;
	}

	if (mode == WESTON_DRM_BACKEND_OUTPUT_OFF) {
		weston_log("Disabling output %s\n", output->base.name);
		drmModeSetCrtc(b->drm.fd, output->crtc_id,
			       0, 0, 0, 0, 0, NULL);
		goto err_free;
	}

	current = drm_output_choose_initial_mode(b, output, mode, &config,
						 &crtc_mode);
	if (!current)
		goto err_free;
	output->base.current_mode = &current->base;
	output->base.current_mode->flags |= WL_OUTPUT_MODE_CURRENT;

	weston_output_init(&output->base, b->compositor, x, y,
			   connector->mmWidth, connector->mmHeight,
			   config.base.transform, config.base.scale);

	if (b->use_pixman) {
		if (drm_output_init_pixman(output, b) < 0) {
			weston_log("Failed to init output pixman state\n");
			goto err_output;
		}
	} else if (drm_output_init_egl(output, b) < 0) {
		weston_log("Failed to init output gl state\n");
		goto err_output;
	}

	output->backlight = backlight_init(drm_device,
					   connector->connector_type);
	if (output->backlight) {
		weston_log("Initialized backlight, device %s\n",
			   output->backlight->path);
		output->base.set_backlight = drm_set_backlight;
		output->base.backlight_current = drm_get_backlight(output);
	} else {
		weston_log("Failed to initialize backlight\n");
	}

	weston_compositor_add_output(b->compositor, &output->base);

	find_and_parse_output_edid(b, output, connector);
	if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
		output->base.connection_internal = 1;

	output->base.start_repaint_loop = drm_output_start_repaint_loop;
	output->base.repaint = drm_output_repaint;
	output->base.destroy = drm_output_destroy;
	output->base.assign_planes = drm_assign_planes;
	output->base.set_dpms = drm_set_dpms;
	output->base.switch_mode = drm_output_switch_mode;

	output->base.gamma_size = output->original_crtc->gamma_size;
	output->base.set_gamma = drm_output_set_gamma;

	weston_plane_init(&output->cursor_plane, b->compositor,
			  INT32_MIN, INT32_MIN);
	weston_plane_init(&output->fb_plane, b->compositor, 0, 0);

	weston_compositor_stack_plane(b->compositor, &output->cursor_plane, NULL);
	weston_compositor_stack_plane(b->compositor, &output->fb_plane,
				      &b->compositor->primary_plane);

	weston_log("Output %s, (connector %d, crtc %d)\n",
		   output->base.name, output->connector_id, output->crtc_id);
	wl_list_for_each(m, &output->base.mode_list, link)
		weston_log_continue(STAMP_SPACE "mode %dx%d@%.1f%s%s%s\n",
				    m->width, m->height, m->refresh / 1000.0,
				    m->flags & WL_OUTPUT_MODE_PREFERRED ?
				    ", preferred" : "",
				    m->flags & WL_OUTPUT_MODE_CURRENT ?
				    ", current" : "",
				    connector->count_modes == 0 ?
				    ", built-in" : "");

	/* Set native_ fields, so weston_output_mode_switch_to_native() works */
	output->base.native_mode = output->base.current_mode;
	output->base.native_scale = output->base.current_scale;

	return 0;

err_output:
	weston_output_destroy(&output->base);
err_free:
	wl_list_for_each_safe(drm_mode, next, &output->base.mode_list,
			      base.link) {
		wl_list_remove(&drm_mode->base.link);
		free(drm_mode);
	}

	drmModeFreeCrtc(output->original_crtc);
	b->crtc_allocator &= ~(1 << output->pipe);
	b->connector_allocator &= ~(1 << output->connector_id);
	free(output);
	free(config.modeline);

	return -1;
}

/*
 * Return a string describing the type of a DRM object
 */
static const char *
drm_object_type_str(uint32_t obj_type)
{
	switch (obj_type) {
		case DRM_MODE_OBJECT_CRTC:	return "crtc";
		case DRM_MODE_OBJECT_CONNECTOR: return "connector";
		case DRM_MODE_OBJECT_ENCODER:	return "encoder";
		case DRM_MODE_OBJECT_MODE:	return "mode";
		case DRM_MODE_OBJECT_PROPERTY:	return "property";
		case DRM_MODE_OBJECT_FB:	return "fb";
		case DRM_MODE_OBJECT_BLOB:	return "blob";
		case DRM_MODE_OBJECT_PLANE:	return "plane";
	}
	return "unknown";
}

/*
 * Cache DRM property IDs
 *
 * Given a particular DRM object, find specified properties by name, and
 * cache their internal property ID. Taking a list of names in prop_names, each
 * corresponding entry in prop_ids (matching names) will be populated with
 * corresponding ID.
 *
 * All users of DRM object properties in this file should use this
 * mechanism.
 *
 * Property lookup is not mandatory and may fail; users should carefully
 * check the return value, which is a bitmask populated with the indices
 * of properties which were successfully looked up.
 *
 * @param ec Internal DRM compositor structure
 * @param prop_ids Array of internal property IDs
 * @param prop_names Array of property names to look up
 * @param nprops Length of prop_ids and prop_names arrays
 * @param obj_id DRM object ID
 * @param obj_type DRM object type (DRM_MODE_OBJECT_*)
 *
 * @returns Bitmask of populated entries in prop_ids
 */
static uint32_t
drm_fetch_object_properties(struct drm_backend *b,
			    uint32_t *prop_ids,
			    const char * const *prop_names,
			    uint32_t nprops,
			    uint32_t obj_id,
			    uint32_t obj_type)
{
	drmModeObjectProperties *props;
	drmModePropertyRes *prop;
	uint32_t valid_mask = 0;
	unsigned i, j;

	memset(prop_ids, 0, nprops * sizeof(*prop_ids));

	props = drmModeObjectGetProperties(b->drm.fd, obj_id, obj_type);
	if (!props) {
		weston_log("Unable to get properties for object %u of type %#x '%s'\n",
			   obj_id, obj_type, drm_object_type_str(obj_type));
		return valid_mask;
	}

	for (i = 0; i < props->count_props; i++) {
		prop = drmModeGetProperty(b->drm.fd, props->props[i]);
		if (!prop)
			continue;

		for (j = 0; j < nprops; j++)
			if (!strncmp(prop->name, prop_names[j], DRM_PROP_NAME_LEN))
				break;

		if (j == nprops) {
			weston_log("Unrecognized property %u '%s' on object %u of type "
				   "%#x '%s'\n", prop->prop_id, prop->name, obj_id,
				   obj_type, drm_object_type_str(obj_type));
			drmModeFreeProperty(prop);
			continue;
		}

		prop_ids[j] = prop->prop_id;
		valid_mask |= (1 << j);
	}

	for (i = 0; i < nprops; i++)
		if (prop_ids[i] == 0)
			weston_log("Property '%s' missing from obj %u of type %#x '%s'\n",
				   prop_names[i], obj_id, obj_type,
				   drm_object_type_str(obj_type));

	drmModeFreeObjectProperties(props);

	return valid_mask;
}

/*
 * Initialize DRM properties for the given sprite
 *
 * @param sprite Output sprite whose properties will be initialized
 * @returns 0 on success; -1 otherwise
 */
static int
sprite_properties_init(struct drm_sprite *sprite)
{
	uint32_t required_items = 0;

	sprite->props.valid = drm_fetch_object_properties(sprite->backend,
							  sprite->props.ids,
							  wplane_prop_names,
							  WPLANE_PROP_COUNT,
							  sprite->plane_id,
							  DRM_MODE_OBJECT_PLANE);

	if (sprite->backend->atomic_modeset)
		required_items |=
			(1 << WPLANE_PROP_SRC_X)   | (1 << WPLANE_PROP_SRC_Y)  | \
			(1 << WPLANE_PROP_SRC_W)   | (1 << WPLANE_PROP_SRC_H)  | \
			(1 << WPLANE_PROP_CRTC_X)  | (1 << WPLANE_PROP_CRTC_Y) | \
			(1 << WPLANE_PROP_CRTC_W)  | (1 << WPLANE_PROP_CRTC_H) | \
			(1 << WPLANE_PROP_CRTC_ID);

	if ((sprite->props.valid & required_items) != required_items) {
		weston_log("Failed to look up plane properties (expected 0x%x, "
				   "got 0x%x) on ID %d\n", required_items,
				   sprite->props.valid, sprite->plane_id);
		return -1;
	}

	return 0;
}

static void
create_sprites(struct drm_backend *b)
{
	struct drm_sprite *sprite, *next;
	struct drm_output *output;
	drmModePlaneRes *plane_res;
	drmModePlane *plane;
	struct gl_renderer_overlay_data *overlay_data = NULL;
	uint32_t next_overlay = 0;
	uint32_t i, mask;

	/*
	 * Overlays are discovered here and their information
	 * stored. drmModeSetPlane() is used only to change
	 * the position and size of the overlay.
	 * The actual rendering is done by connecting
	 * EGLOutputLayerEXT object correponding to an overlay
	 * as a consumer of an EGLStreamKHR that accepts
	 * input from a client buffer
	 */
	plane_res = drmModeGetPlaneResources(b->drm.fd);
	if (!plane_res) {
		weston_log("failed to get plane resources: %s\n",
			   strerror(errno));
		return;
	}

	overlay_data = (struct gl_renderer_overlay_data *)
		zalloc(sizeof(struct gl_renderer_overlay_data) *
		      (plane_res->count_planes));

	if (overlay_data == NULL) {
		drmModeFreePlaneResources(plane_res);
		weston_log("failed to allocate overlay data array\n");
		return;
	}

	for (i = 0; i < plane_res->count_planes; i++) {
		plane = drmModeGetPlane(b->drm.fd, plane_res->planes[i]);
		if (!plane)
			continue;
		/* TODO
		 * This assumes that there is 1-1 correspondence between
		 * crtc and connectors so if a connector is in the connected
		 * state when discovered through create_outputs() then it
		 * has a unique crtc index whose bit is set in b->crtc_allocator.
		 * Thus the following check is essentially to filter out planes
		 * for connectors/crtcs which are not connected.
		 * We need to do this because for now when egl wl_surface objects
		 * are attached to the renderer it must immediately be connected
		 * through an egl stream to an drmLayerEXT corresponding
		 * to an overlay. Consequently after the backend+renderer has been
		 * initialized we need to have ready a set of overlays that are
		 * useable.
		 * Problems:
		 * 1. hot plugging will cause problems because potentially some
		 *    attached wl_surface and the corresponding EGLSurfaces already
		 *    created will become invalid. Because of the nature of EGLStream
		 *    once it is disconnected from a consumer it can no longer be
		 *    reconnected to a different one. This means the EGLSurface object
		 *    created as producer will become permanently invalid once the
		 *    consumer (the output) is unplugged. This is probably not
		 *    solvable with current property of EGLStream.
		 * 2. Once wl_surface has been attached to an drmLayerEXT overlay
		 *    it is permanently attached until it is destroyed. We cannot
		 *    re-arrange that overlay to be the target of a different wl_surface
		 *    this is also due to issue 1. above and is an inflexibility that
		 *    is caused by the use of egl streams.
		 * The above 2 issues can be solved with EGL_KHR_switch_stream probably
		 *
		 * Issue 2 can also be resolved by regenerating the stream. See
		 * Bug 1707929
		 */
		if (!(plane->possible_crtcs & b->crtc_allocator)) {
			drmModeFreePlane(plane);
			continue;
		}

		sprite = zalloc(sizeof(*sprite) + ((sizeof(uint32_t)) * plane->count_formats));
		if (!sprite) {
			weston_log("%s: out of memory\n", __func__);
			drmModeFreePlane(plane);
			continue;
		}

		sprite->possible_crtcs = plane->possible_crtcs;
		sprite->plane_id = plane->plane_id;

		/* all overlays disabled at initialization */
		sprite->disabled = 1;

		sprite->update_sprite = 1;

		sprite->backend = b;
		sprite->count_formats = plane->count_formats;
		memcpy(sprite->formats, plane->formats,
		       plane->count_formats * sizeof(plane->formats[0]));
		drmModeFreePlane(plane);

		if (sprite_properties_init(sprite) < 0) {
			free(sprite);
			weston_log("failed to set sprite properties\n");
			continue;
		}
		weston_plane_init(&sprite->plane, b->compositor, 0, 0);
		weston_compositor_stack_plane(b->compositor, &sprite->plane,
					      &b->compositor->primary_plane);

		/* Populate zpos element of sprite struct */
		if (sprite->props.valid & (1 << WPLANE_PROP_ZPOS)) {
			drmModePropertyPtr zpos = drmModeGetProperty(b->drm.fd,
								     sprite->props.ids[WPLANE_PROP_ZPOS]);
			sprite->zpos = zpos->values[0];
			drmModeFreeProperty(zpos);
		}

		/* Populate alpha element of sprite struct */
		if (sprite->props.valid & (1 << WPLANE_PROP_ALPHA)) {
			drmModePropertyPtr alpha = drmModeGetProperty(b->drm.fd,
								      sprite->props.ids[WPLANE_PROP_ALPHA]);
			sprite->alpha = alpha->values[0];
			drmModeFreeProperty(alpha);
		}

		/* Find possible outputs for the given overlay */
		mask = 0;
		wl_list_for_each(output, &b->compositor->output_list, base.link) {
			if (drm_sprite_crtc_supported(output,
						      sprite->possible_crtcs))
				mask |= (1 << output->base.id);
		}

		overlay_data[next_overlay].id = sprite->plane_id;
		overlay_data[next_overlay].possible_outputs = mask;
		next_overlay = next_overlay + 1;

		wl_list_insert(&b->sprite_list, &sprite->link);
	}

	drmModeFreePlaneResources(plane_res);

	/* Register the overlays with the gl-renderer to be assignable
	 * to weston buffers
	 */
	if (gl_renderer->register_overlays(b->compositor, next_overlay,
					   overlay_data) != 0) {
		weston_log("failed to register overlay with gl renderer\n");
		wl_list_for_each_safe(sprite, next, &b->sprite_list, link) {
			weston_plane_release(&sprite->plane);
			free(sprite);
		}
		free(overlay_data);
		return;
	}

	free(overlay_data);

	return;
}

static void
destroy_sprites(struct drm_backend *backend)
{
	struct drm_sprite *sprite, *next;
	struct drm_output *output;

	output = container_of(backend->compositor->output_list.next,
			      struct drm_output, base.link);

	wl_list_for_each_safe(sprite, next, &backend->sprite_list, link) {
		disable_sprite(sprite);
		drm_output_release_fb(output, sprite->current);
		drm_output_release_fb(output, sprite->next);
		weston_plane_release(&sprite->plane);
		free(sprite);
	}
}

static void
show_available_connectors(int drm_fd)
{
	drmModeConnector *connector;
	drmModeRes *resources;
	const char *type_name;
	const char *connected;
	int i;

	weston_log("Available drm connectors:\n");

	resources = drmModeGetResources(drm_fd);

	if (!resources) {
		weston_log("drmModeGetResources failed\n");
		return;
	}

	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(drm_fd,
						resources->connectors[i]);
		if (connector == NULL)
			continue;

		if (connector->connector_type < ARRAY_LENGTH(connector_type_names))
			type_name = connector_type_names[connector->connector_type];
		else
			type_name = "UNKNOWN";

		if (connector->connection == DRM_MODE_CONNECTED)
			connected = "Connected";
		else
			connected = "Not connected";

		weston_log("    %d (%s, %s)\n", connector->connector_id, type_name,
			connected);
	}

	if (resources->count_connectors == 0)
		weston_log("    <none>\n");

	drmModeFreeResources(resources);
}

static int
create_outputs(struct drm_backend *b, uint32_t option_connector,
	       struct udev_device *drm_device)
{
	drmModeConnector *connector;
	drmModeRes *resources;
	int i;
	int x = 0, y = 0;

	resources = drmModeGetResources(b->drm.fd);
	if (!resources) {
		weston_log("drmModeGetResources failed\n");
		return -1;
	}

	b->crtcs = calloc(resources->count_crtcs, sizeof(uint32_t));
	if (!b->crtcs) {
		drmModeFreeResources(resources);
		return -1;
	}

	b->min_width  = resources->min_width;
	b->max_width  = resources->max_width;
	b->min_height = resources->min_height;
	b->max_height = resources->max_height;

	b->num_crtcs = resources->count_crtcs;
	memcpy(b->crtcs, resources->crtcs, sizeof(uint32_t) * b->num_crtcs);

	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(b->drm.fd,
						resources->connectors[i]);
		if (connector == NULL)
			continue;

		if (connector->connection == DRM_MODE_CONNECTED &&
		    (option_connector == 0 ||
		     connector->connector_id == option_connector)) {
			if (create_output_for_connector(b, resources,
							connector, x, y,
							drm_device) < 0) {
				drmModeFreeConnector(connector);
				continue;
			}

			x += container_of(b->compositor->output_list.prev,
					  struct weston_output,
					  link)->width;
		}

		drmModeFreeConnector(connector);
	}

	if (wl_list_empty(&b->compositor->output_list)) {
		if (option_connector)
			weston_log("No currently active connector matching ID"
				   " %d found.\n", option_connector);
		else
			weston_log("No currently active connector found.\n");
		show_available_connectors(b->drm.fd);
		weston_log("No currently active connector found.\n");
		drmModeFreeResources(resources);
		return -1;
	}

	drmModeFreeResources(resources);

	return 0;
}

static void
update_outputs(struct drm_backend *b, struct udev_device *drm_device)
{
	drmModeConnector *connector;
	drmModeRes *resources;
	struct drm_output *output, *next;
	int x = 0, y = 0;
	uint32_t connected = 0, disconnects = 0;
	int i;

	resources = drmModeGetResources(b->drm.fd);
	if (!resources) {
		weston_log("drmModeGetResources failed\n");
		return;
	}

	/* collect new connects */
	for (i = 0; i < resources->count_connectors; i++) {
		int connector_id = resources->connectors[i];

		connector = drmModeGetConnector(b->drm.fd, connector_id);
		if (connector == NULL)
			continue;

		if (connector->connection != DRM_MODE_CONNECTED) {
			drmModeFreeConnector(connector);
			continue;
		}

		connected |= (1 << connector_id);

		if (!(b->connector_allocator & (1 << connector_id))) {
			struct weston_output *last =
				container_of(b->compositor->output_list.prev,
					     struct weston_output, link);

			/* XXX: not yet needed, we die with 0 outputs */
			if (!wl_list_empty(&b->compositor->output_list))
				x = last->x + last->width;
			else
				x = 0;
			y = 0;
			create_output_for_connector(b, resources,
						    connector, x, y,
						    drm_device);
			weston_log("connector %d connected\n", connector_id);

		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);

	disconnects = b->connector_allocator & ~connected;
	if (disconnects) {
		wl_list_for_each_safe(output, next, &b->compositor->output_list,
				      base.link) {
			if (disconnects & (1 << output->connector_id)) {
				disconnects &= ~(1 << output->connector_id);
				weston_log("connector %d disconnected\n",
				       output->connector_id);
				drm_output_destroy(&output->base);
			}
		}
	}

	/* FIXME: handle zero outputs, without terminating */
	if (b->connector_allocator == 0)
		weston_compositor_exit(b->compositor);
}

static int
udev_event_is_hotplug(struct drm_backend *b, struct udev_device *device)
{
	const char *sysnum;
	const char *val;

	sysnum = udev_device_get_sysnum(device);
	if (!sysnum || atoi(sysnum) != b->drm.id)
		return 0;

	val = udev_device_get_property_value(device, "HOTPLUG");
	if (!val)
		return 0;

	return strcmp(val, "1") == 0;
}

static int
udev_drm_event(int fd, uint32_t mask, void *data)
{
	struct drm_backend *b = data;
	struct udev_device *event;

	event = udev_monitor_receive_device(b->udev_monitor);

	if (udev_event_is_hotplug(b, event))
		update_outputs(b, event);

	udev_device_unref(event);

	return 1;
}

static void
drm_restore(struct weston_compositor *ec)
{
	weston_launcher_restore(ec->launcher);
}

static void
drm_destroy(struct weston_compositor *ec)
{
	struct drm_backend *b = (struct drm_backend *) ec->backend;

	drm_cursors_destroy(b);

	if (b->input.libinput)
		udev_input_destroy(&b->input);

	if (!b->use_egldevice)
		wl_event_source_remove(b->udev_drm_source);

	wl_event_source_remove(b->drm_source);

	destroy_sprites(b);

	weston_compositor_shutdown(ec);

#ifdef USE_GBM
	if (b->gbm)
		gbm_device_destroy(b->gbm);
#endif

	weston_launcher_destroy(ec->launcher);

	close(b->drm.fd);
	free(b);
}

static void
drm_backend_set_modes(struct drm_backend *backend)
{
	struct drm_output *output;
	struct drm_mode *drm_mode;
	int ret;

	wl_list_for_each(output, &backend->compositor->output_list, base.link) {
		if (!output->current) {
			/* If something that would cause the output to
			 * switch mode happened while in another vt, we
			 * might not have a current drm_fb. In that case,
			 * schedule a repaint and let drm_output_repaint
			 * handle setting the mode. */
			weston_output_schedule_repaint(&output->base);
			continue;
		}

		drm_mode = (struct drm_mode *) output->base.current_mode;
		ret = drmModeSetCrtc(backend->drm.fd, output->crtc_id,
				     output->current->fb_id, 0, 0,
				     &output->connector_id, 1,
				     &drm_mode->mode_info);
		if (ret < 0) {
			weston_log(
				"failed to set mode %dx%d for output at %d,%d: %m\n",
				drm_mode->base.width, drm_mode->base.height,
				output->base.x, output->base.y);
		}
	}
}

static void
session_notify(struct wl_listener *listener, void *data)
{
	struct weston_compositor *compositor = data;
	struct drm_backend *b = (struct drm_backend *)compositor->backend;
	struct drm_sprite *sprite;
	struct drm_output *output;

	if (compositor->session_active) {
		weston_log("activating session\n");
		compositor->state = b->prev_state;
		drm_backend_set_modes(b);
		weston_compositor_damage_all(compositor);
		if (b->input.libinput)
			udev_input_enable(&b->input);
	} else {
		weston_log("deactivating session\n");
		if (b->input.libinput)
			udev_input_disable(&b->input);

		b->prev_state = compositor->state;
		weston_compositor_offscreen(compositor);

		/* If we have a repaint scheduled (either from a
		 * pending pageflip or the idle handler), make sure we
		 * cancel that so we don't try to pageflip when we're
		 * vt switched away.  The OFFSCREEN state will prevent
		 * further attemps at repainting.  When we switch
		 * back, we schedule a repaint, which will process
		 * pending frame callbacks. */

		wl_list_for_each(output, &compositor->output_list, base.link) {
			output->base.repaint_needed = 0;
			drmModeSetCursor(b->drm.fd, output->crtc_id, 0, 0, 0);
		}

		output = container_of(compositor->output_list.next,
				      struct drm_output, base.link);

		wl_list_for_each(sprite, &b->sprite_list, link) {
			drmModeSetPlane(b->drm.fd,
					sprite->plane_id,
					output->crtc_id, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0);
			disable_sprite(sprite);
		}
	};
}

/*
 * Find primary GPU
 * Some systems may have multiple DRM devices attached to a single seat. This
 * function loops over all devices and tries to find a PCI device with the
 * boot_vga sysfs attribute set to 1.
 * If no such device is found, the first DRM device reported by udev is used.
 */
static struct udev_device*
find_primary_gpu(struct drm_backend *b, const char *seat)
{
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	const char *path, *device_seat, *id;
	struct udev_device *device, *drm_device, *pci;

	e = udev_enumerate_new(b->udev);
	udev_enumerate_add_match_subsystem(e, "drm");
	udev_enumerate_add_match_sysname(e, "card[0-9]*");

	udev_enumerate_scan_devices(e);
	drm_device = NULL;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		path = udev_list_entry_get_name(entry);
		device = udev_device_new_from_syspath(b->udev, path);
		if (!device)
			continue;
		device_seat = udev_device_get_property_value(device, "ID_SEAT");
		if (!device_seat)
			device_seat = default_seat;
		if (strcmp(device_seat, seat)) {
			udev_device_unref(device);
			continue;
		}

		pci = udev_device_get_parent_with_subsystem_devtype(device,
								"pci", NULL);
		if (pci) {
			id = udev_device_get_sysattr_value(pci, "boot_vga");
			if (id && !strcmp(id, "1")) {
				if (drm_device)
					udev_device_unref(drm_device);
				drm_device = device;
				break;
			}
		}

		if (!drm_device)
			drm_device = device;
		else
			udev_device_unref(device);
	}

	udev_enumerate_unref(e);
	return drm_device;
}

static void
planes_binding(struct weston_keyboard *keyboard, uint32_t time, uint32_t key,
	       void *data)
{
	struct drm_backend *b = data;

	switch (key) {
	case KEY_C:
		b->cursors_are_broken ^= 1;
		break;
	case KEY_V:
		b->sprites_are_broken ^= 1;
		break;
	case KEY_O:
		b->sprites_hidden ^= 1;
		break;
	default:
		break;
	}
}

#ifdef BUILD_VAAPI_RECORDER
static void
recorder_destroy(struct drm_output *output)
{
	vaapi_recorder_destroy(output->recorder);
	output->recorder = NULL;

	output->base.disable_planes--;

	wl_list_remove(&output->recorder_frame_listener.link);
	weston_log("[libva recorder] done\n");
}

static void
recorder_frame_notify(struct wl_listener *listener, void *data)
{
	struct drm_output *output;
	struct drm_backend *b;
	int fd, ret;

	output = container_of(listener, struct drm_output,
			      recorder_frame_listener);
	b = (struct drm_backend *)output->base.compositor->backend;

	if (!output->recorder)
		return;

	ret = drmPrimeHandleToFD(b->drm.fd, output->current->handle,
				 DRM_CLOEXEC, &fd);
	if (ret) {
		weston_log("[libva recorder] "
			   "failed to create prime fd for front buffer\n");
		return;
	}

	ret = vaapi_recorder_frame(output->recorder, fd,
				   output->current->stride);
	if (ret < 0) {
		weston_log("[libva recorder] aborted: %m\n");
		recorder_destroy(output);
	}
}

static void *
create_recorder(struct drm_backend *b, int width, int height,
		const char *filename)
{
	int fd;
	drm_magic_t magic;

	fd = open(b->drm.filename, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return NULL;

	drmGetMagic(fd, &magic);
	drmAuthMagic(b->drm.fd, magic);

	return vaapi_recorder_create(fd, width, height, filename);
}

static void
recorder_binding(struct weston_keyboard *keyboard, uint32_t time, uint32_t key,
		 void *data)
{
	struct drm_backend *b = data;
	struct drm_output *output;
	int width, height;

	if (b->use_egldevice) {
		weston_log("recorder not supported with EGL device\n");
		return;
	}

	output = container_of(b->compositor->output_list.next,
			      struct drm_output, base.link);

	if (!output->recorder) {
		if (output->gbm_format != GBM_FORMAT_XRGB8888) {
			weston_log("failed to start vaapi recorder: "
				   "output format not supported\n");
			return;
		}

		width = output->base.current_mode->width;
		height = output->base.current_mode->height;

		output->recorder =
			create_recorder(b, width, height, "capture.h264");
		if (!output->recorder) {
			weston_log("failed to create vaapi recorder\n");
			return;
		}

		output->base.disable_planes++;

		output->recorder_frame_listener.notify = recorder_frame_notify;
		wl_signal_add(&output->base.frame_signal,
			      &output->recorder_frame_listener);

		weston_output_schedule_repaint(&output->base);

		weston_log("[libva recorder] initialized\n");
	} else {
		recorder_destroy(output);
	}
}
#else
static void
recorder_binding(struct weston_keyboard *keyboard, uint32_t time, uint32_t key,
		 void *data)
{
	weston_log("Compiled without libva support\n");
}
#endif

static void
switch_to_gl_renderer(struct drm_backend *b)
{
	struct drm_output *output;
	bool dmabuf_support_inited;

	if (!b->use_pixman)
		return;

	dmabuf_support_inited = !!b->compositor->renderer->import_dmabuf;

	weston_log("Switching to GL renderer\n");

	if (b->use_egldevice) {
		b->egldevice = find_primary_egldevice(b->drm.filename);
		if (b->egldevice == EGL_NO_DEVICE_EXT) {
			weston_log("Failed to create EGL device. "
				   "Aborting renderer switch\n");
			return;
		}
	} else {
#ifdef USE_GBM
		b->gbm = create_gbm_device(b->drm.fd);
		if (!b->gbm) {
			weston_log("Failed to create gbm device. "
				   "Aborting renderer switch\n");
			return;
		}
#endif
	}

	wl_list_for_each(output, &b->compositor->output_list, base.link)
		pixman_renderer_output_destroy(&output->base);

	b->compositor->renderer->destroy(b->compositor);

	if (drm_backend_create_gl_renderer(b) < 0) {
#ifdef USE_GBM
		if (b->gbm)
			gbm_device_destroy(b->gbm);
#endif
		weston_log("Failed to create GL renderer. Quitting.\n");
		/* FIXME: we need a function to shutdown cleanly */
		assert(0);
	}

	wl_list_for_each(output, &b->compositor->output_list, base.link)
		drm_output_init_egl(output, b);

	b->use_pixman = 0;

	if (!dmabuf_support_inited && b->compositor->renderer->import_dmabuf) {
		if (linux_dmabuf_setup(b->compositor) < 0)
			weston_log("Error: initializing dmabuf "
				   "support failed.\n");
	}
}

static void
renderer_switch_binding(struct weston_keyboard *keyboard, uint32_t time,
			uint32_t key, void *data)
{
	struct drm_backend *b =
		(struct drm_backend *) keyboard->seat->compositor;

	switch_to_gl_renderer(b);
}

static struct drm_backend *
drm_backend_create(struct weston_compositor *compositor,
		   struct weston_drm_backend_config *config)
{
	struct drm_backend *b;
	struct udev_device *drm_device;
	struct wl_event_loop *loop;
	const char *path;
	const char *seat_id = default_seat;
	struct drm_output *output;

	weston_log("initializing drm backend\n");

	b = zalloc(sizeof *b);
	if (b == NULL)
		return NULL;

	/* Backend needs to be set before create_sprites() otherwise init
	 * will seg fault, as the crtc tracking functions dereference the
	 * backend
	 */
	compositor->backend = &b->base;

	b->compositor = compositor;
	b->use_pixman = config->use_pixman;
#ifdef USE_GBM
	b->use_egldevice = config->use_egldevice;
#else
	b->use_egldevice = 1;
#endif
	b->configure_output = config->configure_output;
	b->use_current_mode = config->use_current_mode;
	b->preferred_plane = config->preferred_plane;

#ifdef USE_GBM
	if (parse_gbm_format(config->gbm_format, GBM_FORMAT_XRGB8888, &b->gbm_format) < 0)
		goto err_compositor;
#endif

	if (config->seat_id)
		seat_id = config->seat_id;

	/* Check if we run drm-backend using weston-launch */
	compositor->launcher = weston_launcher_connect(compositor, config->tty,
						       seat_id, true);
	if (compositor->launcher == NULL) {
		weston_log("fatal: drm backend should be run "
			   "using weston-launch binary or as root\n");
		goto err_compositor;
	}

	b->udev = udev_new();
	if (b->udev == NULL) {
		weston_log("failed to initialize udev context\n");
		goto err_launcher;
	}

	b->session_listener.notify = session_notify;
	wl_signal_add(&compositor->session_signal, &b->session_listener);

	gl_renderer = weston_load_module("gl-renderer.so",
					 "gl_renderer_interface");
	if (!gl_renderer) {
		weston_log("failed to load gl renderer\n");
		goto err_udev;
	}

	if (b->use_egldevice) {
		b->egldevice = find_primary_egldevice(NULL);
		if (b->egldevice == EGL_NO_DEVICE_EXT) {
			goto err_udev;
		}

		if (drm_open_egldevice(b) < 0) {
			weston_log("failed to open drm device\n");
			goto err_udev;
		}

		path = b->drm.filename;
		drm_device = NULL;
	} else {
		drm_device = find_primary_gpu(b, seat_id);
		if (drm_device == NULL) {
			weston_log("no drm device found\n");
			goto err_udev;
		}
		path = udev_device_get_syspath(drm_device);
		if (drm_open_udev(b, drm_device) < 0) {
			weston_log("failed to open drm device\n");
			goto err_udev;
		}
	}

	if (init_drm(b) < 0) {
		weston_log("failed to initialize kms\n");
		goto err_udev_dev;
	}

	if (b->use_pixman) {
		if (init_pixman(b) < 0) {
			weston_log("failed to initialize pixman renderer\n");
			goto err_udev_dev;
		}
	} else {
		if (init_egl(b) < 0) {
			weston_log("failed to initialize egl\n");
			goto err_udev_dev;
		}
	}

	b->base.destroy = drm_destroy;
	b->base.restore = drm_restore;

	b->prev_state = WESTON_COMPOSITOR_ACTIVE;

	weston_setup_vt_switch_bindings(compositor);

	if (!config->no_input)
		if (udev_input_init(&b->input,
				    compositor, b->udev, seat_id) < 0)
			weston_log("failed to create input devices\n");

	if (create_outputs(b, config->connector, drm_device) < 0) {
		weston_log("failed to create output for %s\n", path);
		goto err_udev_input;
	}

	/* Sprite creation requires outputs created in advance */
	wl_list_init(&b->sprite_list);
	create_sprites(b);

	/* Initialize drm cursors.  This has to be done after outputs are created */
	if (!config->no_input)
		drm_cursors_init(b);

	/* A this point we have some idea of whether or not we have a working
	 * cursor plane. */
	if (!b->cursors_are_broken)
		compositor->capabilities |= WESTON_CAP_CURSOR_PLANE;

	path = NULL;

	loop = wl_display_get_event_loop(compositor->wl_display);
	b->drm_source =
		wl_event_loop_add_fd(loop, b->drm.fd,
				     WL_EVENT_READABLE, on_drm_input, b);

	/* If compositing to a non-CRTC plane, create repaint timers for each
	 * output, and schedule the first paint
	 */
	if (b->preferred_plane >= 0) {
		wl_list_for_each(output, &b->compositor->output_list, base.link) {
			output->finish_timer_source =
				wl_event_loop_add_timer(loop,
							on_finish_frame,
							output);
			wl_event_source_timer_update(output->finish_timer_source,
						     REPAINT_TIME_MS);
		}
	}

	if (!b->use_egldevice) {
		b->udev_monitor =
			udev_monitor_new_from_netlink(b->udev, "udev");
		if (b->udev_monitor == NULL) {
			weston_log("failed to intialize udev monitor\n");
			goto err_drm_source;
		}
		udev_monitor_filter_add_match_subsystem_devtype(b->udev_monitor,
								"drm", NULL);
		b->udev_drm_source =
			wl_event_loop_add_fd(loop,
					     udev_monitor_get_fd(b->udev_monitor),
					     WL_EVENT_READABLE, udev_drm_event, b);

		if (udev_monitor_enable_receiving(b->udev_monitor) < 0) {
			weston_log("failed to enable udev-monitor receiving\n");
			goto err_udev_monitor;
		}

		udev_device_unref(drm_device);
	}

	if (gl_renderer->set_attribute(b->compositor,
				       GL_RENDERER_ATTR_ALL_EGL_TO_PRIMARY,
				       config->overlay_compositing) < 0) {
		weston_log("failed to set renderer attribute\n");
		goto err_udev_monitor;
	}
	if (gl_renderer->set_attribute(b->compositor,
				       GL_RENDERER_ATTR_ALLOW_ATOMIC_PAGE_FLIPS,
				       b->atomic_modeset) < 0)
		/*
		 * We were unable to set GL_RENDERER_ATTR_ALLOW_ATOMIC_PAGE_FLIPS, so
		 * disable atomic modesetting
		 */
		b->atomic_modeset = 0;

	weston_compositor_add_debug_binding(compositor, KEY_O,
					    planes_binding, b);
	weston_compositor_add_debug_binding(compositor, KEY_C,
					    planes_binding, b);
	weston_compositor_add_debug_binding(compositor, KEY_V,
					    planes_binding, b);
	weston_compositor_add_debug_binding(compositor, KEY_Q,
					    recorder_binding, b);
	weston_compositor_add_debug_binding(compositor, KEY_W,
					    renderer_switch_binding, b);

	if (compositor->renderer->import_dmabuf) {
		if (linux_dmabuf_setup(compositor) < 0)
			weston_log("Error: initializing dmabuf "
				   "support failed.\n");
	}

	return b;

err_udev_monitor:
	wl_event_source_remove(b->udev_drm_source);
	udev_monitor_unref(b->udev_monitor);
err_drm_source:
	wl_event_source_remove(b->drm_source);
#ifdef USE_GBM
	if (b->gbm)
		gbm_device_destroy(b->gbm);
#endif
	destroy_sprites(b);
err_udev_input:
	if (b->input.libinput)
		udev_input_destroy(&b->input);
err_udev_dev:
	udev_device_unref(drm_device);
err_udev:
	udev_unref(b->udev);
err_launcher:
	weston_launcher_destroy(compositor->launcher);
err_compositor:
	weston_compositor_shutdown(compositor);
	free(b);
	compositor->backend = NULL;
	return NULL;
}

static void
config_init_to_defaults(struct weston_drm_backend_config *config)
{
}

WL_EXPORT int
backend_init(struct weston_compositor *compositor,
	     int *argc, char *argv[],
	     struct weston_config *wc,
	     struct weston_backend_config *config_base)
{
	struct drm_backend *b;
	struct weston_drm_backend_config config = {{ 0, }};

	if (config_base == NULL ||
	    config_base->struct_version != WESTON_DRM_BACKEND_CONFIG_VERSION ||
	    config_base->struct_size > sizeof(struct weston_drm_backend_config)) {
		weston_log("drm backend config structure is invalid\n");
		return -1;
	}

	config_init_to_defaults(&config);
	memcpy(&config, config_base, config_base->struct_size);

	b = drm_backend_create(compositor, &config);
	if (b == NULL)
		return -1;

	return 0;
}
