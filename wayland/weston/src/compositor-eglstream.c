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

#include <assert.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>

#include "compositor.h"
#include "shared/helpers.h"
#include "gl-renderer.h"
#include "launcher-util.h"

#define DEFAULT_EGLSTREAM_TYPE EGLSTREAM_TYPE_CROSS_PART
#define DEFAULT_EGLSTREAM_STREAM_REMOTE 1
#define DEFAULT_EGLSTREAM_IP "12.0.0.1"
#define DEFAULT_EGLSTREAM_PORT 8888
#define DEFAULT_EGLSTREAM_NODELAY 0
#define DEFAULT_EGLSTREAM_WIDTH 1920
#define DEFAULT_EGLSTREAM_HEIGHT 1080
#define DEFAULT_EGLSTREAM_FULLSCREEN 0
#define DEFAULT_EGLSTREAM_FRAMERATE 60
#define DEFAULT_EGLSTREAM_CONSUMER_LATENCY_USEC 0
#define DEFAULT_EGLSTREAM_ACQUIRE_TIMEOUT_USEC 16000
#define DEFAULT_EGLSTREAM_FIFO_LENGTH 0
#define DEFAULT_EGLSTREAM_FIFO_SYNCHRONOUS 0
#define DEFAULT_EGLSTREAM_SCALE 1
#define DEFAULT_EGLSTREAM_TRANSFORM WL_OUTPUT_TRANSFORM_NORMAL

struct eglstream_backend {
	struct weston_backend base;
	struct weston_compositor *compositor;

	struct wl_listener session_listener;
	uint32_t prev_state;
};

struct eglstream_mode {
	struct weston_mode base;
	uint32_t refresh_ms; // Refresh period in milliseconds
};

enum eglstream_type {
	EGLSTREAM_TYPE_CROSS_PART,
	EGLSTREAM_TYPE_CROSS_PROC
};

struct eglstream_output {
	struct weston_output   base;

	struct wl_event_source *finish_frame_timer;

	int sock;

	enum eglstream_type type;
	EGLint stream_remote;
	EGLint timeout;
	EGLint fifo_synchronous;
	EGLint fifo_length;
};

static struct gl_renderer_interface *gl_renderer;

static const char default_seat[] = "seat0";
static const int default_tty = 1;

static int
eglstream_output_repaint(struct weston_output *output_base,
			 pixman_region32_t *damage)
{
	struct eglstream_output *output = (struct eglstream_output *) output_base;
	struct weston_compositor *c = output->base.compositor;
	struct eglstream_mode *mode = container_of(output->base.current_mode,
						   struct eglstream_mode,
						   base);

	output->base.compositor->renderer->repaint_output(&output->base,
							  damage);
	pixman_region32_subtract(&c->primary_plane.damage,
				 &c->primary_plane.damage, damage);

	wl_event_source_timer_update(output->finish_frame_timer,
				     mode->refresh_ms);

	return -1;
}

static void
eglstream_output_start_repaint_loop(struct weston_output *output_base)
{
	struct timespec ts;

	weston_compositor_read_presentation_clock(output_base->compositor, &ts);
	weston_output_finish_frame(output_base, &ts,
				   WP_PRESENTATION_FEEDBACK_INVALID);
}

static int
finish_frame_handler(void *data)
{
	struct eglstream_output *output = data;
	struct timespec ts;

	weston_compositor_read_presentation_clock(output->base.compositor, &ts);
	weston_output_finish_frame(&output->base, &ts, 0);

	return 1;
}

static void
eglstream_output_destroy(struct weston_output *output_base)
{
	struct eglstream_output *output = (struct eglstream_output *) output_base;

	wl_event_source_remove(output->finish_frame_timer);

	gl_renderer->output_destroy(&output->base);
	weston_output_destroy(&output->base);

	free(output);
}

