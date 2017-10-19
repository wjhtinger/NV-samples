/*
 * lighting_vert.glslv
 *
 * Copyright (c) 2007-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* Vertex shader for ground, branches, and leaves (all lit objects) */

//NOTE: any changes to NUM_LIGHTS must also be made to screen.c
#define NUM_LIGHTS 8

// Lighting parameters
uniform int  lights;                // Number of active lights
uniform vec3 lightpos[NUM_LIGHTS];  // Worldspace position of light
uniform vec3 lightcol[NUM_LIGHTS];  // Color of light
const float  atten1 = 1.0;          // Linear attenuation weight
const float  atten2 = 0.1;          // Quadratic attenuation weight

// Projection*modelview matrix
uniform mat4 mvpmatrix;

// Input vertex parameters
attribute vec3 vertex;
attribute vec3 normal;
attribute vec3 color;
attribute vec2 texcoord;

// Output parameters for fragment shader
varying vec3 colorVar;
varying vec2 texcoordVar;

void main() {

    vec3  totLight;
    vec3  normaldir;
    vec3  lightvec;
    float lightdist;
    vec3  lightdir;
    float ldotn;
    float attenuation;
    int   i;

    // Initialize lighting contribution
    totLight = vec3(0.0, 0.0, 0.0);

    // Normalize normal vector
    normaldir = normalize(normal);

    // Add contribution of each light
    for (i=0; i<lights; i++) {
        // Compute direction/distance to light
        lightvec  = lightpos[i] - vertex;
        lightdist = length(lightvec);
        lightdir  = lightvec / lightdist;

        // Compute dot product of light and normal vectors
        ldotn = clamp(dot(lightdir, normaldir), 0.0, 1.0);

        // Compute attenuation factor
        attenuation = (atten1 + atten2 * lightdist) * lightdist;

        // Add contribution of this light
        totLight += (ldotn / attenuation) * lightcol[i];
    }

    // Output material * total light
    colorVar = totLight * color;

    // Pass through the texture coordinate
    texcoordVar = texcoord;

    // Transform the vertex
    gl_Position = mvpmatrix * vec4(vertex,1.0);
}
