/*
 * cube_frag.glslv
 *
 * Copyright (c) 2009-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Fragment shader for rotating textured cube

precision mediump float;    // Default precision
uniform sampler2D texunit;  // Texture unit

varying vec2      texcoord; // Texture coordinate

void main() {

    // If outside the texture, fill in with NVIDIA green
    if ((abs(texcoord.x) > 1.0) || (abs(texcoord.y) > 1.0))
        gl_FragColor = vec4(0.46, 0.73, 0.00, 1.0);

    // Otherwise map from [-1,+1] to [0,1] and get texture value
    else
        gl_FragColor = texture2D(texunit, 0.5*(texcoord+vec2(1,1)));
}
