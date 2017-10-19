/*
 * egl_utils.c
 *
 * Copyright (c) 2015-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "egl_utils.h"
#include "log_utils.h"

#define MAX_ATTRIB 31

EXTENSION_LIST(EXTLST_DECL)

typedef void (*extlst_fnptr_t)(void);

static struct {
    extlst_fnptr_t *fnptr;
    char const *name;
} extensionList[] = { EXTENSION_LIST(EXTLST_ENTRY) };

/* Get the required extension function addresses */
static int EGLUtilSetupExtensions(void)
{
    unsigned int i;

    for (i = 0; i < (sizeof(extensionList) / sizeof(*extensionList)); i++) {
        *extensionList[i].fnptr = eglGetProcAddress(extensionList[i].name);
        if (*extensionList[i].fnptr == NULL) {
#ifdef NVMEDIA_QNX
            if(!strcmp(extensionList[i].name, "eglGetOutputLayersEXT"))
                continue;
            if(!strcmp(extensionList[i].name, "eglStreamConsumerOutputEXT"))
                continue;
#endif
#ifdef NVMEDIA_GHSI
            if (!strcmp(extensionList[i].name, "eglGetStreamFileDescriptorKHR")) {
                continue;
            }
            if (!strcmp(extensionList[i].name, "eglCreateStreamFromFileDescriptorKHR")) {
                continue;
            }
#endif
            LOG_ERR("Couldn't get address of %s()\n", extensionList[i].name);
            return -1;
        }
    }

    return 0;
}

int EGLUtilCreateContext(EglUtilState *state)
{
    int glversion = 2;
    EGLint ctxAttrs[2*MAX_ATTRIB+1], ctxAttrIndex=0;
    EGLBoolean eglStatus;

    ctxAttrs[ctxAttrIndex++] = EGL_CONTEXT_CLIENT_VERSION;
    ctxAttrs[ctxAttrIndex++] = glversion;
    ctxAttrs[ctxAttrIndex++] = EGL_NONE;
    // Create an EGL context
    state->context =
        eglCreateContext(state->display,
                         state->config,
                         NULL,
                         ctxAttrs);
    if (!state->context) {
        LOG_ERR("EGL couldn't create context.\n");
        return 0;
    }

    // Make the context and surface current for rendering
    eglStatus = eglMakeCurrent(state->display,
                               state->surface, state->surface,
                               state->context);
    if (!eglStatus) {
        LOG_ERR("EGL couldn't make context/surface current.\n");
        return 0;
    }
    return 1;
}

EglUtilState *EGLUtilInit(EglUtilOptions *options)
{
    EGLBoolean eglStatus;
    EglUtilState *state = NULL;

#ifndef NVMEDIA_QNX
    if (EGLUtilSetupExtensions()) {
        LOG_ERR("%s: failed to setup egl extensions\n", __func__);
        return NULL;
    }
#endif

    state = malloc(sizeof(EglUtilState));
    if (!state) {
        return NULL;
    }

    memset(state, 0, sizeof(EglUtilState));
    state->display = EGL_NO_DISPLAY;
    state->surface = EGL_NO_SURFACE;

    /*Initialize with options if passed*/
    if(options) {
        if(options->windowSize[0])
            state->width = options->windowSize[0];
        if(options->windowSize[1])
            state->height = options->windowSize[1];
        state->xoffset = options->windowOffset[0];
        state->yoffset = options->windowOffset[1];
        state->displayId = options->displayId;
        state->windowId = options->windowId;
        state->vidConsumer = options->vidConsumer;
    }

    if(!WindowSystemInit(state)) { // get state->display
        free(state);
        return NULL;
    }

    // Initialize EGL
    eglStatus = eglInitialize(state->display, 0, 0);
    if (!eglStatus) {
        LOG_ERR("EGL failed to initialize.\n");
        goto fail;
    }

    if(!WindowSystemWindowInit(state)) {
        goto fail;
    }

    if(!WindowSystemEglSurfaceCreate(state)) {
        LOG_ERR("Couldn't obtain surface \n");
        goto fail;
    }

    // Query the EGL surface width and height
    eglStatus =  eglQuerySurface(state->display, state->surface,
            EGL_WIDTH,  &state->width)
                                  && eglQuerySurface(state->display, state->surface,
                                          EGL_HEIGHT, &state->height);
    if (!eglStatus) {
        LOG_ERR("EGL couldn't get window width/height.\n");
        goto fail;
    }

#ifdef NVMEDIA_QNX
    if (EGLUtilSetupExtensions()) {
        LOG_ERR("%s: failed to setup egl extensions \n", __func__);
        return NULL;
    }
#endif

    return state;

    fail:
    WindowSystemTerminate();
    free(state);
    return NULL;
}

// DestroyContext,destroying egl context.
void EGLUtilDestroyContext(EglUtilState *state)
{
    EGLBoolean eglStatus;

    if(!state) {
        LOG_ERR("Bad parameter: Error destroying EGL Context.\n");
        return;
    }
    // Clear rendering context
    // Note that we need to bind the API to unbind... yick
    if (state->display != EGL_NO_DISPLAY) {
        eglBindAPI(EGL_OPENGL_ES_API);
        eglStatus = eglMakeCurrent(state->display,
                                   EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
        if (!eglStatus)
            LOG_ERR("Error clearing current surfaces/context.\n");
    }

    // Destroy the EGL context
    if (state->context != EGL_NO_CONTEXT) {
        eglStatus = eglDestroyContext(state->display, state->context);
        if (!eglStatus)
            LOG_ERR("Error destroying EGL context.\n");
        state->context = EGL_NO_CONTEXT;
    }

    if (state->context_dGPU != EGL_NO_CONTEXT && state->display_dGPU != EGL_NO_DISPLAY) {
        eglStatus = eglDestroyContext(state->display_dGPU, state->context_dGPU);
        if (!eglStatus)
            LOG_ERR("Error destroying EGL context.\n");
        state->context_dGPU = EGL_NO_CONTEXT;
    }
}
//Deinit, destroying egl surface and native window system resources
void EGLUtilDeinit(EglUtilState *state)
{
    EGLBoolean eglStatus;
    if(!state) {
        LOG_ERR("Bad parameter: Error destroying EGL Surface.\n");
        return;
    }
    // Destroy the EGL surface
    if (state->surface != EGL_NO_SURFACE) {
        eglStatus = eglDestroySurface(state->display, state->surface);
        if (!eglStatus)
            LOG_ERR("Error destroying EGL surface.\n");
        state->surface = EGL_NO_SURFACE;
    }

    // Close the window
    WindowSystemWindowTerminate(state);

    // Terminate display access
    WindowSystemTerminate();

    // Terminate EGL
    if (state->display != EGL_NO_DISPLAY) {
        eglStatus = eglTerminate(state->display);
        if (!eglStatus)
            LOG_ERR("Error terminating EGL.\n");
        state->display = EGL_NO_DISPLAY;
    }

    // Release EGL thread
    eglStatus = eglReleaseThread();
    if (!eglStatus)
        LOG_ERR("Error releasing EGL thread.\n");

    free(state);
}
