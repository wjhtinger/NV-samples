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

#include <stdlib.h>
#include <sched.h>
#include <string.h>

#include "ogl.h"
#include "ogl-debug.h"

PFNEGLGETOUTPUTLAYERSEXTPROC peglGetOutputLayersEXT;
PFNEGLCREATESTREAMKHRPROC peglCreateStreamKHR;
PFNEGLDESTROYSTREAMKHRPROC peglDestroyStreamKHR;
PFNEGLQUERYSTREAMKHRPROC peglQueryStreamKHR;
PFNEGLQUERYSTREAMU64KHRPROC peglQueryStreamu64KHR;
PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC peglStreamConsumerGLTextureExternalKHR;
PFNEGLSTREAMCONSUMERACQUIREKHRPROC peglStreamConsumerAcquireKHR;
PFNEGLSTREAMCONSUMERRELEASEKHRPROC peglStreamConsumerReleaseKHR;
PFNEGLSTREAMCONSUMEROUTPUTEXTPROC peglStreamConsumerOutputEXT;
PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC peglCreateStreamProducerSurfaceKHR;
PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC peglCreatePlatformWindowSurfaceEXT;
PFNEGLCREATESTREAMSYNCNVPROC peglCreateStreamSyncNV;
PFNEGLCLIENTWAITSYNCKHRPROC peglClientWaitSyncKHR;
PFNEGLSIGNALSYNCKHRPROC peglSignalSyncKHR;
PFNEGLDESTROYSYNCKHRPROC peglDestroySyncKHR;

#ifndef GL_GLEXT_PROTOTYPES
PFNGLGENVERTEXARRAYSOESPROC pglGenVertexArrays;
PFNGLDELETEVERTEXARRAYSOESPROC pglDeleteVertexArrays;
PFNGLBINDVERTEXARRAYOESPROC pglBindVertexArray;
PFNGLISVERTEXARRAYOESPROC pglIsVertexArray;
#endif

GLint oglStreamReady(EGLDisplay dpy, EGLStreamKHR str, EGLint *state);

EGLSurface oglCreateSurface(EGLDisplay dpy, EGLConfig cfg, EGLStreamKHR str,
			    GLuint w, GLuint h)
{
	EGLint srf_attr[] = {
		EGL_WIDTH, w,
		EGL_HEIGHT, h,
		EGL_NONE,
	};

	return peglCreateStreamProducerSurfaceKHR(dpy, cfg, str, srf_attr);
}

EGLContext oglCreateContext(EGLDisplay dpy, EGLConfig cfg)
{
	EGLint ai32ContextAttribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};

	return eglCreateContext(dpy, cfg,
				EGL_NO_CONTEXT, ai32ContextAttribs);
}

EGLConfig oglChooseConfig(EGLDisplay dpy)
{
	EGLConfig *configs;
	EGLConfig cfg = EGL_NO_CONFIG;
	EGLint config_count, n;
#ifdef BS24
	EGLint i, size;
#endif
	EGLBoolean retval;

	EGLint cfg_attribs[] = {
		EGL_SURFACE_TYPE, EGL_STREAM_BIT_KHR,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
#ifdef BS24
		EGL_BUFFER_SIZE, 24, /* RGB */
#else
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_SAMPLES, 0,
#endif
		EGL_NONE,
	};

	retval = eglGetConfigs(dpy, NULL, 0, &config_count);
	if (!retval || config_count < 1) {
		ogl_debug("condition failed");
		goto out;
	}

	configs = calloc(config_count, sizeof *configs);
	if (!configs) {
		ogl_debug("condition failed");
		goto out;
	}

	retval = eglChooseConfig(dpy, cfg_attribs, configs, config_count, &n);
	if ((retval != EGL_TRUE) || (n < 1)) {
		ogl_debug("condition failed");
		goto out_cfg;
	}

	ogl_debug("configs: %i", n);
#ifdef BS24
	for (i = 0; i < n; i++) {
		retval = eglGetConfigAttrib(dpy,
					    configs[i], EGL_BUFFER_SIZE, &size);
		if (size == 32) {
			cfg = configs[i];
			break;
		}
	}
#else
	cfg = configs[0];
#endif

out_cfg:
	free(configs);
out:
	return cfg;
}

