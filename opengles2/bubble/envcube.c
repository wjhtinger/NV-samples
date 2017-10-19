/*
 * envcube.c
 *
 * Copyright (c) 2003-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// Bubble environment cube setup and rendering
//

#include "nvgldemo.h"
#include "envcube.h"
#include "shaders.h"

#include "back_img.h"
#include "bottom_img.h"
#include "front_img.h"
#include "left_img.h"
#include "right_img.h"
#include "top_img.h"

#define MAXSIZE 512

//Texture files for each face
static unsigned char *textures[] = {
    right_img,
    left_img,
    top_img,
    bottom_img,
    back_img,
    front_img
};

// Cube vertices
static float cube_vertices[3*8] = {
    -1.0f,  1.0f, -1.0f, // l t f
    -1.0f, -1.0f, -1.0f, // l b f
     1.0f, -1.0f, -1.0f, // r b f
     1.0f,  1.0f, -1.0f, // r t f
    -1.0f,  1.0f,  1.0f, // l t b
    -1.0f, -1.0f,  1.0f, // l b b
     1.0f, -1.0f,  1.0f, // r b b
     1.0f,  1.0f,  1.0f  // r t b
};

// Cube vertex index list
static unsigned char cube_tristrip[] =
    { 4, 7, 5, 6, 2, 7, 3, 4, 0, 5, 1, 2, 0, 3 };

// Create environment cube resources
EnvCube*
EnvCube_build(void)
{
    EnvCube* e = MALLOC(sizeof(EnvCube));
    if (e == NULL) return NULL;
    MEMSET(e, 0, sizeof(EnvCube));
    e->cubeTexture = NvGlDemoLoadTgaFromBuffer(GL_TEXTURE_CUBE_MAP, 6, textures);

    return e;
}

// Draw surrounding cube
void
EnvCube_draw(
    EnvCube* e)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, e->cubeTexture);

    glEnableVertexAttribArray(aloc_cubeVertex);

    glVertexAttribPointer(aloc_cubeVertex,
                          3, GL_FLOAT, GL_FALSE, 0, cube_vertices);
    glDrawElements(GL_TRIANGLE_STRIP,
                   (sizeof(cube_tristrip) / sizeof(*cube_tristrip)),
                   GL_UNSIGNED_BYTE, cube_tristrip);

    glDisableVertexAttribArray(aloc_cubeVertex);
}

// Retrieve cube map texture
unsigned int
EnvCube_getCube(
    EnvCube* e)
{
    return e->cubeTexture;
}

// Free environment cube resources
void
EnvCube_destroy(
    EnvCube* e)
{
    FREE(e);
}