static int
init_egl(struct eglstream_backend *b)
{
	int ret = 0;
	EGLint device_platform_attribs[] = {
		EGL_NONE
	};

	ret = gl_renderer->display_create(b->compositor,
					  EGL_PLATFORM_DEVICE_EXT,
					  (void *)0,
					  device_platform_attribs,
					  gl_renderer->opaque_stream_attribs,
					  NULL,
					  0);

	if (ret < 0) {
		return -1;
	}

	if (!gl_renderer->has_extension(b->compositor, GL_RENDERER_EXTENSION_EGL_STREAM_REMOTE)) {
		weston_log("failed since required extension EGL_NV_stream_remote not supported\n");
		return -1;
	}

	return 0;
}

/* Init output state that depends on gl */
static int
eglstream_output_init_egl(struct eglstream_output *output,
			  struct eglstream_backend *b)
{
	int count = 0;
	EGLAttrib *stream_attribs = NULL;
	EGLAttrib attribs[50];

	if (output->stream_remote) {
		stream_attribs = attribs;

		attribs[count++] = EGL_CONSUMER_LATENCY_USEC_KHR;
		attribs[count++] = 0;

		attribs[count++] = EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR;
		attribs[count++] = output->timeout;

		attribs[count++] = EGL_STREAM_TYPE_NV;
		attribs[count++] = (output->type == EGLSTREAM_TYPE_CROSS_PART) ?
						    EGL_STREAM_CROSS_PARTITION_NV :
						    EGL_STREAM_CROSS_PROCESS_NV;

		attribs[count++] = EGL_STREAM_ENDPOINT_NV;
		attribs[count++] = EGL_STREAM_PRODUCER_NV;

		attribs[count++] = EGL_STREAM_PROTOCOL_NV;
		attribs[count++] = EGL_STREAM_PROTOCOL_SOCKET_NV;

		if (gl_renderer->has_extension(b->compositor,
		    GL_RENDERER_EXTENSION_EGL_STREAM_FIFO)) {
			attribs[count++] = EGL_STREAM_FIFO_LENGTH_KHR;
			attribs[count++] = output->fifo_length;
		}

		if (gl_renderer->has_extension(b->compositor,
		    GL_RENDERER_EXTENSION_EGL_STREAM_FIFO_SYNCHRONOUS)) {
			attribs[count++] = EGL_STREAM_FIFO_SYNCHRONOUS_NV;
			attribs[count++] = output->fifo_synchronous;
		}

		attribs[count++] = EGL_SOCKET_HANDLE_NV;
		attribs[count++] = output->sock;

		attribs[count++] = EGL_SOCKET_TYPE_NV;
		attribs[count++] = (output->type == EGLSTREAM_TYPE_CROSS_PART) ?
						    EGL_SOCKET_TYPE_INET_NV :
						    EGL_SOCKET_TYPE_UNIX_NV;

		attribs[count++] = EGL_NONE;
	}

	if (gl_renderer->output_stream_create(&output->base, output->sock, ~0u,
					      0, stream_attribs) < 0) {
		weston_log("failed to create gl renderer output stream state\n");
		return -1;
	}

	return 0;
}

static uint32_t
parse_eglstream_type(const char *type, const char *output_name)
{
	static const struct {
		const char *name; uint32_t token;
	} names[] = {
		{ "cross-partition", EGLSTREAM_TYPE_CROSS_PART },
		{ "cross-process", EGLSTREAM_TYPE_CROSS_PROC }
	};
	unsigned int i;

	for (i = 0; i < ARRAY_LENGTH(names); i++)
		if (strcmp(names[i].name, type) == 0)
			return names[i].token;

	weston_log("Invalid type \"%s\" for output %s\n",
		   type, output_name);
	weston_log("Valid types:\n");
	for (i = 0; i < ARRAY_LENGTH(names); i++)
		weston_log("\t%s\n", names[i].name);

	for (i = 0; i < ARRAY_LENGTH(names); i++)
		if (names[i].token == DEFAULT_EGLSTREAM_TYPE) {
			weston_log("Using default eglstream type: %s\n",
				   names[i].name);
			break;
		}

	return DEFAULT_EGLSTREAM_TYPE;
}

static void
eglstream_restore(struct weston_compositor *ec)
{
	weston_launcher_restore(ec->launcher);
}