GLint oglStreamReady(EGLDisplay dpy, EGLStreamKHR str, EGLint *state)
{
	EGLBoolean retval;
	EGLint val, final_state;

	retval = peglQueryStreamKHR(dpy, str, EGL_STREAM_ENDPOINT_NV, &val);
	if (!retval) {
		ogl_debug("condition failed");
		return -1;
	}

	switch (val) {
	case EGL_STREAM_PRODUCER_NV:
	case EGL_STREAM_LOCAL_NV:
		final_state = EGL_STREAM_STATE_CONNECTING_KHR;
		break;

	case EGL_STREAM_CONSUMER_NV:
		final_state = EGL_STREAM_STATE_CREATED_KHR;
		break;
	default:
		ogl_debug("condition failed");
		return -1;
	}

	if (!state)
		state = &val;

	retval = peglQueryStreamKHR(dpy, str, EGL_STREAM_STATE_KHR,
				   state);
	if (!retval) {
		ogl_error("condition failed");
		return -1;
	}
	ogl_debug("state %s:%x",
		  final_state == EGL_STREAM_STATE_CREATED_KHR ?
		  "consumer" : "producer", *state);

	if (*state == final_state)
		return 0;

	if (*state != EGL_STREAM_STATE_INITIALIZING_NV &&
	    *state != EGL_STREAM_STATE_CREATED_KHR) {
		ogl_debug("condition failed");
		return -1;
	}

	return 1;
}

EGLBoolean oglBindCRTC(EGLDisplay dpy, EGLStreamKHR str, GLuint crtc_id)
{
	EGLOutputLayerEXT egl_lyr;
	EGLBoolean retval;
	EGLint n;
	EGLAttribKHR layer_attr[] = {
		EGL_DRM_CRTC_EXT, (EGLAttribKHR)crtc_id,
		EGL_NONE,
	};

	retval = peglGetOutputLayersEXT(dpy, layer_attr, &egl_lyr, 1, &n);
	if (!retval || !n) {
		ogl_debug("condition failed");
		return GL_FALSE;
	}

	retval = peglStreamConsumerOutputEXT(dpy, str, egl_lyr);
	if (!retval) {
		ogl_debug("condition failed");
		return GL_FALSE;
	}

	return EGL_TRUE;
}

EGLStreamKHR oglCreateOutputStream(EGLDisplay dpy, int crtc_id)
{
	EGLStreamKHR str;
	EGLBoolean retval;

	str = peglCreateStreamKHR(dpy, NULL);
	if (str == EGL_NO_STREAM_KHR) {
		ogl_debug("condition error");
		return EGL_NO_STREAM_KHR;
	}

	retval = oglBindCRTC(dpy, str, crtc_id);
	if (!retval) {
		ogl_debug("condition error");
		goto out_str;
	}

	return str;

out_str:
	peglDestroyStreamKHR(dpy, str);
	return EGL_NO_STREAM_KHR;
}

EGLStreamKHR oglCreateCrossPartStream(EGLDisplay dpy, GLuint type, int sock,
				      int latency, int timeout)
{
	EGLint str_attr[] = {

		EGL_STREAM_ENDPOINT_NV, type,
		EGL_SOCKET_HANDLE_NV, sock,
		EGL_CONSUMER_LATENCY_USEC_KHR, latency,
		EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, timeout,
		EGL_STREAM_TYPE_NV, EGL_STREAM_CROSS_PARTITION_NV,
		EGL_SOCKET_TYPE_NV, EGL_SOCKET_TYPE_INET_NV,
		EGL_STREAM_PROTOCOL_NV, EGL_STREAM_PROTOCOL_SOCKET_NV,
		EGL_NONE,
	};

	return peglCreateStreamKHR(dpy, str_attr);
}

EGLDisplay oglCreateDisplay(EGLNativeDisplayType disp)
{
	EGLDisplay dpy;
	EGLint major, minor;
	EGLBoolean retval;

	dpy = eglGetDisplay(disp);
	if (dpy == EGL_NO_DISPLAY) {
		ogl_debug("condition failed");
		return EGL_NO_DISPLAY;
	}

	retval = eglInitialize(dpy, &major, &minor);
	if (retval != EGL_TRUE) {
		ogl_debug("condition failed");
		goto out_dpy;
	}

	retval = eglBindAPI(EGL_OPENGL_ES_API);
	if (retval != EGL_TRUE) {
		ogl_debug("condition failed");
		goto out_dpy;
	}

	ogl_debug("EGL: %u.%u", major, minor);
	return dpy;

out_dpy:
	eglTerminate(dpy);
	return EGL_NO_DISPLAY;
}


