/*
 * bubble_frag.glslf
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
// Fragment shader to render bubble
//

precision highp float;

// Texture unit
uniform samplerCube texunit;

// Texture coordinate input
varying vec3 texcoord;

void main()
{
    gl_FragColor = textureCube(texunit, texcoord);
}