static void
eglstream_destroy(struct weston_compositor *ec)
{
	struct eglstream_backend *b = (struct eglstream_backend *) ec->backend;
	struct eglstream_output *output = NULL;

	wl_list_for_each(output, &b->compositor->output_list, base.link) {
		if (output->sock >= 0)
			close(output->sock);
	}

	weston_compositor_shutdown(ec);
	weston_launcher_destroy(ec->launcher);

	free(b);
}

static void
eglstream_backend_set_modes(struct eglstream_backend *backend)
{
	struct eglstream_output *output;

	wl_list_for_each(output, &backend->compositor->output_list, base.link)
		weston_output_schedule_repaint(&output->base);
}

static void
session_notify(struct wl_listener *listener, void *data)
{
	struct weston_compositor *compositor = data;
	struct eglstream_backend *b = (struct eglstream_backend *)compositor->backend;
	struct eglstream_output *output;

	if (compositor->session_active) {
		weston_log("activating session\n");
		compositor->state = b->prev_state;
		eglstream_backend_set_modes(b);
		weston_compositor_damage_all(compositor);

	} else {
		weston_log("deactivating session\n");
		b->prev_state = compositor->state;
		weston_compositor_offscreen(compositor);

		wl_list_for_each(output, &compositor->output_list, base.link) {
			output->base.repaint_needed = 0;
		}

		output = container_of(compositor->output_list.next,
				      struct eglstream_output, base.link);
	};
}

static int sock_send_str(int sock, const char *buf)
{
	int len, retval, sent = 0;

	len = strlen(buf);
	while (len) {
		retval = send(sock, buf + sent, len, 0);
		if (retval < 0)
			return retval;

		len -= retval;
		sent += retval;
	}

	return sent;
}

static int sock_connect_INET(int sock, const char *ip, int port)
{
	struct sockaddr_in server_addr;

	server_addr.sin_addr.s_addr	= inet_addr(ip);
	server_addr.sin_family		= AF_INET;
	server_addr.sin_port		= htons(port);

	return connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
}

