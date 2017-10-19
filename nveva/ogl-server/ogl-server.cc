/*
 * Copyright (c) 2015-2016, NVIDIA Corporation.  All Rights Reserved.
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
 * software).   The software must carry the NVIDIA copyright notice shown
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
 * NVIDIA TO USER IS PROVIDED "AS IS."  NVIDIA DISCLAIMS ALL WARRANTIES,
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

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/socket.h>
#include <pthread.h>

#include "ogl-debug.h"
#include "ogl-math.h"
#include "ogl-vao.h"
#include "ogl-image.h"
#include "ogl-texture.h"
#include "ogl-shader.h"

#if defined(WINDOW_SYSTEM_QNX_SCREEN)
#include "ogl-screen.h"
#else
#if defined(ENABLE_WAYLAND_SUPPORT)
#include "ogl-wayland.h"
#endif
#include "ogl-drm.h"
#endif // WINDOW_SYSTEM_QNX_SCREEN
#include "ogl-socket.h"

#define OGL_SERVER_VERSION	"0.0.1-2015.10.19"

#define OGL_SERVER_DRM		"drm-nvdc"

#ifdef HAVE_LIBPNG
#define OGL_SERVER_SPLASH	"splash.png"
#else
#define OGL_SERVER_SPLASH	"splash.tga"
#endif
#define OGL_SERVER_PORT		8888
#define OGL_SERVER_TIMEOUT	16000
#define OGL_SERVER_LATENCY	0
#define OGL_SERVER_NO_DELAY	0
#define OGL_SERVER_RETRY	0
#define OGL_SERVER_IFACE	NULL
#define OGL_SERVER_DISPLAY_ID	-1
#define OGL_SERVER_DISPLAY_MODE	-1
#define OGL_SERVER_WAYLAND	0
#define OGL_SERVER_GLES_ON	1
#define OGL_SERVER_STREAM_WAIT	1000000000

#if defined(WINDOW_SYSTEM_QNX_SCREEN)
#define OGL_SERVER_LAYER -1
#define OGL_SERVER_SCREEN 1
#endif

char* splash_path;
unsigned port = OGL_SERVER_PORT;
unsigned timeout = OGL_SERVER_TIMEOUT;
unsigned latency = OGL_SERVER_LATENCY;
int retry = OGL_SERVER_RETRY;
int no_delay = OGL_SERVER_NO_DELAY;
int display_id = OGL_SERVER_DISPLAY_ID;
int display_mode = OGL_SERVER_DISPLAY_MODE;
char* iface = OGL_SERVER_IFACE;

pthread_t thread;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_socket = PTHREAD_COND_INITIALIZER;
pthread_cond_t cond_stream = PTHREAD_COND_INITIALIZER;

#if defined(WINDOW_SYSTEM_QNX_SCREEN)
int layer = OGL_SERVER_LAYER;
int qnx_screen = OGL_SERVER_SCREEN;
static screen_display_t screen_dpy;
static screen_display_mode_t screen_mode;
#else
static int drm_fd = -1;
static drmModeCrtcPtr crtc;

static struct wl_display *wlDisplay;
#endif
int wayland = OGL_SERVER_WAYLAND;
int gles_on = OGL_SERVER_GLES_ON;


static EGLDisplay display = EGL_NO_DISPLAY;
static EGLContext context = EGL_NO_CONTEXT;
static EGLSurface surface = EGL_NO_SURFACE;
static EGLStreamKHR stream = EGL_NO_STREAM_KHR;
static EGLStreamKHR consumer = EGL_NO_STREAM_KHR;
static EGLSyncKHR streamSync = EGL_NO_SYNC_KHR;

int volatile server_sock = -1;
int volatile accept_sock = -1;

int volatile quit;

struct model {
	mat4_t		*transform;
	mat4_t		*camera;
	col4_t		color;
	ogl_texture_t	texture;
	ogl_vao_t	vao;

	GLuint		program;
	GLint		u_sampler0;
	GLint		u_model;
	GLint		u_camera;
};

static struct model *splash;
static struct model *external;
static mat4_t camera;
static mat4_t identity;

static const char *vs =
	"uniform mat4 u_model;"
	"uniform mat4 u_camera;"
	"attribute mediump vec4 v_vertex;"
	"attribute mediump vec4 v_color;"
	"attribute mediump vec2 v_texture0;"

	"varying mediump vec4 f_color;"
	"varying mediump vec2 f_tcoord0;"

	"void main()"
	"{"
		"f_color = v_color;"
		"f_tcoord0 = v_texture0;"
		"vec4 pos = u_model * v_vertex;"
		"gl_Position = u_camera * pos;"
	"}";

/* Internal texture (splash screen) */
static const char *fsint =
	"uniform sampler2D u_sampler0;"
	"varying mediump vec2 f_tcoord0;"
	"varying mediump vec4 f_color;"

	"void main()"
	"{"
		"mediump vec4 tcolor = texture2D(u_sampler0, f_tcoord0);"
		"gl_FragColor = f_color * tcolor;"
	"}";

