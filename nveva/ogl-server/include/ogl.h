/*
 * Copyright (c) 2015, NVIDIA Corporation.  All Rights Reserved.
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

#ifndef __OGL_H__
#define __OGL_H__

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifndef EGL_NO_CONFIG
#define EGL_NO_CONFIG	((EGLConfig)0)
#endif

#ifdef __cplusplus
	extern "C" {
#endif

#ifndef GL_GLEXT_PROTOTYPES
extern PFNGLGENVERTEXARRAYSOESPROC pglGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSOESPROC pglDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYOESPROC pglBindVertexArray;
extern PFNGLISVERTEXARRAYOESPROC pglIsVertexArray;
#endif

extern PFNEGLGETOUTPUTLAYERSEXTPROC peglGetOutputLayersEXT;
extern PFNEGLCREATESTREAMKHRPROC peglCreateStreamKHR;
extern PFNEGLDESTROYSTREAMKHRPROC peglDestroyStreamKHR;
extern PFNEGLQUERYSTREAMKHRPROC peglQueryStreamKHR;
extern PFNEGLQUERYSTREAMU64KHRPROC peglQueryStreamu64KHR;
extern PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC peglStreamConsumerGLTextureExternalKHR;
extern PFNEGLSTREAMCONSUMERACQUIREKHRPROC peglStreamConsumerAcquireKHR;
extern PFNEGLSTREAMCONSUMERRELEASEKHRPROC peglStreamConsumerReleaseKHR;
extern PFNEGLSTREAMCONSUMEROUTPUTEXTPROC peglStreamConsumerOutputEXT;
extern PFNEGLCREATESTREAMPRODUCERSURFACEKHRPROC peglCreateStreamProducerSurfaceKHR;
extern PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC peglCreatePlatformWindowSurfaceEXT;
extern PFNEGLCREATESTREAMSYNCNVPROC peglCreateStreamSyncNV;
extern PFNEGLCLIENTWAITSYNCKHRPROC peglClientWaitSyncKHR;
extern PFNEGLSIGNALSYNCKHRPROC peglSignalSyncKHR;
extern PFNEGLDESTROYSYNCKHRPROC peglDestroySyncKHR;

extern EGLSurface oglCreateSurface(EGLDisplay dpy, EGLConfig cfg,
				   EGLStreamKHR str, GLuint w, GLuint h);
extern EGLContext oglCreateContext(EGLDisplay dpy, EGLConfig cfg);
extern EGLConfig oglChooseConfig(EGLDisplay dpy);
extern EGLBoolean oglWaitStreamReady(EGLDisplay dpy, EGLStreamKHR str);
extern EGLBoolean oglBindCRTC(EGLDisplay dpy, EGLStreamKHR str,
			      GLuint crtc_id);
extern EGLStreamKHR oglCreateOutputStream(EGLDisplay dpy, int crtc_id);
extern EGLStreamKHR oglCreateCrossPartStream(EGLDisplay dpy, GLuint type, int sock,
					     int latency, int timeout);
extern EGLDisplay oglCreateDisplay(EGLNativeDisplayType disp);
extern EGLBoolean oglInit(void);

#ifdef __cplusplus
	}
#endif

#endif	/* __OGL_H__ */
