/*
 * cube_vert.glslv
 *
 * Copyright (c) 2009-2013, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Vertex shader for rotating textured cube

uniform mat4   cameramat; // Camera matrix
uniform mat4   objectmat; // Object matrix

attribute vec3 vtxpos;    // Vertex position
attribute vec2 vtxtex;    // Input texture coordinates

varying vec2   texcoord;  // Output texture coordinates


/*
 * Function
 */
void main() {

    // Pass through texture coordinates
    texcoord = vtxtex;

    // Transform vertex position
    gl_Position= cameramat * (objectmat*vec4(vtxpos,1));
}