/* External texture (consumer stream) */
static const char *fsext =
	"#extension GL_NV_EGL_stream_consumer_external: enable\n"
	"#extension GL_OES_EGL_image_external : enable\n"

	"uniform samplerExternalOES u_sampler0;"
	"varying mediump vec2 f_tcoord0;"
	"varying mediump vec4 f_color;"

	"void main()"
	"{"
		"mediump vec4 tcolor = texture2D(u_sampler0, f_tcoord0);"
		"gl_FragColor = f_color * tcolor;"
	"}";

static const ogl_location_t attr[] = {
	{ "v_vertex", OGL_ATTR_VERTEX },
	{ "v_texture0", OGL_ATTR_TEXTURE0 },
	{ "v_color", OGL_ATTR_COLOR },
	{ NULL, -1 },
};

static void help(const char* name);

static void ModelDelete(struct model *m)
{
	if (!m)
		return;

	oglTextureDelete(&m->texture);
	oglVAODelete(&m->vao);
	glDeleteProgram(m->program);
	free(m->transform);
	free(m);
}

static struct model *ModelGenerate(GLboolean ext, mat4_t *camera)
{
	ogl_location_t unif[] = {
		{ "u_model", -1 },
		{ "u_camera", -1 },
		{ "u_sampler0", -1 },
		{ NULL, -1 },
	};

	struct model *m;
	GLint retval;

	if (!camera)
		return NULL;

	m = (struct model*)calloc(1, sizeof(*m));
	if (!m) {
		ogl_debug("calloc failed");
		return NULL;
	}

	m->transform = (mat4_t*)malloc(sizeof(*m->transform));
	if (!m->transform) {
		ogl_debug("malloc failed");
		goto out_free1;
	}

	glActiveTexture(GL_TEXTURE0);
	retval = oglTextureGenerate(&m->texture);
	if (!retval) {
		ogl_debug("oglTextureGenerate failed");
		goto out_free2;
	}

	retval = oglVAOGenerate(&m->vao);
	if (!retval) {
		ogl_debug("oglVAOGenerate failed");
		goto out_texture;
	}

	m->program = oglCreateProgram(vs, ext ? fsext : fsint, attr, unif);
	if (!m->program) {
		ogl_debug("oglCreateProgram failed");
		goto out_vao;
	}

	m->u_model = unif[0].location;
	m->u_camera = unif[1].location;
	m->u_sampler0 = unif[2].location;

	m->color.r = 1.0f;
	m->color.g = 1.0f;
	m->color.b = 1.0f;
	m->color.a = 1.0f;

	oglIdentityMat4(m->transform);
	m->camera = camera;

	return m;

out_vao:
	oglVAODelete(&m->vao);
out_texture:
	oglTextureDelete(&m->texture);
out_free2:
	free(m->transform);
out_free1:
	free(m);
	return NULL;
}

static struct model *ModelGenerateFromFile(mat4_t *camera, const char *path)
{
	struct model *m;
	ogl_image_t img;
	rect_t ibr;
	rect_t tbr;
	GLint retval;

	m = ModelGenerate(GL_FALSE, camera);
	if (!m)
		return GL_FALSE;

