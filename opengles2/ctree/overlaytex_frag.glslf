/*
 * overlaytex_frag.glslf
 *
 * Copyright (c) 2007-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#pragma profilepragma blendoperation( gl_FragColor, GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA )

precision highp float;

/* Fragment shader for textured overlays (logo and slider text) */

// Texture unit (Always 0, but we have to do it as a uniform)
uniform sampler2D texunit;

// Texture coordinte to use
varying vec2 texcoordVar;

void main() {
    gl_FragColor = texture2D(texunit, texcoordVar);
}

