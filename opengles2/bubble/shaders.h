/*
 * shaders.h
 *
 * Copyright (c) 2003-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// Bubble demo shader access
//

#ifndef __SHADERS_H
#define __SHADERS_H

#include <GLES2/gl2.h>

#ifdef ANDROID
#define BUBBLE_PREFIX "bubble/"
#else
#define BUBBLE_PREFIX ""
#endif

// Reflective bubble shader
extern GLint prog_bubble;
extern GLint uloc_bubbleViewMat;
extern GLint uloc_bubbleNormMat;
extern GLint uloc_bubbleProjMat;
extern GLint uloc_bubbleTexUnit;
extern GLint aloc_bubbleVertex;
extern GLint aloc_bubbleNormal;

// Wireframe/point mesh shader
extern GLint prog_mesh;
extern GLint uloc_meshViewMat;
extern GLint uloc_meshProjMat;
extern GLint aloc_meshVertex;

// Env cube shader
extern GLint prog_cube;
extern GLint uloc_cubeProjMat;
extern GLint uloc_cubeTexUnit;
extern GLint aloc_cubeVertex;

// Crosshair shader
extern GLint prog_mouse;
extern GLint uloc_mouseWindow;
extern GLint uloc_mouseCenter;
extern GLint aloc_mouseVertex;

// Function to load all the shaders
extern int
LoadShaders(void);

extern void
FreeShaders(void);

#endif // __SHADERS_H