	memset(&img, 0, sizeof(img));
	oglImageFromFile(&img, path);
	if (!img.data) {
		ogl_debug("oglImageFromFile failed");
		goto out;
	}

	retval = oglTextureFromImage(&m->texture, &img);
	if (!retval) {
		ogl_debug("oglTextureFromImage failed");
		goto out;
	}

	ibr.max.x = img.size.x;
	ibr.max.y = img.size.y;
	ibr.min.x = -ibr.max.x;
	ibr.min.y = -ibr.max.y;

	tbr.min.x = 0.0f;
	tbr.min.y = 0.0f;
	tbr.max = m->texture.max;

	retval = oglVAOQuad(&m->vao, &ibr, &tbr);
	if (!retval) {
		ogl_debug("oglVAOQuad failed");
		goto out;
	}
	return m;

out:
	ModelDelete(m);
	return NULL;
}

static struct model *ModelGenerateFromStream(mat4_t *camera,
					     EGLDisplay display,
					     EGLStreamKHR stream)
{
	struct model *m;
	GLint retval;

	m = ModelGenerate(GL_TRUE, camera);
	if (!m)
		return GL_FALSE;

	retval = oglTextureFromStream(&m->texture, display, stream);
	if (!retval) {
		ogl_debug("oglTextureFromStream failed");
		goto out;
	}

	retval = oglVAOQuad(&m->vao, NULL, NULL);
	if (!retval) {
		ogl_debug("oglVAOQuad failed");
		goto out;
	}
	return m;

out:
	ModelDelete(m);
	return NULL;
}

static void ModelSet(struct model *m)
{
	glUseProgram(m->program);
	glUniformMatrix4fv(m->u_model, 1, GL_FALSE,
			   m->transform->ptr);
	glUniformMatrix4fv(m->u_camera, 1, GL_FALSE,
			   m->camera->ptr);
	glUniform1i(m->u_sampler0, 0);
	glBindTexture(m->texture.type, m->texture.id);
	pglBindVertexArray(m->vao.array);
	glVertexAttrib4fv(OGL_ATTR_COLOR, m->color.ptr);
}

static inline void ModelDraw(struct model *m)
{
	glDrawElements(m->vao.mode, m->vao.count, GL_UNSIGNED_SHORT, NULL);
}

static void InitViewport(GLfloat fovy, GLuint width, GLuint height)
{
	mat4_t m1, m2;
	GLfloat scale, aspect;

	scale = 1.0f / height;
	aspect = scale * width;

	oglPerspectiveFOVMat4(&m1, fovy, aspect,
			      0.5f, 5000.0f);
	oglIdentityMat4(&identity);
	oglIdentityMat4(&m2);
	m2.f11 = scale;
	m2.f22 = scale;
	m2.f34 = -m1.f22;

	oglMultiplyMat4(&camera, &m1, &m2);
	glViewport(0, 0, width, height);
}

#ifdef HACK_SCREEN_SIZE
/* Find greatest common divisor */
static inline unsigned gcd(unsigned a, unsigned b)
{
	GLuint c;

	while (a) {
		c = a; a = b % a; b = c;
	}

	return b;
}

static void hack_size(unsigned base, unsigned *width, unsigned *height)
{
	unsigned w = *width;
	unsigned h = *height;
	unsigned d = gcd(w, h);

	w /= d;
	h /= d;
	d = base / w;
	*width = w * d;
	*height = h * d;
}
#else
static void hack_size(unsigned base, unsigned *width, unsigned *height) { }
#endif

static void CloseSock(volatile int *sock)
{
	if (sock && *sock >= 0) {
		close(*sock);
		*sock = -1;
	}
}

static void DestroyStream(EGLStreamKHR *str)
{
	if (str && *str != EGL_NO_STREAM_KHR) {
		peglDestroyStreamKHR(display, *str);
		*str = EGL_NO_STREAM_KHR;
	}
}

static void DestroySyncHandle(EGLSyncKHR *sync)
{
	if(sync && *sync != EGL_NO_SYNC_KHR) {
		peglDestroySyncKHR(display, *sync);
		*sync = EGL_NO_SYNC_KHR;
	}
}

