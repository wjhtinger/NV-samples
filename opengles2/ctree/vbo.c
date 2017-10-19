/*
 * vbo.c
 *
 * Copyright (c) 2003-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// VBO functions
//

#include "nvgldemo.h"
#include "vbo.h"

static long vboptr = 0;
static unsigned int vbosize = 0;
int vboInitialized = 0;
int useVBO = 0;

GLboolean
VBO_init(void)
{
    vboInitialized = 1;
    useVBO = 1;
    return GL_TRUE;
}

void
VBO_deinit(void)
{
    GLuint bufs[] = { VBO_NAME };

    if (vboInitialized) {
        glDeleteBuffers(sizeof bufs / sizeof *bufs, bufs);
    }

    vbosize = vboptr = 0;
}

GLboolean
VBO_setup(
    int size)
{
    int res;

    // clear out prior errors
    while (glGetError() != GL_NO_ERROR);

    VBO_deinit();
    glBindBuffer(GL_ARRAY_BUFFER, VBO_NAME);
    glBufferData(GL_ARRAY_BUFFER, size, NULL, GL_DYNAMIC_DRAW);
    if ((res = glGetError()) == GL_NO_ERROR) {
        vbosize = size;
        return GL_TRUE;
    }
    else {
        do {
            // Print out the error
            NvGlDemoLog("Error: %x\n", res);
        } while ((res = glGetError()) != GL_NO_ERROR);
        return GL_FALSE;
    }
}

unsigned long
VBO_alloc(
    int size)
{
    unsigned long ret = 0;
    size = VBO_align(size);
    if (((unsigned int) size) > vbosize) {
        NvGlDemoLog("VBO out of space\n");
    } else {
        ret = vboptr;
        vboptr += size;
        vbosize -= size;
    }
    return ret;
}