static int sock_connect_UNIX(int *sock, const char *path, int stream_remote)
{
	struct sockaddr_un server_addr = { 0 };
	int sock_id = *sock;

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sun_family = AF_UNIX;
	strncpy(server_addr.sun_path, path, sizeof(server_addr.sun_path)-1);

	if (connect(sock_id, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		goto fail;
	}

	if (!stream_remote) {
		struct msghdr msg = { 0 };
		struct iovec iov;
		struct cmsghdr *cmsg;
		int sockFd = -1;
		char msg_buf[1];
		char ctl_buf[CMSG_SPACE(sizeof(int))];

		iov.iov_base = msg_buf;
		iov.iov_len = sizeof(msg_buf);
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = ctl_buf;
		msg.msg_controllen = sizeof(ctl_buf);

		if (recvmsg(sock_id, &msg, 0) <= 0) {
			goto fail;
		}

		cmsg = CMSG_FIRSTHDR(&msg);
		if (cmsg == NULL
		    || cmsg->cmsg_level != SOL_SOCKET
		    || cmsg->cmsg_type != SCM_RIGHTS) {
			/* Probably connected to somebody else's socket. */
			goto fail;
		}

		memcpy(&sockFd, CMSG_DATA(cmsg), sizeof(int));
		(void)close(sock_id);

		*sock = sockFd;
	}

	return 1;

fail:
	if (sock_id != -1)
		(void)close(sock_id);
	return -1;
}

static struct eglstream_mode *
eglstream_add_mode(struct eglstream_output *output, int width,
		   int height, uint64_t refresh)
{
	struct eglstream_mode *mode;

	mode = malloc(sizeof *mode);
	if (mode == NULL)
		return NULL;

	mode->base.flags = 0;

	mode->base.flags |= WL_OUTPUT_MODE_PREFERRED;
	mode->base.width = width;
	mode->base.height = height;
	mode->base.refresh = refresh;
	mode->refresh_ms = (1000000 + refresh - 1)/refresh;

	wl_list_insert(output->base.mode_list.prev, &mode->base.link);
	return mode;

out_err:
	free(mode);
	return NULL;
}

static int
eglstream_output_switch_mode_dummy(struct weston_output *output_base, struct weston_mode *mode)
{
	return 0;
}
static int
eglstream_create_output(struct eglstream_backend *b,
			int x, int y, enum eglstream_type type,
			int stream_remote, const char *ip, int port,
			int no_delay, const char* path, int width, int height,
			int framerate, int fullscreen, int timeout,
			int fifo_length, int fifo_synchronous, int32_t scale,
			uint32_t transform)
{
	struct eglstream_output *output = NULL;
	struct eglstream_mode *eglstream_mode = NULL;
	struct eglstream_mode *next = NULL;
	struct weston_mode *m = NULL;
	struct wl_event_loop *loop;
	int retval;
	char msg[32];
	int refresh = framerate * 1000;

	output = zalloc(sizeof *output);
	if (output == NULL)
		return -1;

	if (type == EGLSTREAM_TYPE_CROSS_PART)
		output->sock = socket(AF_INET, SOCK_STREAM, 0);
	else
		output->sock = socket(AF_UNIX, SOCK_STREAM, 0);

	if (output->sock < 0) {
		weston_log("create socket failed\n");
		goto fail;
	}

	setsockopt(output->sock, IPPROTO_TCP, TCP_NODELAY,
		   (char *)&no_delay, sizeof(int));

	if (type == EGLSTREAM_TYPE_CROSS_PART) {
		if (sock_connect_INET(output->sock, ip, port) < 0) {
			weston_log("connect socket failed\n");
			goto fail;
		}
	} else if (type == EGLSTREAM_TYPE_CROSS_PROC){
		if (sock_connect_UNIX(&output->sock, path, stream_remote) < 0) {
			weston_log("connect socket failed\n");
			goto fail;
		}
	}

	output->base.subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
	output->base.make = "unknown";
	output->base.model = "unknown";
	output->base.serial_number = "unknown";
	output->base.name = strdup("eglstream");
	wl_list_init(&output->base.mode_list);

	if (type == EGLSTREAM_TYPE_CROSS_PART) {
		// TODO: Fix any server that depends on this so we don't have to
		// send anything if we don't care.
		retval = sock_send_str(output->sock, "weston: mode?");
		if (retval < 0) {
			weston_log("send query failed\n");
			goto fail;
		}

		retval = recv(output->sock, msg, sizeof(msg), 0);
		if (retval < 0) {
			weston_log("receive response failed\n");
			goto fail;
		}

		msg[retval] = '\0';
		weston_log("reply: %s\n", msg);

		if (fullscreen) {
			retval = sscanf(msg, "%ux%u@%u", &width, &height, &refresh);
			if (retval < 3) {
				weston_log("parsing server response failed\n");
				goto fail;
			}
		}
	}

	eglstream_mode = eglstream_add_mode(output, width, height, refresh);
	if (!eglstream_mode) {
		weston_log("no mode detected\n");
		goto fail;
	}

	output->base.current_mode = &eglstream_mode->base;
	output->base.current_mode->flags |= WL_OUTPUT_MODE_CURRENT;
	weston_output_init(&output->base, b->compositor, x, y,
			   eglstream_mode->base.width,
			   eglstream_mode->base.height,
			   transform, scale);

	output->type = type;
	output->stream_remote = stream_remote;
	output->timeout = timeout;
	output->fifo_length = fifo_length;
	output->fifo_synchronous = fifo_synchronous;

	weston_log("Output %s, (type %d, stream_remote %d, ip %s, port %d,"
		   "nodelay %d, path %s, timeout %d, fifo length %d, fifo"
		   "synchronous %d, scale %d, transform %d)\n",
		   output->base.name, output->type, output->stream_remote, ip,
		   port, no_delay, path, output->timeout, output->fifo_length,
		   output->fifo_synchronous, scale, transform);
	wl_list_for_each(m, &output->base.mode_list, link)
		weston_log_continue(STAMP_SPACE "mode %dx%d@%.1f%s%s\n",
				    m->width, m->height, m->refresh / 1000.0,
				    m->flags & WL_OUTPUT_MODE_PREFERRED ?
				    ", preferred" : "",
				    m->flags & WL_OUTPUT_MODE_CURRENT ?
				    ", current" : "");

	if (eglstream_output_init_egl(output, b) < 0) {
		weston_log("Failed to init output gl state\n");
		goto fail;
	}

	wl_list_insert(b->compositor->output_list.prev, &output->base.link);
	output->base.connection_internal = 1;

	loop = wl_display_get_event_loop(b->compositor->wl_display);
	output->finish_frame_timer =
		wl_event_loop_add_timer(loop, finish_frame_handler, output);

	output->base.start_repaint_loop = eglstream_output_start_repaint_loop;
	output->base.repaint = eglstream_output_repaint;
	output->base.destroy = eglstream_output_destroy;

	output->base.assign_planes = NULL;
	output->base.set_dpms = NULL;
	output->base.switch_mode = eglstream_output_switch_mode_dummy;

	output->base.set_gamma = NULL;

	return 0;

fail:
	wl_list_for_each_safe(eglstream_mode, next, &output->base.mode_list,
			      base.link) {
		wl_list_remove(&eglstream_mode->base.link);
		free(eglstream_mode);
	}

	weston_output_destroy(&output->base);

	if (output->sock >= 0)
		close(output->sock);

	free(output);
	return -1;
}


static int
eglstream_create_outputs(struct eglstream_backend *b)
{
	struct egloutput_output *output;
	struct weston_config_section *section;
	const char *section_name;
	char *name, *temp;
	int i, x = 0;
	enum eglstream_type type;
	int stream_remote;
	char *ip;
	int port;
	int no_delay;
	char* path;
	char *mode;
	int width, height;
	int framerate;
	int fullscreen;
	int timeout;
	int fifo_length;
	int fifo_synchronous;
	int32_t scale;
	uint32_t transform;

	// Create an output for each .ini [output] section with the name "eglstream".
	int created_outputs = 0;
	section = NULL;
	while (weston_config_next_section(b->compositor->config,
					  &section, &section_name)) {
		if (strcmp(section_name, "output") != 0)
			continue;
		weston_config_section_get_string(section, "name", &name, NULL);
		if (name == NULL || strncmp(name, "eglstream", 9)) {
			free(name);
			continue;
		}

		weston_config_section_get_string(section,
						 "type", &temp, NULL);
		if (!temp)
			type = DEFAULT_EGLSTREAM_TYPE;
		else
			type = parse_eglstream_type(temp, name);
		free(temp);

		weston_config_section_get_int(section, "stream-remote",
					      &stream_remote,
					      DEFAULT_EGLSTREAM_STREAM_REMOTE);

		if (stream_remote == 0 && type == EGLSTREAM_TYPE_CROSS_PART) {
			weston_log("Invalid: Cross-partition requires "
			"stream-remote enabled\n");
			return -1;
		}

		if (weston_config_section_get_string(section, "ip",
						     &temp, NULL) == 0)
			if (type == EGLSTREAM_TYPE_CROSS_PROC) {
				weston_log("Attribute ip ignored due to the "
				"stream type\n");
			}

		if (!temp)
			ip = DEFAULT_EGLSTREAM_IP;

		free(temp);

		if (weston_config_section_get_int(section, "port", &port,
					          DEFAULT_EGLSTREAM_PORT) == 0)
			if (type == EGLSTREAM_TYPE_CROSS_PROC) {
				weston_log("Attribute port ignored due to the "
				"stream type\n");
			}

		weston_config_section_get_int(section, "nodelay",
					      &no_delay,
					      DEFAULT_EGLSTREAM_NODELAY);

		if (weston_config_section_get_string(section, "path", &path,
						     NULL) == 0)
			if (type == EGLSTREAM_TYPE_CROSS_PART) {
				weston_log("Attribute path ignored due to the "
				"stream type\n");
			}

		weston_config_section_get_string(section,
						 "mode", &mode, NULL);
		width = DEFAULT_EGLSTREAM_WIDTH;
		height = DEFAULT_EGLSTREAM_HEIGHT;
		if (mode && sscanf(mode, "%dx%d", &width, &height) != 2) {
			weston_log("Invalid mode \"%s\" for output %s\n",
				mode, name);
			free(mode);
			return -1;
		}
		free(mode);

		weston_config_section_get_int(section, "fullscreen",
					      &fullscreen,
					      DEFAULT_EGLSTREAM_FULLSCREEN);

		weston_config_section_get_int(section, "framerate",
					      &framerate,
					      DEFAULT_EGLSTREAM_FRAMERATE);

		weston_config_section_get_int(section, "fifo-length",
					      &fifo_length,
					      DEFAULT_EGLSTREAM_FIFO_LENGTH);

		weston_config_section_get_int(section, "acquire-timeout-usec",
					      &timeout,
					      DEFAULT_EGLSTREAM_ACQUIRE_TIMEOUT_USEC);

		weston_config_section_get_int(section, "fifo-synchronous",
					      &fifo_synchronous,
					      DEFAULT_EGLSTREAM_FIFO_SYNCHRONOUS);

		weston_config_section_get_int(section, "scale",
					      &scale,
					      DEFAULT_EGLSTREAM_SCALE);

		weston_config_section_get_string(section, "transform",
						 &temp, NULL);
		if (!temp)
			transform = DEFAULT_EGLSTREAM_TRANSFORM;
		else
			if (weston_parse_transform(temp, &transform) != 0)
				weston_log("Invalid transform \"%s\"\n", temp);
		free(temp);

		if (eglstream_create_output(b, x, 0, type, stream_remote,
					    ip, port, no_delay, path,
					    width, height, framerate,
					    fullscreen,
					    timeout,
					    fifo_length,
					    fifo_synchronous,
					    scale, transform) < 0) {
			weston_log("failed to create eglstream output\n");
			return -1;
		}
		created_outputs++;

		x += container_of(b->compositor->output_list.prev,
				  struct weston_output,
				  link)->width;
	}

	if (created_outputs == 0) {
		weston_log("--eglstream mode requested, but no "
			"eglstream sections found in weston.ini\n");
	}

	return 0;
}

static struct eglstream_backend *
eglstream_backend_create(struct weston_compositor *compositor)
{
	struct eglstream_backend *b;
	const char *seat_id = default_seat;
	const int tty = default_tty;

	weston_log("initializing eglstream backend\n");

	b = zalloc(sizeof *b);
	if (b == NULL)
		return NULL;

	b->compositor = compositor;

	/* Check if we run eglstream-backend using weston-launch */
	compositor->launcher = weston_launcher_connect(compositor, tty,
						       seat_id, true);
	if (compositor->launcher == NULL) {
		weston_log("fatal: eglstream backend should be run "
			   "using weston-launch binary or as root\n");
		goto err_compositor;
	}

	b->session_listener.notify = session_notify;
	wl_signal_add(&compositor->session_signal, &b->session_listener);

	gl_renderer = weston_load_module("gl-renderer.so",
					 "gl_renderer_interface");
	if (!gl_renderer) {
		weston_log("failed to load gl renderer\n");
		goto err_launcher;
	}

	if (init_egl(b) < 0) {
		weston_log("failed to initialize egl\n");
		goto err_launcher;
	}

	b->base.destroy = eglstream_destroy;
	b->base.restore = eglstream_restore;

	b->prev_state = WESTON_COMPOSITOR_ACTIVE;

	weston_setup_vt_switch_bindings(compositor);

	if (eglstream_create_outputs(b) < 0) {
		weston_log("failed to create eglstream outputs\n");
		goto err_launcher;
	}

	compositor->backend = &b->base;

	return b;

err_launcher:
	weston_launcher_destroy(compositor->launcher);

err_compositor:
	weston_compositor_shutdown(compositor);
	free(b);
	return NULL;
}

WL_EXPORT int
backend_init(struct weston_compositor *compositor,
	int *argc, char *argv[],
	struct weston_config *wc,
	struct weston_backend_config *config_base)
{
	struct eglstream_backend *b;

	b = eglstream_backend_create(compositor);
	if (b == NULL)
		return -1;

	return 0;
}