static void SignalHandler(int sig)
{
	ogl_debug("caught signal %i", sig);

	shutdown(accept_sock, SHUT_RDWR);
	shutdown(server_sock, SHUT_RDWR);
	DestroySyncHandle(&streamSync);
	quit = GL_TRUE;
}

static EGLBoolean DrawSplash(void)
{
	if (splash) {
		glEnable(GL_BLEND);
		ModelSet(splash);
		ModelDraw(splash);
	}
	return eglSwapBuffers(display, surface);
}

static EGLBoolean DrawConsumer(void)
{
	EGLBoolean retval;

	retval = peglStreamConsumerAcquireKHR(display, consumer);
	if (!retval) {
		return GL_TRUE;
	}

	ModelDraw(external);

	retval = peglStreamConsumerReleaseKHR(display, consumer);
	if (!retval) {
		ogl_error("eglStreamConsumerReleaseKHR failed");
		return GL_FALSE;
	}

	return eglSwapBuffers(display, surface);
}

static int ProcessOptions(int argc, char **argv)
{
	const char *appname = basename(argv[0]);
	int i;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--iface") ||
		    !strcmp(argv[i], "-i")) {
			if (++i < argc) {
				iface = strdup(argv[i]);
				if (!iface) {
					printf("out of memory\n");
					return 0;
				}
			} else {
				ogl_error("iface option requires an argument");
				help(appname);
				return 0;
			}
		} else if (!strcmp(argv[i], "--port") ||
		           !strcmp(argv[i], "-p")) {
			if (++i < argc) {
				port = strtol(argv[i], NULL, 10);
			} else {
				ogl_error("port option requires an argument");
				help(appname);
				return 0;
			}
		} else if (!strcmp(argv[i], "--latency") ||
		           !strcmp(argv[i], "-l")) {
			if (++i < argc) {
				latency = strtol(argv[i], NULL, 10);
			} else {
				ogl_error("latency option requires an argument");
				help(appname);
				return 0;
			}
		} else if (!strcmp(argv[i], "--timeout") ||
		           !strcmp(argv[i], "-t")) {
			if (++i < argc) {
				timeout = strtol(argv[i], NULL, 10);
			} else {
				ogl_error("timeout option requires an argument");
				help(appname);
				return 0;
			}
		} else if (!strcmp(argv[i], "--splash") ||
		           !strcmp(argv[i], "-s")) {
			if (++i < argc) {
				splash_path = strdup(argv[i]);
				if (!splash_path) {
					printf("out of memory\n");
					return 0;
				}
			} else {
				ogl_error("splash option requires an argument");
				help(appname);
				return 0;
			}
		} else if (!strcmp(argv[i], "--display") ||
		           !strcmp(argv[i], "-d")) {
			if (++i < argc) {
				display_id = strtol(argv[i], NULL, 10);
			} else {
				ogl_error("display option requires an argument");
				help(appname);
				return 0;
			}
#if !defined(WINDOW_SYSTEM_QNX_SCREEN)
		} else if (!strcmp(argv[i], "--mode") ||
		           !strcmp(argv[i], "-m")) {
			if (++i < argc) {
				display_mode = strtol(argv[i], NULL, 10);
			} else {
				ogl_error("mode option requires an argument");
				help(appname);
				return 0;
			}
#endif
		} else if (!strcmp(argv[i], "--no-delay") ||
		           !strcmp(argv[i], "-n")) {
			no_delay = 1;
		} else if (!strcmp(argv[i], "--retry") ||
		           !strcmp(argv[i], "-r")) {
			retry = 1;
		}
#if defined(WINDOW_SYSTEM_QNX_SCREEN)
		else if (!strcmp(argv[i], "--layer") ||
		           !strcmp(argv[i], "-y")) {
			if (++i < argc) {
				layer = strtol(argv[i], NULL, 10);
			} else {
				ogl_error("layer option requires an argument");
				help(appname);
				return 0;
			}
		}
#else
		else if (!strcmp(argv[i], "--wayland") ||
		           !strcmp(argv[i], "-w")) {
			wayland = 1;
		}
