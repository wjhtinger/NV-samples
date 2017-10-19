/*
 * colraster.glslf
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
// Fragment shader for rasterized fonts
//

#pragma profilepragma blendoperation( gl_FragColor, GL_FUNC_ADD, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA )

precision mediump float;
uniform sampler2D fontTex;
uniform vec3 color;

varying vec2 texCoord;

void main()  {
    gl_FragColor = vec4( color, texture2D(fontTex, texCoord).a );
}

