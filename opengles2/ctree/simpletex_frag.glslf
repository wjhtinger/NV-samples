/*
 * simpletex_frag.glslf
 *
 * Copyright (c) 2007-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/* Fragment shader for simple textured objects (background) */

precision highp float;

// Color to adjust texture value
uniform lowp vec4 color;

// Texture unit (Always 0, but we have to do it as a uniform)
uniform sampler2D texunit;

// Texture coordinate
varying vec2 texcoordVar;

void main() {
    gl_FragColor = color * texture2D(texunit, texcoordVar);
}