#endif
		else if (!strcmp(argv[i], "--gles-on") ||
		           !strcmp(argv[i], "-g")) {
			if (++i < argc) {
				gles_on = strtol(argv[i], NULL, 10);
			} else {
				ogl_error("gles-on option requires an argument");
				help(appname);
				return 0;
			}
		} else if (!strcmp(argv[i], "--help") ||
		           !strcmp(argv[i], "-h")) {
			help(appname);
			return 0;
		} else {
			ogl_error("Unrecognized argument: %s\n", argv[i]);
			help(appname);
			return 0;
		}
	}

	return 1;
}

static void help(const char* name)
{
	printf("%s version %s\n", name, OGL_SERVER_VERSION);
	printf("Usage: %s [options]\n", name);
	printf("options:\n"
		"--iface, -i <name>  : interface name to listen on\n"
		"                      (default: %s)\n"
		"--port, -p <port>   : port number\n"
		"                      (default: %d)\n"
		"--no-delay, n       : set TCP_NODELAY\n"
		"                      (default: %s)\n"
		"--latency, -l <num> : cross-partition stream latency)\n"
		"                      (default: %d)\n"
		"--timeout, -t <num> : cross-partition stream timeout)\n"
		"                      (default: %d)\n"
		"--splash, -s <path> : path to splash image\n"
		"                      splash screen needs GLES rendering\n"
		"                      (default: %s)\n"
		"--display, -d <num> : display index (autodetect: -1)\n"
		"                      (default :%i)\n"
		"--retry, -r         : reconnect if client disconnects\n"
		"                      (default: %s)\n"
#if defined(WINDOW_SYSTEM_QNX_SCREEN)
		"--layer, -y         : layer index\n"
		"                      (default: specified in screen config file)\n"
#else
		"--mode, -m <num>    : display mode index (current: -1)\n"
		"                      (default :%i)\n"
		"--wayland, -w       : display using a wayland window\n"
		"                      wayland needs GLES rendering\n"
		"                      (default: %s)\n"
#endif
		"--gles-on, -g <num> : GLES rendering (0: off)\n"
		"                      (default: %s)\n"
		"--help, -h          : display this message and exit\n\n",
		OGL_SERVER_IFACE ? OGL_SERVER_IFACE : "all interfaces",
		OGL_SERVER_PORT,
		OGL_SERVER_NO_DELAY ? "on" : "off",
		OGL_SERVER_LATENCY,
		OGL_SERVER_TIMEOUT,
		OGL_SERVER_SPLASH,
		OGL_SERVER_DISPLAY_ID,
		OGL_SERVER_RETRY ? "on" : "off",
#if !defined(WINDOW_SYSTEM_QNX_SCREEN)
		OGL_SERVER_DISPLAY_MODE,
		OGL_SERVER_WAYLAND ? "on" : "off",
#endif
		OGL_SERVER_GLES_ON ? "on" : "off");
}