#if defined(ENABLE_WAYLAND_SUPPORT)
static int checkExtension(const char *exts, const char *ext)
{
    int extLen = (int)strlen(ext);
    const char *end = exts + strlen(exts);

    while (exts < end) {
        while (*exts == ' ') {
            exts++;
        }
        int n = strcspn(exts, " ");
        if ((extLen == n) && (strncmp(ext, exts, n) == 0)) {
            return 1;
        }
        exts += n;
    }
    return 0;
}
#endif

EGLBoolean oglInit(void)
{
	const char *eglDisplayExtensions;

	if (!(eglDisplayExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)))
		printf("Querying client extensions failed.");

#if defined(ENABLE_WAYLAND_SUPPORT)
	if (!checkExtension(eglDisplayExtensions, "EGL_EXT_platform_wayland")) {
		printf("EGL_EXT_platform_wayland not found.");
	}
#endif

#ifndef GL_GLEXT_PROTOTYPES
	pglGenVertexArrays = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArraysOES");
	if (!pglGenVertexArrays)
		return GL_FALSE;

	pglDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSOESPROC)eglGetProcAddress("glDeleteVertexArraysOES");
	if (!pglDeleteVertexArrays)
		return GL_FALSE;

	pglBindVertexArray = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArrayOES");
	if (!pglBindVertexArray)
		return GL_FALSE;

	pglIsVertexArray = (PFNGLISVERTEXARRAYOESPROC)eglGetProcAddress("glIsVertexArrayOES");
	if (!pglIsVertexArray)
		return GL_FALSE;
#endif

#if !defined(WINDOW_SYSTEM_QNX_SCREEN)
	peglGetOutputLayersEXT = (PFNEGLGETOUTPUTLAYERSEXTPROC)eglGetProcAddress("eglGetOutputLayersEXT");
	if (!peglGetOutputLayersEXT) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}
#endif

	peglCreateStreamKHR = (PFNEGLCREATESTREAMKHRPROC)eglGetProcAddress("eglCreateStreamKHR");
	if (!peglCreateStreamKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglDestroyStreamKHR = (PFNEGLDESTROYSTREAMKHRPROC)eglGetProcAddress("eglDestroyStreamKHR");
	if (!peglDestroyStreamKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglQueryStreamKHR = (PFNEGLQUERYSTREAMKHRPROC)eglGetProcAddress("eglQueryStreamKHR");
	if (!peglQueryStreamKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglQueryStreamu64KHR = (PFNEGLQUERYSTREAMU64KHRPROC)eglGetProcAddress("eglQueryStreamu64KHR");
	if (!peglQueryStreamu64KHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglStreamConsumerGLTextureExternalKHR = (PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC)eglGetProcAddress("eglStreamConsumerGLTextureExternalKHR");
	if (!peglStreamConsumerGLTextureExternalKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglStreamConsumerAcquireKHR = (PFNEGLSTREAMCONSUMERACQUIREKHRPROC)eglGetProcAddress("eglStreamConsumerAcquireKHR");
	if (!peglStreamConsumerAcquireKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglStreamConsumerReleaseKHR = (PFNEGLSTREAMCONSUMERRELEASEKHRPROC)eglGetProcAddress("eglStreamConsumerReleaseKHR");
	if (!peglStreamConsumerReleaseKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

#if !defined(WINDOW_SYSTEM_QNX_SCREEN)
	peglStreamConsumerOutputEXT = (PFNEGLSTREAMCONSUMEROUTPUTEXTPROC)eglGetProcAddress("eglStreamConsumerOutputEXT");
	if (!peglStreamConsumerOutputEXT) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}
#endif

	peglCreateStreamProducerSurfaceKHR = (PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC)eglGetProcAddress("eglCreateStreamProducerSurfaceKHR");
	if (!peglCreateStreamProducerSurfaceKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglCreatePlatformWindowSurfaceEXT = (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
	if (!peglCreatePlatformWindowSurfaceEXT) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglCreateStreamSyncNV = (PFNEGLCREATESTREAMSYNCNVPROC)eglGetProcAddress("eglCreateStreamSyncNV");
	if (!peglCreateStreamSyncNV) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");
	if (!peglClientWaitSyncKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglSignalSyncKHR = (PFNEGLSIGNALSYNCKHRPROC)eglGetProcAddress("eglSignalSyncKHR");
	if (!peglSignalSyncKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}

	peglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
	if (!peglDestroySyncKHR) {
		ogl_debug("condition failed");
		return EGL_FALSE;
	}
	return EGL_TRUE;
}
