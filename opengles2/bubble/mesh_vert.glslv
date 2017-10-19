/*
 * mesh_vert.glslv
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
// Vertex shader to render wireframe/points
//

// Input vertex position
attribute vec3 vertex;

// Transformation matrices
uniform mat4 modelview;
uniform mat4 projection;

void main() {
    gl_PointSize = 1.0;
    gl_Position = projection * (modelview * vec4(vertex,1.0));
}
