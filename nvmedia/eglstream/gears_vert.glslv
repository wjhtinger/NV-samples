/*
 * gears_vert.glslv
 *
 * Copyright (c) 2007-2014, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Constant uniforms
uniform vec3 light_dir;  // Light 0 direction
uniform mat4 proj_mat;   // Projection matrix.

// Per-frame/per-object uniforms
uniform mat4 mview_mat;  // Model-view matrix
uniform vec3 material;   // Ambient and diffuse material

// Per-vertex attributes
attribute vec3 pos_attr;
attribute vec3 nrm_attr;

// Output vertex color
varying vec3 col_var;

void main()
{
    // Transformed position is projection * modelview * pos
    gl_Position = proj_mat * mview_mat * vec4(pos_attr, 1.0);

    // We know modelview matrix has no scaling, so don't need a separate
    //   inverse-transpose for transforming normals
    vec3 normal = vec3(normalize(mview_mat * vec4(nrm_attr, 0.0)));

    // Compute dot product of light and normal vectors
    float ldotn = max(dot(normal, light_dir), 0.0);

    // Compute affect of lights and clamp
    //   Global ambient is default GL value of 0.2
    //   Light0 ambient is default GL value of 0.0
    //   Light0 diffuse is default GL value of 1.0
    col_var = min((ldotn+0.2) * material, 1.0);
}
