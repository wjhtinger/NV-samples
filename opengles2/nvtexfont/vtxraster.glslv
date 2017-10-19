/*
 * vtxraster.glslf
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
// Vertex shader for rasterized fonts
//

attribute vec2 vertArray;
attribute vec2 texArray;

uniform float scaleX, scaleY;
uniform float screenX, screenY;
uniform int offset;

varying vec2 texCoord;

void main()  {
    //gl_Position = vec4( scale * (vertArray + vec2(offset, 0.0)) + vec2(screenX, screenY), 1.0, 1.0);
    gl_Position = vec4( screenX + scaleX * (vertArray.x + float(offset)),
                        screenY + scaleY * (vertArray.y),
                        1.0, 1.0);
    texCoord = texArray;
    //gl_Position = vec4(attrObjCoord, 0.0, 1.0);
}
