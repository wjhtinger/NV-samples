/*
 * overlaycol_vert.glslv
 *
 * Copyright (c) 2007-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* Vertex shader for colored overlays (sliders) */

// Input vertex parameters
attribute vec2 vertex;
attribute vec3 color;

// Output to fragment shader
varying vec4 colorVar;

// Geometry is set up for a 640x480 screen
const vec2 scale = vec2(1.0/320.0, 1.0/240.0);
const vec2 shift = vec2(-1.0, -1.0);

void main() {
    // Pass through the color
    colorVar = vec4(color, 1.0);

    // Transform the vertex
    gl_Position = vec4(scale * vertex + shift, 0.0, 1.0);
}
