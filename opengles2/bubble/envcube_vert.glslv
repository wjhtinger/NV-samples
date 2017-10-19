/*
 * envcube_vert.glslv
 *
 * Copyright (c) 2007-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// Vertex shader to render environment cube
//

// Input vertex position
attribute vec3 vertex;

// Transformation matrix (Modelview just scales so we hardcode it)
uniform mat4 projection;

// Output to fragment shader
varying vec3 texcoord;

void main() {
    texcoord    = normalize(vertex);
    gl_Position = projection * (vec4(20.0*vertex,1.0));
}
