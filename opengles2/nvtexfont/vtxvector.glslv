/*
 * vtxvector.glslf
 *
 * Copyright (c) 2007 - 2012 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 * This software is based upon texfont, with consent from Mark J. Kilgard,
 * provided under the following terms:
 *
 * Copyright (c) Mark J. Kilgard, 1997.
 *
 * This program is freely distributable without licensing fees  and is
 * provided without guarantee or warrantee expressed or  implied. This
 * program is -not- in the public domain.
 */

//
// Vertex shader for vector fonts
//

attribute vec2 vertArray;
uniform int offset;
uniform vec2 scale;
uniform vec2 screenPos;

void main()  {
    gl_Position = vec4( scale * (vertArray + vec2(offset, -100.0)) + screenPos, 1.0, 1.0);
}
