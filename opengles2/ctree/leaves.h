/*
 * leaves.h
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
// Leaf functions
//

#ifndef __LEAVES_H
#define __LEAVES_H

#include <GLES2/gl2.h>
#include "vector.h"

// Initialization and clean-up
void Leaves_initialize(GLuint front, GLuint back, float radius);
void Leaves_deinitialize(void);
void Leaves_clear(void);
void Leaves_setRadius(float r);
void Leaves_add(float4x4 m);

// Query
int Leaves_polyCount(void);
int Leaves_leafCount(void);
int Leaves_sizeVBO(void);

// Rendering
void Leaves_buildVBO(void);
void Leaves_draw(int use_VBO);

#endif // __LEAVES_H
