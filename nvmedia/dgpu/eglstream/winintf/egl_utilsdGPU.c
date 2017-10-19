/*
 * egl_utilsdGPU.c
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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

#define MAX_ATTRIB (31)

int EGLUtilCreateContext_dGPU(EglUtilState *state)
{
    int glversion = 2;
    EGLint ctxAttrs[2*MAX_ATTRIB+1], ctxAttrIndex=0;
    EGLBoolean eglStatus;

    EGLint configAttrs[] = {
        EGL_SURFACE_TYPE, EGL_STREAM_BIT_KHR,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_ALPHA_SIZE, 1,
        EGL_NONE
    };

    EGLint ncfg = 0;
    EGLConfig cfg;
    if (!eglChooseConfig(state->display_dGPU, configAttrs, &cfg, 1, &ncfg))
        LOG_ERR("Could not find EGL config for dGPU in %s\n", __FUNCTION__);
    if (ncfg == 0)
        LOG_ERR("No EGL config found for dGPU in %s\n", __FUNCTION__);


    ctxAttrs[ctxAttrIndex++] = EGL_CONTEXT_CLIENT_VERSION;
    ctxAttrs[ctxAttrIndex++] = glversion;
    ctxAttrs[ctxAttrIndex++] = EGL_NONE;
    // Create an EGL context
    state->context_dGPU =
        eglCreateContext(state->display_dGPU,
                         cfg,
                         NULL,
                         ctxAttrs);

    if (!state->context_dGPU) {
        LOG_ERR("EGL couldn't create context for dGPU.\n");
        LOG_ERR("eglerror is %x \n", eglGetError());
        return 0;
    }

    // Make the context and surface current for rendering
    eglStatus = eglMakeCurrent(state->display_dGPU,
                               EGL_NO_SURFACE, EGL_NO_SURFACE,
                               state->context_dGPU);
    if (!eglStatus) {
        LOG_ERR("EGL couldn't make context/surface current.\n");
        return 0;
    }
    return 1;
}

int EGLUtilInit_dGPU(EglUtilState *state)
{
        EGLBoolean eglStatus;

    if(!state){
        LOG_ERR("EglUtilState is NULL\n");
        return 1;
    }
    if(!WindowSystemInit_dGPU(state)) { // get state->display
        LOG_ERR("WindowSystemInit_dGPU Failed\n");
        return 1;
    }
    // Initialize EGL
    LOG_INFO("dGPU display %d.\n", state->display_dGPU);
    eglStatus = eglInitialize(state->display_dGPU, 0, 0);
    if (!eglStatus) {
        LOG_ERR("eglInitialize failed for dGPU display.\n");
        goto fail;
    }

    return 0;

fail:
    WindowSystemTerminate_dGPU();
    return 1;
}
