/*
 * nvtexfont2.c
 *
 * Copyright (c) 2003 - 2012 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 * This software is based upon texfont, with consent from Mark J. Kilgard,
 * provided under the following terms:
 *
 * Copyright (c) Mark J. Kilgard, 1997.
 *
 * This program is freely distributable without licensing fees  and is
 * provided without guarantee or warrantee expressed or  implied. This
 * program is -not- in the public domain.
 */

#include "GLES2/gl2.h"
#include "GLES2/gl2ext.h"

#include "nvgldemo.h"
#include "nvtexfont.h"
#include "nvtexfont-priv.h"

// Depending on compile options, we either build in the shader sources or
//   binaries or load them from external data files at runtime.
//   (Variables are initialized to the file contents or name, respectively).
#ifdef USE_EXTERN_SHADERS
static const char rasterVertShader[] = { VERTFILE(vtxraster) };
static const char rasterFragShader[] = { FRAGFILE(colraster) };
static const char vectorVertShader[] = { VERTFILE(vtxvector) };
static const char vectorFragShader[] = { FRAGFILE(colvector) };
#else
static const char rasterVertShader[] = {
#   include VERTFILE(vtxraster)
};
static const char rasterFragShader[] = {
#   include FRAGFILE(colraster)
};
static const char vectorVertShader[] = {
#   include VERTFILE(vtxvector)
};
static const char vectorFragShader[] = {
#   include FRAGFILE(colvector)
};
#endif

GLuint rasterProg = 0, vectorProg = 0;
char *lastError;

/* General Calls */

void
deinitShaders(void)
{
    if (vectorProg) {
        glDeleteProgram(vectorProg);
        vectorProg = 0;
    }

    if (rasterProg) {
        glDeleteProgram(rasterProg);
        rasterProg = 0;
    }
}

void
initShaders(void)
{
    // Set up Raser shader program
    rasterProg = LOADSHADER(rasterVertShader, rasterFragShader,
                            GL_FALSE, GL_FALSE);

    glBindAttribLocation(rasterProg, VERT_ARRAY, "vertArray");
    glBindAttribLocation(rasterProg, TEX_ARRAY, "texArray");

    glLinkProgram(rasterProg);

    // Set up Vector shader program
    vectorProg = LOADSHADER(vectorVertShader, vectorFragShader,
                            GL_FALSE, GL_FALSE);

    glBindAttribLocation(vectorProg, VERT_ARRAY, "vertArray");

    glLinkProgram(vectorProg);
}

char*
nvtexfontErrorString(void)
{
    return lastError;
}

NVTexfontContext
nvtexfontAllocContext(void)
{
    nvtexfontState *state = MALLOC(sizeof(nvtexfontState));

    state->r = 1.0; state->g = 0.0; state->b = 0.0;
    state->scaleX = 1.0;
    state->scaleY = 1.0;
    state->x = 0.0; state->y = 0.0;

    return (NVTexfontContext)state;
}

void
nvtexfontFreeContext(
    NVTexfontContext tfc)
{
    FREE((nvtexfontState*)tfc);
}

void
nvtexfontSetContextPos(
    NVTexfontContext tfc,
    float            x,
    float            y)
{
    ((nvtexfontState*)tfc)->x = x;
    ((nvtexfontState*)tfc)->y = y;
}

void
nvtexfontSetContextScale(
    NVTexfontContext tfc,
    float            scaleX,
    float            scaleY)
{
    ((nvtexfontState*)tfc)->scaleX = scaleX;
    ((nvtexfontState*)tfc)->scaleY = scaleY;
}

void
nvtexfontSetContextColor(
    NVTexfontContext tfc,
    float            r,
    float            g,
    float            b)
{
    ((nvtexfontState*)tfc)->r = r;
    ((nvtexfontState*)tfc)->g = g;
    ((nvtexfontState*)tfc)->b = b;
}
