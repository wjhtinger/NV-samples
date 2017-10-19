/*
 * simplecol_vert.glslv
 *
 * Copyright (c) 2007-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* Vertex shader for simple colored objects (flies and part of sky) */

// Projection*modelview matrix
uniform mat4 mvpmatrix;

// Input vertex parameters
attribute vec3 vertex;
attribute vec4 color;

// Output to fragment shader
varying vec4 colorVar;

void main() {
    // Pass through the color
    colorVar = color;

    // Transform the vertex
    gl_Position = mvpmatrix * vec4(vertex,1.0);
}
