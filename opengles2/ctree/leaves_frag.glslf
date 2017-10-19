/*
 * leaves_frag.glslf
 *
 * Copyright (c) 2007-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* Fragment shader for ground and branches (lit objects with full opacity) */

precision highp float;

// Input parameters from vertex shader
varying lowp vec3 colorVar;
varying vec2 texcoordVar;

// Texture unit (Always 0, but we have to do it as a uniform)
uniform sampler2D texunit;

// Cutoff alpha for discard
const lowp float minalpha = 0.5;

void main() {

    // Load texture color
    lowp vec4 texcolor = texture2D(texunit, texcoordVar);

    // Skip if texture alpha is below cutoff
    if (texcolor.a <= minalpha) discard;

    // Multiply texture color by input color
    gl_FragColor = texcolor * vec4(colorVar,1.0);
}

