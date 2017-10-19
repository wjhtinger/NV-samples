/*
 * bubble_vert.glslv
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
// Vertex shader to render bubble
//

// Vertex coordinate and normal
attribute vec3 vertex;
attribute vec3 normal;

// Transformation matrices
uniform mat4 projection;
uniform mat4 modelview;
uniform mat4 imodelview;

// Output to fragment shader
varying vec3 texcoord;

void main() {
    vec4 xfmvertex = modelview * vec4(vertex,1.0);
    vec3 xfmnormal = normalize(vec3(imodelview * vec4(normal,0.0)));
    texcoord = reflect(vec3(xfmvertex), xfmnormal);
    gl_Position = projection * xfmvertex;
}