static void *sockThread(void *param)
{
	char msg[64];
	int retval, locked = 0;

	server_sock = oglCreateSocket();
	if (server_sock < 0) {
		ogl_error("oglCreateSocket failed");
		goto out;
	}

	retval = oglSockTCPDelay(server_sock, no_delay);
	if (retval < 0) {
		ogl_error("oglSockTCPDelay failed");
		goto out;
	}

	retval = oglBindSocket(server_sock, port);
	if (retval < 0) {
		ogl_error("oglBindSocket failed");
		goto out;
	}

	retval = oglBindToDev(server_sock, iface);
	if (retval < 0) {
		ogl_error("oglBindToDev failed");
		goto out;
	}

	retval = oglListenSocket(server_sock);
	if (retval < 0) {
		ogl_error("oglListenSocket failed");
		goto out;
	}

	while (GL_TRUE) {
		locked = !pthread_mutex_lock(&mutex);
		if (!locked) {
			ogl_error("pthread_mutex_lock failed");
			goto out;
		}

		while (accept_sock >= 0) {
			if (quit)
				goto out;

			retval = pthread_cond_wait(&cond_stream, &mutex);
			if (retval) {
				ogl_error("pthread_cond_wait failed");
				goto out;
			}
		}
		do {
			if (quit)
				goto out;

			accept_sock = oglCreateAcceptSocket(server_sock);
			if (accept_sock < 0)
				ogl_debug("oglCreateAcceptSocket failed");
		} while (accept_sock < 0);
		retval = oglReceiveStringSocket(accept_sock, msg, sizeof(msg));
		if (retval > 0) {
			ogl_debug("received: %s", msg);
			if (strstr(msg, "mode?")) {
				unsigned w;
				unsigned h;
#if defined(WINDOW_SYSTEM_QNX_SCREEN)
				w = screen_mode.width;
				h = screen_mode.height;
				// Refresh rate in mHz
				uint32_t refresh = screen_mode.refresh * 1000;
#else
				uint64_t refresh;
				drmModeModeInfo *info = &crtc->mode;
				w = info->hdisplay;
				h = info->vdisplay;
				/* Calculate higher precision (mHz) refresh rate */
				refresh = (info->clock * 1000000LL / info->htotal +
					   info->vtotal / 2) / info->vtotal;

				if (info->flags & DRM_MODE_FLAG_INTERLACE)
					refresh *= 2;
				if (info->flags & DRM_MODE_FLAG_DBLSCAN)
					refresh /= 2;
				if (info->vscan > 1)
					refresh /= info->vscan;
#endif
				/* Hack since full screen doesn't work */
				hack_size(800, &w, &h);

				sprintf(msg, "%ux%u@%u", w, h, (unsigned)refresh);


				retval = oglSendStringSocket(accept_sock, msg);
				if ((uint)retval != strlen(msg))
					ogl_debug("oglSendStringSocket failed");
			}
		}
		retval = pthread_cond_signal(&cond_socket);
		if (retval) {
			ogl_error("pthread_cond_signal failed");
			goto out;
		}

		locked = !!pthread_mutex_unlock(&mutex);
		if (locked) {
			ogl_error("pthread_mutex_unlock failed");
			goto out;
		}
	}

out:
	quit = GL_TRUE;

	if (locked) {
		pthread_cond_signal(&cond_socket);
		locked = !!pthread_mutex_unlock(&mutex);
	}

	pthread_exit(param);
}

