/*
 * mouse_vert.glslv
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
// Vertex shader to render mouse crosshair
//

// Input vertex position
attribute vec2 vertex;

// Inverse size of window (1/w,1/h)
uniform vec2 window;
// Location (in pixels) of center of mouse cursor
uniform vec2 center;

void main() {
    gl_Position = vec4(2.0*(center+vertex)*window-1.0,0.0,1.0);
}
