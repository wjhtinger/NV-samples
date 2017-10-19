/*
 * branches.h
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
// Branch polygon setup/rendering
//

#ifndef __BRANCHES_H
#define __BRANCHES_H

#include <GLES2/gl2.h>
#include "vector.h"

// Number of faces for cylinders representing each branch
#define BRANCHES_FACETS 5

// Initialization and clean-up
void Branches_initialize(GLuint t);
void Branches_deinitialize(void);
void Branches_clear(void);

// Creation
int  Branches_add(float n[3], float tc[2], float v[3]);
void Branches_addIndex(unsigned int);
void Branches_generateStump(int *lower);
void Branches_buildCylinder(
        int *idx, float4x4 mat, float taper, float texcoordY, GLboolean low);

// Query
int  Branches_polyCount(void);
int  Branches_branchCount(void);
int  Branches_sizeVBO(void);
int  Branches_numVertices(void);

// Rendering
void Branches_draw(int useVBO);
void Branches_buildVBO(void);

#endif // __BRANCHES_H