int main(int argc, char **argv)
{
	EGLConfig cfg;
	EGLint state;
	int retval, locked = 0;
	EGLNativeDisplayType nativeDisplay;
	int width = 0, height = 0;

#if defined(__INTEGRITY)
	// Wait for notification to start
	while (1) {
		Object obj;
		if (RequestResource(&obj, "__nvidia_dispinit",
		    "!systempassword") == Success) {
			Close(obj);
			break;
		}
		usleep(100000);
	}
#endif

	signal(SIGINT, SignalHandler);
	signal(SIGTERM, SignalHandler);

	splash_path = strdup(OGL_SERVER_SPLASH);
	if (!splash_path) {
		ogl_error("out of memory\n");
		return 1;
	}

	retval = ProcessOptions(argc, argv);
	if (!retval) {
		ogl_error("ProcessOptions failed");
		return retval;
	}

#if !defined(WINDOW_SYSTEM_QNX_SCREEN)
	drm_fd = drmOpen(OGL_SERVER_DRM, NULL);
	if (drm_fd < 0) {
		ogl_error("drmOpen failed");
		return 0;
	}

	crtc = oglInitCRTC(drm_fd, display_id, display_mode);
	if (!crtc) {
		ogl_error("oglInitCRTC failed");
		goto out_drm;
	}

	width = crtc->mode.hdisplay;
	height = crtc->mode.vdisplay;
#endif

	retval = pthread_create(&thread, NULL, sockThread, NULL);
	if (retval) {
		ogl_error("pthread_create failed");
		goto out_crtc;
	}

	retval = oglInit();
	if (!retval) {
		ogl_error("oglInit failed");
		goto out_pthread;
	}

#if defined(WINDOW_SYSTEM_QNX_SCREEN)
	if (qnx_screen) {
		screen_dpy = screenDisplayInit(display_id);
		if (!screen_dpy) {
			ogl_error("screen failed");
			goto out_pthread;
		}

		retval = screenDisplayGetMode(screen_dpy, &screen_mode);
		if (!retval) {
			ogl_error("screen failed");
			goto out_pthread;
		}

		width = screen_mode.width;
		height = screen_mode.height;

		// nativeDisplay should be set to default display otherwise
		// eglGetDisplay fails
		nativeDisplay = EGL_DEFAULT_DISPLAY;
	}
#else
#if defined(ENABLE_WAYLAND_SUPPORT)
	if (wayland) {
		retval = waylandDisplayInit(&wlDisplay);
		if (!retval) {
			ogl_error("waylandDisplayInit failed");
			goto out_pthread;
		}
		nativeDisplay = (EGLNativeDisplayType)wlDisplay;
	} else
#endif // defined(ENABLE_WAYLAND_SUPPORT)
	{
		nativeDisplay = (EGLNativeDisplayType)EGL_DEFAULT_DISPLAY;
	}
#endif

	display = oglCreateDisplay(nativeDisplay);
	if (display == EGL_NO_DISPLAY) {
		ogl_error("oglCreateDisplay failed");
		goto out_pthread;
	}

	cfg = oglChooseConfig(display);
	if (cfg == EGL_NO_CONFIG) {
		ogl_error("oglChooseConfig failed");
		goto out_display;
	}

	if (gles_on) {
		context = oglCreateContext(display, cfg);
		if (context == EGL_NO_CONTEXT) {
			ogl_error("oglCreateContext failed");
			goto out_display;
		}
#if defined(WINDOW_SYSTEM_QNX_SCREEN)
		if (qnx_screen) {
			surface = screenCreateSurface(screen_dpy, display, cfg, context,
					screen_mode.width, screen_mode.height, layer);
		}
#else
#if defined(ENABLE_WAYLAND_SUPPORT)
		if (wayland) {
			// Though the surface is going to a wayland window, the size of
			// the surface is still chosen to be the fullsize of a given
			// drm screen.
			surface = waylandCreateSurface(wlDisplay, cfg, display, context,
			crtc->mode.hdisplay, crtc->mode.vdisplay);
		} else
#endif // ENABLE_WAYLAND_SUPPORT
		{
			stream = oglCreateOutputStream(display, crtc->crtc_id);
			if (stream == EGL_NO_STREAM_KHR) {
				ogl_error("oglCreateOutputStream failed");
				goto out_context;
			}

			surface = oglCreateSurface(display, cfg, stream,
			crtc->mode.hdisplay, crtc->mode.vdisplay);
		}
#endif
		if (surface == EGL_NO_SURFACE) {
			ogl_error("Surface creation failed");
			goto out_stream;
		}

		retval = eglMakeCurrent(display, surface, surface, context);
		if (!retval) {
			ogl_error("eglMakeCurrent failed");
			goto out_surface;
		}

		glCullFace(GL_BACK);
		glFrontFace(GL_CCW);
		glEnable(GL_CULL_FACE);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		InitViewport(OGL_RAD(90.0f), width, height);

		splash = ModelGenerateFromFile(&camera, splash_path);
		if (!splash)
			ogl_debug("no splash screen created");
	} // if (gles_on)

	while (GL_TRUE) {
		if (gles_on) {
			glClear(GL_COLOR_BUFFER_BIT);
			DrawSplash();
		}
		locked = !pthread_mutex_lock(&mutex);
		if (!locked) {
			ogl_error("pthread_mutex_lock failed");
			goto out;
		}

		while (accept_sock < 0) {
			if (quit)
				goto out;

			retval = pthread_cond_wait(&cond_socket, &mutex);
			if (retval) {
				ogl_error("pthread_cond_wait");
				goto out;
			}
		}

		consumer = oglCreateCrossPartStream(display, EGL_STREAM_CONSUMER_NV,
						    accept_sock, latency, timeout);
		if (consumer == EGL_NO_STREAM_KHR) {
			ogl_error("oglCreateCrossPartStream failed");
			goto retry;
		}

		do {
			if (quit)
				goto out;

			retval = peglQueryStreamKHR(display, consumer,
						   EGL_STREAM_STATE_KHR, &state);
			if (!retval) {
				ogl_error("eglQueryStreamKHR failed");
				goto out;
			}

			if (state == EGL_STREAM_STATE_CREATED_KHR)
				break;
		} while (state == EGL_STREAM_STATE_INITIALIZING_NV);

		if (state != EGL_STREAM_STATE_CREATED_KHR)
			goto retry;
		if (gles_on) {
			external = ModelGenerateFromStream(&identity, display, consumer);
			if (!external) {
				ogl_error("ModelGenerateFromStream failed");
				goto out;
			}

			glDisable(GL_BLEND);
			ModelSet(external);

		}
#if !defined(WINDOW_SYSTEM_QNX_SCREEN)
        else {
			if (!oglBindCRTC(display, consumer, crtc->crtc_id)) {
				ogl_error("oglBindCRTC failed");
				goto out;
			}
		}
#endif

		streamSync = peglCreateStreamSyncNV(display, consumer,
		                                    EGL_SYNC_NEW_FRAME_NV, 0);
		if (streamSync == EGL_NO_SYNC_KHR) {
			ogl_error("eglCreateStreamSyncNV failed");
			goto out;
		}

		do {
			if (quit)
				goto out;

			if (!peglSignalSyncKHR(display, streamSync, EGL_UNSIGNALED_KHR)) {
				ogl_info("eglSignalSyncKHR failed");
				goto retry;
			}
			if (!peglQueryStreamKHR(display, consumer,
			                            EGL_STREAM_STATE_KHR, &state)) {
				ogl_info("eglQueryStreamKHR failed");
				goto retry;
			}
			if (state == EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
				if (gles_on) {
					glClear(GL_COLOR_BUFFER_BIT);
					retval = DrawConsumer();
				}
			} else if(state == EGL_STREAM_STATE_DISCONNECTED_KHR) {
				ogl_info("Stream disconnected");
				goto retry;
			} else {
				retval = peglClientWaitSyncKHR(display, streamSync,
				                                0, OGL_SERVER_STREAM_WAIT);
			}

		} while (retval);

retry:
		if (!retry || quit)
			goto out;

		DestroySyncHandle(&streamSync);
		DestroyStream(&consumer);
		CloseSock(&accept_sock);
		retval = pthread_cond_signal(&cond_stream);
		if (retval) {
			ogl_error("pthread_cond_signal failed");
			goto out;
		}
		locked = !!pthread_mutex_unlock(&mutex);
		if (locked) {
			ogl_error("pthread_mutex_unlock failed");
			goto out;
		}
	}
out:
	quit = GL_TRUE;

	if (gles_on) {
		if (external) {
			ModelDelete(external);
			external = NULL;
		}

		if (splash) {
			ModelDelete(splash);
			splash = NULL;
		}

		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}
	eglReleaseThread();

out_surface:
	if (gles_on) {
		eglDestroySurface(display, surface);
		surface = EGL_NO_SURFACE;
	}

out_stream:
	DestroySyncHandle(&streamSync);
	DestroyStream(&consumer);
	DestroyStream(&stream);

#if !defined(WINDOW_SYSTEM_QNX_SCREEN)
out_context:
	if (gles_on) {
		eglDestroyContext(display, context);
		context = EGL_NO_CONTEXT;
	}
#endif

out_display:
	eglTerminate(display);
	display = EGL_NO_DISPLAY;

out_pthread:
	if (locked) {
		pthread_cond_signal(&cond_stream);
		locked = !!pthread_mutex_unlock(&mutex);
	} else {
		pthread_kill(thread, SIGINT);
	}

	pthread_join(thread, NULL);
	CloseSock(&accept_sock);
	CloseSock(&server_sock);

out_crtc:
#if !defined(WINDOW_SYSTEM_QNX_SCREEN)
	drmModeFreeCrtc(crtc);
	crtc = NULL;

out_drm:
	drmClose(drm_fd);
	drm_fd = -1;
#endif

	return 0;
}
