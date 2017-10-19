/*
 * simpletex_vert.glslv
 *
 * Copyright (c) 2007-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* Vertex shader for simple textured objects (background) */

// Projection*modelview matrix
uniform mat4 mvpmatrix;

// Input vertex parameters
attribute vec3 vertex;
attribute vec2 texcoord;

// Output to fragment shader
varying vec2 texcoordVar;

void main() {
    // Pass the texture coordinates through
    texcoordVar = texcoord;

    // Transform the vertex
    gl_Position = mvpmatrix * vec4(vertex,1.0);
}
