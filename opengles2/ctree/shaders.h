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
// Shader programs and attribute/uniform locations
//

#ifndef __SHADERS_H
#define __SHADERS_H

#include <GLES2/gl2.h>

#ifdef ANDROID
#define CTREE_PREFIX "ctree/"
#else
#define CTREE_PREFIX ""
#endif

// Ground and branch shader (lit objects with full opacity)
extern GLint prog_solids;
extern GLint uloc_solidsLights;
extern GLint uloc_solidsLightPos;
extern GLint uloc_solidsLightCol;
extern GLint uloc_solidsMvpMat;
extern GLint uloc_solidsTexUnit;
extern GLint aloc_solidsVertex;
extern GLint aloc_solidsNormal;
extern GLint aloc_solidsColor;
extern GLint aloc_solidsTexcoord;

// Leaves shader (lit objects with alphatest)
extern GLint prog_leaves;
extern GLint uloc_leavesLights;
extern GLint uloc_leavesLightPos;
extern GLint uloc_leavesLightCol;
extern GLint uloc_leavesMvpMat;
extern GLint uloc_leavesTexUnit;
extern GLint aloc_leavesVertex;
extern GLint aloc_leavesNormal;
extern GLint aloc_leavesColor;
extern GLint aloc_leavesTexcoord;

// Simple colored object shader
extern GLint prog_simplecol;
extern GLint uloc_simplecolMvpMat;
extern GLint aloc_simplecolVertex;
extern GLint aloc_simplecolColor;

// Simple textured object shader
extern GLint prog_simpletex;
extern GLint uloc_simpletexColor;
extern GLint uloc_simpletexMvpMat;
extern GLint uloc_simpletexTexUnit;
extern GLint aloc_simpletexVertex;
extern GLint aloc_simpletexTexcoord;

// Colored overlay shader
extern GLint prog_overlaycol;
extern GLint aloc_overlaycolVertex;
extern GLint aloc_overlaycolColor;

// Textured overlay shader
extern GLint prog_overlaytex;
extern GLint uloc_overlaytexTexUnit;
extern GLint aloc_overlaytexVertex;
extern GLint aloc_overlaytexTexcoord;

// Load/free shaders
extern int  LoadShaders(void);
extern void FreeShaders(void);

#endif // __SHADERS_H
